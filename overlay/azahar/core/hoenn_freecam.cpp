// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Pitch: stick Y → cam+0x98 (product path).
// Yaw:   stick X → cam+0x9C (locked — RE probe #14, user dogfood 2026-07-23).
// Zoom:  L or R → cam+0xB0 (exclusive). Never rewrite CAMERA_SLOT.
//
// Failed / do not restore as primary:
//   F1 +0x90 only · F2/F3 sin/cos 0x90+0x94 · +0x94 alone (vertical) · F4 multi FOV
//   F5 slot rebind · F6 false eye/target · pad-train · N2 false pairs · multi-probe menu
#include "core/hoenn_freecam.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"

namespace Hoenn {

namespace {
constexpr VAddr CAMERA_SLOT = 0x085F67DC;
constexpr u32 OFF_PITCH = 0x98;
constexpr u32 OFF_YAW = 0x9C; // probe #14 — horizontal freelook (confirmed)
constexpr u32 OFF_FOV = 0xB0;

constexpr float PITCH_BASE = -12.74f;
constexpr float PITCH_MIN = -25.f;
constexpr float PITCH_MAX = 0.f;
constexpr float PITCH_STEP = 0.30f;

constexpr float YAW_STEP = 0.85f;

constexpr float FOV_MIN = 220.f;
constexpr float FOV_MAX = 750.f;
constexpr float FOV_ASSIST = 480.f;
constexpr float FOV_STEP = 12.f;
constexpr float STICK_DEADZONE = 0.18f;
constexpr int ANDROID_STICK_C = 718;

constexpr u64 QUIET_AFTER_FIELD = 200'000'000;

u32 FBits(float f) {
    u32 b = 0;
    std::memcpy(&b, &f, 4);
    return b;
}
float BFloat(u32 b) {
    float f = 0.f;
    std::memcpy(&f, &b, 4);
    return f;
}

bool HeapPtr(u32 p) {
    return p >= 0x08000000 && p < 0x0C000000;
}

bool OkFov(float f) {
    return std::isfinite(f) && f >= 100.f && f <= 1200.f;
}

bool OkFloat(float f) {
    return std::isfinite(f) && std::fabs(f) < 1.0e8f;
}

std::shared_ptr<Kernel::Process> Resolve(Core::System& sys, u32 pid) {
    auto p = sys.Kernel().GetProcessById(pid);
    if (p) {
        return p;
    }
    for (const auto& x : sys.Kernel().GetProcessList()) {
        if (x) {
            return x;
        }
    }
    return sys.Kernel().GetCurrentProcess();
}

bool IsFieldMod(std::string_view n) {
    return n == "DllField";
}

void WriteF(Memory::MemorySystem& mem, Kernel::Process& process, Core::System& system, u32 addr,
            float v) {
    mem.Write32(process, addr, FBits(v));
    system.InvalidateCacheRange(addr, 4);
}

} // namespace

FreeCam& FreeCam::GetInstance() {
    static FreeCam i;
    return i;
}

void FreeCam::ResetYaw() {
    yaw_seeded = false;
    yaw = 0.f;
    last_cam = 0;
}

void FreeCam::EnsureDevices() {
    if (!c_stick) {
        std::string param = Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CStick];
        if (param.empty() || param.find("null") != std::string::npos ||
            param.find("gamepad") == std::string::npos) {
            param = "engine:gamepad,code:" + std::to_string(ANDROID_STICK_C);
        }
        c_stick = Input::CreateDevice<Input::AnalogDevice>(param);
        LOG_INFO(Core, "Hoenn camera: C-Stick {}", param);
    }
    if (!btn_l) {
        btn_l = Input::CreateDevice<Input::ButtonDevice>(
            Settings::values.current_input_profile.buttons[Settings::NativeButton::L]);
    }
    if (!btn_r) {
        btn_r = Input::CreateDevice<Input::ButtonDevice>(
            Settings::values.current_input_profile.buttons[Settings::NativeButton::R]);
    }
}

void FreeCam::OnModuleLoaded(std::string_view name, u32 /*load_address*/) {
    if (name == "DllBattle") {
        in_battle = true;
        zero_fov_streak = 0;
        LOG_WARNING(Core, "Hoenn camera: battle enter — pause");
        return;
    }
    if (IsFieldMod(name)) {
        in_battle = false;
        quiet_until = 1;
        pitch = PITCH_BASE;
        ResetYaw();
        zero_fov_streak = 0;
        LOG_WARNING(Core, "Hoenn camera: DllField loaded — will quiet then drive");
    }
}

void FreeCam::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        in_battle = true;
        zero_fov_streak = 0;
        ResetYaw();
        LOG_INFO(Core, "Hoenn camera: DllField unload — pause");
    } else if (name == "DllBattle") {
        in_battle = false;
        quiet_until = 1;
        pitch = PITCH_BASE;
        ResetYaw();
        zero_fov_streak = 0;
        LOG_INFO(Core, "Hoenn camera: battle exit — will quiet then drive");
    }
}

void FreeCam::SetFreelookEnabled(bool e) {
    if (freelook == e) {
        return;
    }
    freelook = e;
    c_stick.reset();
    if (freelook) {
        pitch = PITCH_BASE;
        ResetYaw();
        LOG_WARNING(Core, "Hoenn free-look ON — pitch=Y (+0x98); yaw=X (+0x9C)");
    } else {
        LOG_INFO(Core, "Hoenn free-look OFF");
    }
}

