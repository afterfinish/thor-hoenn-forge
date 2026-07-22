// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Post-battle: official slot often still holds a dead object (FOV reads 0).
// We rebind *CAMERA_SLOT to a nearby live camera object (one u32 write to
// BSS) — mirrors what entering a building does — then drive FOV/pitch only
// on that official object. No multi-write into random heap (black screens).
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
constexpr u32 OFF_FOV = 0xB0;

constexpr float PITCH_BASE = -12.74f; // community third-person
constexpr float PITCH_MIN = -25.f;
constexpr float PITCH_MAX = 0.f;
constexpr float PITCH_STEP = 0.30f;

constexpr float FOV_MIN = 220.f;
constexpr float FOV_MAX = 750.f;
constexpr float FOV_ASSIST = 480.f;
constexpr float FOV_STEP = 12.f;
// Vanilla-ish FOV used when hunting a live camera after battle
constexpr float FOV_VANILLA_LO = 180.f;
constexpr float FOV_VANILLA_HI = 420.f;

constexpr float STICK_DEADZONE = 0.20f;
constexpr int ANDROID_STICK_C = 718;

constexpr u64 QUIET_AFTER_FIELD = 200'000'000;   // ~0.75s
constexpr u64 REBIND_COOLDOWN = 400'000'000;     // don't scan every tick
constexpr u32 ZERO_FOV_BEFORE_REBIND = 8;        // ~0.5s of dead FOV at ~15Hz

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

bool OkPitch(float f) {
    return std::isfinite(f) && f > -80.f && f < 40.f;
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
    // Only main field module — not every DllFieldEvent* (those spammed quiet)
    return n == "DllField";
}
} // namespace

FreeCam& FreeCam::GetInstance() {
    static FreeCam i;
    return i;
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

void FreeCam::OnModuleLoaded(std::string_view name) {
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
        zero_fov_streak = 0;
        LOG_WARNING(Core, "Hoenn camera: DllField loaded — will quiet then drive/rebind");
    }
}

