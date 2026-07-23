// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Pitch: stick Y → cam+0x98.
// Yaw:   stick X → cam+0x9C (probe #14 locked).
// Zoom:  L or R → cam+0xB0.
//
// Freecam and L3 turbo are independent — never pause freecam while turbo is on
// (both work together after a map refresh; post-savestate turbo lag is a known open bug).
//
// Never rewrite CAMERA_SLOT (hard-crashed). No InvalidateCacheRange on data float writes.
// Map transitions (house floors, post-battle): recover via live pitch/yaw layout when FOV
// is 0/invalid — interiors often zero FOV briefly or use a different FOV scale.
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
constexpr u32 OFF_YAW = 0x9C;
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

/**
 * Pitch/yaw look like angles on a field camera (FOV may be dead in interiors / transitions).
 * All-zero is treated as wiped/dead memory — not a live camera.
 */
bool OkAngleLayout(float pitch_v, float yaw_v) {
    if (!OkFloat(pitch_v) || !OkFloat(yaw_v)) {
        return false;
    }
    if (std::fabs(pitch_v) >= 89.f || std::fabs(yaw_v) >= 1.0e4f) {
        return false;
    }
    // Wiped object is usually all zeros; require at least one non-trivial angle.
    return std::fabs(pitch_v) > 1.0e-3f || std::fabs(yaw_v) > 1.0e-3f;
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

void WriteF(Memory::MemorySystem& mem, Kernel::Process& process, u32 addr, float v) {
    mem.Write32(process, addr, FBits(v));
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

void FreeCam::OnCoreReconnect() {
    quiet_until = 1;
    in_battle = false;
    zero_fov_streak = 0;
    ResetYaw();
    LOG_INFO(Core, "Hoenn camera: core reconnect (savestate) — quiet then reseed");
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
            LOG_INFO(Core, "Hoenn camera: quiet after field/battle/savestate…");
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
        LOG_WARNING(Core, "Hoenn camera: cam {:08X} → {:08X} — reseed yaw (map/object change)",
                    last_cam, cam);
        yaw_seeded = false;
        last_cam = cam;
        // Brief quiet so the game finishes constructing the new camera object
        quiet_until = now + QUIET_AFTER_FIELD / 4; // ~50ms of emu time scale
        zero_fov_streak = 0;
        return;
    }

    const float cur_fov = BFloat(mem.Read32(*process, cam + OFF_FOV));
    const float peek_pitch = BFloat(mem.Read32(*process, cam + OFF_PITCH));
    const float peek_yaw = BFloat(mem.Read32(*process, cam + OFF_YAW));
    const bool fov_live = OkFov(cur_fov);
    const bool angles_live = OkAngleLayout(peek_pitch, peek_yaw);

    // Full dead object: neither FOV nor angle layout looks like a field camera.
    // Do NOT rewrite CAMERA_SLOT (crash history). Wait for game to publish a live cam.
    if (!fov_live && !angles_live) {
        zero_fov_streak++;
        yaw_seeded = false;
        if ((diag++ % 40) == 0) {
            LOG_WARNING(Core,
                        "Hoenn camera: dead cam={:08X} fov={:.2f} pitch={:.2f} yaw={:.2f} — "
                        "wait for live object (map transition / post-battle)",
                        cam, cur_fov, peek_pitch, peek_yaw);
        }
        return;
    }

    if (!fov_live) {
        // Transition / interior: FOV zero or out of range, but pitch/yaw still valid.
        // Drive freelook only; skip FOV writes so we don't blast a dead field.
        zero_fov_streak++;
        if ((diag++ % 60) == 0) {
            LOG_INFO(Core,
                     "Hoenn camera: FOV inactive cam={:08X} fov={:.2f} — pitch/yaw drive only "
                     "(house floor / transition recovery)",
                     cam, cur_fov);
        }
    } else {
        zero_fov_streak = 0;
    }

    // --- Zoom L/R — only when FOV field is live ---
    if (zoom_assist && fov_live) {
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
        WriteF(mem, *process, cam + OFF_FOV, user_fov);
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

    // --- Pitch Y ---
    if (std::fabs(sy) >= STICK_DEADZONE) {
        const float y = invert_y ? sy : -sy;
        pitch += y * sensitivity * PITCH_STEP;
        pitch = std::clamp(pitch, PITCH_MIN, PITCH_MAX);
    }
    WriteF(mem, *process, cam + OFF_PITCH, pitch);

    // --- Yaw X → +0x9C ---
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
        WriteF(mem, *process, cam + OFF_YAW, yaw);
    }

    if ((diag++ % 60) == 0) {
        LOG_INFO(Core,
                 "Hoenn camera: ok cam={:08X} fov={:.0f} pitch={:.2f} yaw={:.3g} (+0x9C) "
                 "sx={:+.2f} sy={:+.2f}",
                 cam, BFloat(mem.Read32(*process, cam + OFF_FOV)), pitch, yaw, sx, sy);
    }
}

} // namespace Hoenn