bool FreeCam::IsFreelookEnabled() const {
    return freelook;
}

void FreeCam::SetZoomAssistEnabled(bool e) {
    if (zoom_assist == e) {
        return;
    }
    zoom_assist = e;
    btn_l.reset();
    btn_r.reset();
    if (zoom_assist) {
        user_fov = FOV_ASSIST;
        LOG_WARNING(Core, "Hoenn zoom assist ON");
    } else {
        LOG_INFO(Core, "Hoenn zoom assist OFF");
    }
}

bool FreeCam::IsZoomAssistEnabled() const {
    return zoom_assist;
}

void FreeCam::SetSensitivity(float s) {
    sensitivity = std::clamp(s, 0.25f, 10.f);
}
float FreeCam::GetSensitivity() const {
    return sensitivity;
}
void FreeCam::SetInvertX(bool v) {
    invert_x = v;
}
void FreeCam::SetInvertY(bool v) {
    invert_y = v;
}

void FreeCam::Tick(Core::System& system, u32 process_id) {
    if (!freelook && !zoom_assist) {
        return;
    }
    if (!system.IsPoweredOn()) {
        return;
    }

    if (quiet_until == 1) {
        quiet_until = system.CoreTiming().GetTicks() + QUIET_AFTER_FIELD;
    }

    if (in_battle) {
        return;
    }

    const u64 now = system.CoreTiming().GetTicks();
    if (now < quiet_until) {
        if ((diag++ % 40) == 0) {
            LOG_INFO(Core, "Hoenn camera: quiet after field/battle…");
        }
        return;
    }

    auto process = Resolve(system, process_id);
    if (!process) {
        return;
    }
    auto& mem = system.Memory();
    EnsureDevices();

    const u32 cam = mem.Read32(*process, CAMERA_SLOT);
    if (!HeapPtr(cam)) {
        if ((diag++ % 50) == 0) {
            LOG_WARNING(Core, "Hoenn camera: no cam at slot ({:08X})", cam);
        }
        return;
    }

    if (cam != last_cam) {
        LOG_WARNING(Core, "Hoenn camera: cam {:08X} → {:08X} — reseed yaw", last_cam, cam);
        yaw_seeded = false;
        last_cam = cam;
    }

    const float cur_fov = BFloat(mem.Read32(*process, cam + OFF_FOV));
    if (cur_fov == 0.f || !OkFov(cur_fov)) {
        zero_fov_streak++;
        yaw_seeded = false;
        if ((diag++ % 40) == 0) {
            LOG_WARNING(Core,
                        "Hoenn camera: dead/invalid cam={:08X} fov={:.2f} — pause (enter "
                        "building to recover)",
                        cam, cur_fov);
        }
        return;
    }
    zero_fov_streak = 0;

    if (zoom_assist) {
        bool l = false, r = false;
        try {
            if (btn_l) {
                l = btn_l->GetStatus();
            }
            if (btn_r) {
                r = btn_r->GetStatus();
            }
        } catch (...) {
            btn_l.reset();
            btn_r.reset();
        }
        if (l && !r) {
            user_fov = std::min(FOV_MAX, user_fov + FOV_STEP);
        } else if (r && !l) {
            user_fov = std::max(FOV_MIN, user_fov - FOV_STEP);
        }
        WriteF(mem, *process, system, cam + OFF_FOV, user_fov);
    }

    if (!freelook) {
        return;
    }

    float sx = 0.f, sy = 0.f;
    try {
        if (c_stick) {
            std::tie(sx, sy) = c_stick->GetStatus();
        }
    } catch (...) {
        c_stick.reset();
    }

    // --- Pitch (stick Y) ---
    if (std::fabs(sy) >= STICK_DEADZONE) {
        const float y = invert_y ? sy : -sy;
        pitch += y * sensitivity * PITCH_STEP;
        pitch = std::clamp(pitch, PITCH_MIN, PITCH_MAX);
    }
    WriteF(mem, *process, system, cam + OFF_PITCH, pitch);

    // --- Yaw (stick X → +0x9C) ---
    if (!yaw_seeded) {
        yaw = BFloat(mem.Read32(*process, cam + OFF_YAW));
        if (!OkFloat(yaw)) {
            yaw = 0.f;
        }
        yaw_seeded = true;
        LOG_WARNING(Core, "Hoenn yaw SEED +0x9C = {:.4g} cam={:08X}", yaw, cam);
    }
    if (std::fabs(sx) >= STICK_DEADZONE) {
        const float x = invert_x ? -sx : sx;
        yaw += x * sensitivity * YAW_STEP;
    }
    if (OkFloat(yaw)) {
        WriteF(mem, *process, system, cam + OFF_YAW, yaw);
    }

    if ((diag++ % 60) == 0) {
        LOG_INFO(Core,
                 "Hoenn camera: ok cam={:08X} fov={:.0f} pitch={:.2f} yaw={:.3g} (+0x9C) "
                 "sx={:+.2f} sy={:+.2f}",
                 cam, BFloat(mem.Read32(*process, cam + OFF_FOV)), pitch, yaw, sx, sy);
    }
}

} // namespace Hoenn