void FreeCam::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        in_battle = true;
        zero_fov_streak = 0;
        LOG_INFO(Core, "Hoenn camera: DllField unload — pause");
    } else if (name == "DllBattle") {
        in_battle = false;
        quiet_until = 1;
        pitch = PITCH_BASE;
        zero_fov_streak = 0;
        LOG_INFO(Core, "Hoenn camera: battle exit — will quiet then drive/rebind");
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
        LOG_WARNING(Core, "Hoenn free-look ON");
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

/**
 * After battle the slot often still names a dead object (FOV=0) while the game
 * already uses another heap camera. Find a nearby object with vanilla-ish FOV
 * and pitch, then write its address into CAMERA_SLOT only.
 */
bool FreeCam::TryRebindLiveCamera(Core::System& system, u32 process_id) {
    auto process = Resolve(system, process_id);
    if (!process) {
        return false;
    }
    auto& mem = system.Memory();
    const u32 dead = mem.Read32(*process, CAMERA_SLOT);

    // Search near the dead pointer first (new field cams allocate nearby)
    u32 lo = 0x08000000;
    u32 hi = 0x0A000000;
    if (HeapPtr(dead)) {
        lo = dead > 0x400000 ? dead - 0x400000 : 0x08000000;
        hi = dead + 0x400000;
        if (hi > 0x0C000000) {
            hi = 0x0C000000;
        }
    }

    u32 best = 0;
    float best_score = 1e9f;
    u32 checked = 0;

    // Step 0x20: camera objects are large; keeps scan under ~0.2M reads
    for (u32 base = lo; base + OFF_FOV + 4 < hi; base += 0x20) {
        checked++;
        if (base == dead) {
            continue;
        }
        const float fov = BFloat(mem.Read32(*process, base + OFF_FOV));
        if (fov < FOV_VANILLA_LO || fov > FOV_VANILLA_HI || !std::isfinite(fov)) {
            continue;
        }
        const float pitch_v = BFloat(mem.Read32(*process, base + OFF_PITCH));
        if (!OkPitch(pitch_v)) {
            continue;
        }
        // Prefer FOV near typical overworld (~250–300)
        const float score = std::fabs(fov - 270.f) + std::fabs(pitch_v + 10.f) * 0.1f;
        if (score < best_score) {
            best_score = score;
            best = base;
        }
    }

    if (!best) {
        LOG_WARNING(Core, "Hoenn camera: rebind failed (no live cam near {:08X}, checked={})", dead,
                    checked);
        return false;
    }

    mem.Write32(*process, CAMERA_SLOT, best);
    system.InvalidateCacheRange(CAMERA_SLOT, 4);

    // Verify
    const u32 verify = mem.Read32(*process, CAMERA_SLOT);
    const float vfov = BFloat(mem.Read32(*process, best + OFF_FOV));
    LOG_WARNING(Core,
                "Hoenn camera: REBOUND slot {:08X} → {:08X} (fov={:.1f} score={:.1f}) — "
                "same as map-refresh",
                dead, verify, vfov, best_score);
    zero_fov_streak = 0;
    pitch = PITCH_BASE;
    return verify == best;
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

    u32 cam = mem.Read32(*process, CAMERA_SLOT);
    if (!HeapPtr(cam)) {
        // Slot empty — try rebind once cooldown allows
        if (now >= next_rebind_attempt) {
            next_rebind_attempt = now + REBIND_COOLDOWN;
            if (TryRebindLiveCamera(system, process_id)) {
                cam = mem.Read32(*process, CAMERA_SLOT);
            }
        }
        if (!HeapPtr(cam)) {
            if ((diag++ % 50) == 0) {
                LOG_WARNING(Core, "Hoenn camera: no cam at slot ({:08X})", cam);
            }
            return;
        }
    }

    const float cur_fov = BFloat(mem.Read32(*process, cam + OFF_FOV));

    // Dead post-battle object: FOV stuck at 0 while game renders elsewhere
    if (cur_fov == 0.f || !OkFov(cur_fov)) {
        zero_fov_streak++;
        if (zero_fov_streak >= ZERO_FOV_BEFORE_REBIND && now >= next_rebind_attempt) {
            next_rebind_attempt = now + REBIND_COOLDOWN;
            LOG_WARNING(Core, "Hoenn camera: dead object cam={:08X} fov={:.2f} — rebind", cam,
                        cur_fov);
            if (TryRebindLiveCamera(system, process_id)) {
                cam = mem.Read32(*process, CAMERA_SLOT);
            } else {
                return;
            }
        } else {
            if ((diag++ % 40) == 0) {
                LOG_WARNING(Core, "Hoenn camera: waiting live cam (fov={:.2f} streak={})", cur_fov,
                            zero_fov_streak);
            }
            return;
        }
    } else {
        zero_fov_streak = 0;
    }

    // Re-read FOV after possible rebind
    if (!OkFov(BFloat(mem.Read32(*process, cam + OFF_FOV)))) {
        return;
    }

    // --- Zoom L/R ---
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
        mem.Write32(*process, cam + OFF_FOV, FBits(user_fov));
        system.InvalidateCacheRange(cam + OFF_FOV, 4);
    }

    // --- Freelook stick Y ---
    if (freelook) {
        float sx = 0.f, sy = 0.f;
        try {
            if (c_stick) {
                std::tie(sx, sy) = c_stick->GetStatus();
            }
        } catch (...) {
            c_stick.reset();
        }
        (void)sx;
        (void)invert_x;
        if (std::fabs(sy) >= STICK_DEADZONE) {
            const float y = invert_y ? sy : -sy;
            pitch += y * sensitivity * PITCH_STEP;
            pitch = std::clamp(pitch, PITCH_MIN, PITCH_MAX);
        }
        mem.Write32(*process, cam + OFF_PITCH, FBits(pitch));
        system.InvalidateCacheRange(cam + OFF_PITCH, 4);
    }

    if ((diag++ % 60) == 0) {
        LOG_INFO(Core, "Hoenn camera: ok cam={:08X} fov={:.1f} pitch={:.2f}", cam,
                 BFloat(mem.Read32(*process, cam + OFF_FOV)), pitch);
    }
}

} // namespace Hoenn
