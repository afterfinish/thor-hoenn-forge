// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Pitch (stick Y → +0x98): product path.
// Orbit (stick X): rotate horizontal XZ pair (+0x90/+0x94), mirror (+0x4C/+0x50).
//   Writing only +0x90 slid the camera; rotating both orbits around the player.
// FOV: L/R exclusive when zoom assist on.
//
// Post-battle: FOV=0 → pause writes. NEVER rewrite CAMERA_SLOT (rebind crashed).
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

// Primary orientation block (matches community pitch/FOV offsets)
constexpr u32 OFF_ORBIT_X = 0x90;
constexpr u32 OFF_ORBIT_Z = 0x94;
// Duplicate earlier block (same floats in dumps — keep both in sync)
constexpr u32 OFF_ORBIT_X_ALT = 0x4C;
constexpr u32 OFF_ORBIT_Z_ALT = 0x50;

constexpr float PITCH_BASE = -12.74f;
constexpr float PITCH_MIN = -25.f;
constexpr float PITCH_MAX = 0.f;
constexpr float PITCH_STEP = 0.30f;

// Orbit angular speed (radians per tick at full stick * sensitivity)
constexpr float ORBIT_STEP = 0.055f;
constexpr float ORBIT_RADIUS_MIN = 8.f;
constexpr float ORBIT_RADIUS_MAX = 200.f;
constexpr float ORBIT_RADIUS_DEFAULT = 43.f; // observed idle ~ hypot(30.2, 30.8)

constexpr float FOV_MIN = 220.f;
constexpr float FOV_MAX = 750.f;
constexpr float FOV_ASSIST = 480.f;
constexpr float FOV_STEP = 12.f;
constexpr float STICK_DEADZONE = 0.20f;
constexpr int ANDROID_STICK_C = 718;

constexpr u64 QUIET_AFTER_FIELD = 200'000'000;
constexpr float PI = 3.14159265358979323846f;

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

void WriteOrbitPair(Memory::MemorySystem& mem, Kernel::Process& process, Core::System& system,
                    u32 cam, float x, float z) {
    mem.Write32(process, cam + OFF_ORBIT_X, FBits(x));
    mem.Write32(process, cam + OFF_ORBIT_Z, FBits(z));
    mem.Write32(process, cam + OFF_ORBIT_X_ALT, FBits(x));
    mem.Write32(process, cam + OFF_ORBIT_Z_ALT, FBits(z));
    system.InvalidateCacheRange(cam + OFF_ORBIT_X_ALT, 8);
    system.InvalidateCacheRange(cam + OFF_ORBIT_X, 8);
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
        orbit_seeded = false;
        zero_fov_streak = 0;
        LOG_WARNING(Core, "Hoenn camera: DllField loaded — will quiet then drive");
    }
}

void FreeCam::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        in_battle = true;
        zero_fov_streak = 0;
        orbit_seeded = false;
        LOG_INFO(Core, "Hoenn camera: DllField unload — pause");
    } else if (name == "DllBattle") {
        in_battle = false;
        quiet_until = 1;
        pitch = PITCH_BASE;
        orbit_seeded = false;
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
        orbit_seeded = false;
        LOG_WARNING(Core, "Hoenn free-look ON — pitch=Y, orbit yaw=X (XZ pair +0x90/+0x94)");
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

    const float cur_fov = BFloat(mem.Read32(*process, cam + OFF_FOV));
    if (cur_fov == 0.f || !OkFov(cur_fov)) {
        zero_fov_streak++;
        orbit_seeded = false;
        if ((diag++ % 40) == 0) {
            LOG_WARNING(Core,
                        "Hoenn camera: dead/invalid cam={:08X} fov={:.2f} — pause (enter "
                        "building to recover)",
                        cam, cur_fov);
        }
        return;
    }
    zero_fov_streak = 0;

    // --- Zoom L/R (exclusive) ---
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

    // --- Pitch: stick Y (unchanged product path) ---
    if (std::fabs(sy) >= STICK_DEADZONE) {
        const float y = invert_y ? sy : -sy;
        pitch += y * sensitivity * PITCH_STEP;
        pitch = std::clamp(pitch, PITCH_MIN, PITCH_MAX);
    }
    mem.Write32(*process, cam + OFF_PITCH, FBits(pitch));
    system.InvalidateCacheRange(cam + OFF_PITCH, 4);

    // --- Orbit yaw: stick X rotates XZ horizontal offset around player ---
    if (!orbit_seeded) {
        float ox = BFloat(mem.Read32(*process, cam + OFF_ORBIT_X));
        float oz = BFloat(mem.Read32(*process, cam + OFF_ORBIT_Z));
        if (!std::isfinite(ox) || !std::isfinite(oz)) {
            ox = BFloat(mem.Read32(*process, cam + OFF_ORBIT_X_ALT));
            oz = BFloat(mem.Read32(*process, cam + OFF_ORBIT_Z_ALT));
        }
        float r = std::hypot(ox, oz);
        if (!std::isfinite(r) || r < ORBIT_RADIUS_MIN || r > ORBIT_RADIUS_MAX) {
            r = ORBIT_RADIUS_DEFAULT;
            ox = r; // seed facing +X if garbage
            oz = 0.f;
        }
        orbit_radius = r;
        orbit_angle = std::atan2(oz, ox);
        orbit_seeded = true;
        LOG_WARNING(Core, "Hoenn orbit seed angle={:.2f} rad radius={:.1f} xz=({:.1f},{:.1f})",
                    orbit_angle, orbit_radius, ox, oz);
    }

    if (std::fabs(sx) >= STICK_DEADZONE) {
        // invert_x defaults true so stick right → orbit that feels natural
        const float x = invert_x ? -sx : sx;
        orbit_angle += x * sensitivity * ORBIT_STEP;
        // Keep angle in [-pi, pi] for tidy logs
        while (orbit_angle > PI) {
            orbit_angle -= 2.f * PI;
        }
        while (orbit_angle < -PI) {
            orbit_angle += 2.f * PI;
        }
    }

    // Hold orbit every frame (like pitch) so game does not snap back
    {
        const float nx = std::cos(orbit_angle) * orbit_radius;
        const float nz = std::sin(orbit_angle) * orbit_radius;
        WriteOrbitPair(mem, *process, system, cam, nx, nz);
    }

    if ((diag++ % 60) == 0) {
        LOG_INFO(Core,
                 "Hoenn camera: ok cam={:08X} fov={:.1f} pitch={:.2f} orbit={:.1f}° r={:.1f}",
                 cam, BFloat(mem.Read32(*process, cam + OFF_FOV)), pitch,
                 orbit_angle * (180.f / PI), orbit_radius);
    }
}

} // namespace Hoenn
