// Copyright Hoenn Forge — ORAS free look (houses) + L/R zoom assist
//
// Ship path only: GOLD dual pitch/yaw (+0x98/+0x9C and mirrors +0x54/+0x58).
// No mode unlock, CRO patch, shadows, matrix thrash, or RE experiment menu.
// Those caused black screens, freezes, and poison after map transitions.

#include "core/hoenn_freecam.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"

namespace Hoenn {

namespace {
constexpr VAddr CAMERA_SLOT = 0x085F67DC;
constexpr u32 OFF_FLAG = 0x80;
constexpr u32 OFF_MODE = 0x8C;
constexpr u32 OFF_PITCH = 0x98;
constexpr u32 OFF_YAW = 0x9C;
constexpr u32 OFF_FOV = 0xB0;
constexpr u32 OFF_PITCH_ALT = 0x54;
constexpr u32 OFF_YAW_ALT = 0x58;
constexpr u32 LIVE_FLAG = 0x0F;
constexpr u32 MODE_FREELOOK = 0x00020001;

constexpr float PITCH_MIN = -25.f;
constexpr float PITCH_MAX = 0.f;
constexpr float PITCH_STEP = 0.30f;
constexpr float YAW_STEP = 0.85f;
constexpr float FOV_GOLD_LO = 150.f;
// Zoom pushes FOV past the ~225 default house value, so acceptance spans the zoom band
constexpr float FOV_ZOOM_MIN = 180.f;
constexpr float FOV_ZOOM_MAX = 750.f;
constexpr float FOV_STEP = 10.f;
constexpr u32 OFF_FOV_ALT = 0x6C;
constexpr float STICK_DEADZONE = 0.18f;
constexpr int ANDROID_STICK_C = 718;

constexpr u64 QUIET_AFTER_FIELD = 200'000'000;
constexpr u64 QUIET_AFTER_TRANSITION = 80'000'000;
constexpr u64 COLLECT_INTERVAL = 40'000'000;
constexpr u64 COLLECT_MISS_INTERVAL = 120'000'000;
constexpr u32 SCAN_RADIUS_HOT = 0x30000;
constexpr u32 SCAN_STEP = 0x10;
// Names avoid Linux sched.h macros (SCHED_IDLE etc.)
constexpr u64 kSchedTools = 16'000'000;
constexpr u64 kSchedIdle = 50'000'000;

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

bool OkFloat(float f) {
    return std::isfinite(f) && std::fabs(f) < 1.0e8f;
}

// A camera FOV across the full zoom band — the acceptance test for anything we are
// about to overwrite with an FOV. Deliberately narrow at the bottom: a guest pointer
// (0x08000000–0x0BFFFFFF) reinterpreted as a float is ~1e-32 and a small integer field
// is smaller still, so neither can pass and neither can be clobbered.
bool OkCameraFov(float f) {
    return std::isfinite(f) && f >= FOV_GOLD_LO && f <= FOV_ZOOM_MAX;
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
}

void FreeCam::OnCoreReconnect() {
    quiet_until = 1;
    in_battle = false;
    live_count = 0;
    primary_cam = 0;
    last_collect_tick = 0;
    ResetYaw();
}

void FreeCam::EnsureDevices() {
    if (!c_stick) {
        std::string param =
            Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CStick];
        if (param.empty() || param.find("null") != std::string::npos ||
            param.find("gamepad") == std::string::npos) {
            param = "engine:gamepad,code:" + std::to_string(ANDROID_STICK_C);
        }
        c_stick = Input::CreateDevice<Input::AnalogDevice>(param);
    }
    // L/R: prefer bound profile; fall back to Android shoulder buttons (L1/R1)
    auto make_btn = [](Settings::NativeButton::Values id, int android_code) {
        std::string param = Settings::values.current_input_profile.buttons[id];
        if (param.empty() || param.find("null") != std::string::npos) {
            param = "engine:gamepad,code:" + std::to_string(android_code);
        }
        return Input::CreateDevice<Input::ButtonDevice>(param);
    };
    if (!btn_l) {
        // KEYCODE_BUTTON_L1 = 102
        btn_l = make_btn(Settings::NativeButton::L, 102);
    }
    if (!btn_r) {
        // KEYCODE_BUTTON_R1 = 103
        btn_r = make_btn(Settings::NativeButton::R, 103);
    }
}

void FreeCam::OnModuleLoaded(std::string_view name, u32 /*load_address*/) {
    if (name == "DllBattle") {
        in_battle = true;
        live_count = 0;
        primary_cam = 0;
    } else if (name == "DllField") {
        in_battle = false;
        quiet_until = 1;
        live_count = 0;
        primary_cam = 0;
        last_collect_tick = 0;
        ResetYaw();
    }
}

void FreeCam::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        live_count = 0;
        primary_cam = 0;
    } else if (name == "DllBattle") {
        in_battle = false;
    }
}

void FreeCam::SetFreelookEnabled(bool e) {
    if (freelook == e) {
        return;
    }
    freelook = e;
    c_stick.reset();
    if (freelook) {
        ResetYaw();
        LOG_WARNING(Core, "Hoenn freelook ON (houses/interiors — dual GOLD eulers)");
    } else {
        LOG_INFO(Core, "Hoenn freelook OFF");
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
    fov_seeded = false; // re-seed from live cam FOV on next tick (don't jump to 480)
    if (zoom_assist) {
        LOG_WARNING(Core, "Hoenn zoom assist ON (hold L out / R in)");
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

u64 FreeCam::GetScheduleInterval() const {
    return (freelook || zoom_assist) ? kSchedTools : kSchedIdle;
}

void FreeCam::SeedAnglesFromCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam) {
    if (!HeapPtr(cam)) {
        return;
    }
    const float p = BFloat(mem.Read32(process, cam + OFF_PITCH));
    const float y = BFloat(mem.Read32(process, cam + OFF_YAW));
    if (OkFloat(p) && p >= PITCH_MIN - 5.f && p <= PITCH_MAX + 5.f) {
        pitch = std::clamp(p, PITCH_MIN, PITCH_MAX);
    }
    if (OkFloat(y)) {
        yaw = y;
        yaw_seeded = true;
    }
}

bool FreeCam::IsGoldLive(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const {
    if (!HeapPtr(base) || base + OFF_FOV + 4 >= 0x0C000000) {
        return false;
    }
    const u32 flag = mem.Read32(process, base + OFF_FLAG);
    const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
    // Accept FOV through the full zoom band. Using only the discovery band (≤400)
    // made freelook die after zooming out — WriteFreelook gates on IsGoldLive.
    return flag == LIVE_FLAG && OkCameraFov(fov);
}

void FreeCam::CollectLiveTargets(Memory::MemorySystem& mem, Kernel::Process& process,
                                 u32 slot_cam) {
    live_count = 0;
    primary_cam = 0;
    std::unordered_set<u32> seen;

    struct Hit {
        u32 base;
        float score;
    };
    Hit hits[16]{};
    int hit_n = 0;

    auto consider = [&](u32 base) {
        if (!IsGoldLive(mem, process, base) || seen.count(base) || hit_n >= 16) {
            return;
        }
        seen.insert(base);
        const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
        float s = std::fabs(fov - 225.f);
        if (base == slot_cam) {
            s -= 100.f;
        }
        hits[hit_n++] = {base, s};
    };

    consider(slot_cam);
    consider(last_good_cam);
    const u32 bss0 = static_cast<u32>(CAMERA_SLOT);
    for (u32 a = bss0 - 0x40; a <= bss0 + 0x40; a += 4) {
        consider(mem.Read32(process, a));
    }

    auto scan_near = [&](u32 center, u32 radius) {
        if (!HeapPtr(center)) {
            return;
        }
        const u32 lo = center > radius ? center - radius : 0x08000000;
        const u32 hi = std::min(center + radius, 0x0BFFFFFFu);
        for (u32 base = lo; base + OFF_FOV + 4 < hi; base += SCAN_STEP) {
            consider(base);
        }
    };
    scan_near(slot_cam, SCAN_RADIUS_HOT);
    if (last_good_cam != slot_cam) {
        scan_near(last_good_cam, SCAN_RADIUS_HOT);
    }
    // Known house GOLD band (small window only)
    scan_near(0x082D3000, 0x8000);

    // Sort by score (insertion for tiny n)
    for (int i = 1; i < hit_n; ++i) {
        Hit key = hits[i];
        int j = i - 1;
        while (j >= 0 && hits[j].score > key.score) {
            hits[j + 1] = hits[j];
            --j;
        }
        hits[j + 1] = key;
    }

    for (int i = 0; i < hit_n && live_count < kMaxLiveTargets; ++i) {
        live_targets[static_cast<size_t>(live_count++)] = hits[i].base;
    }

    if (IsGoldLive(mem, process, slot_cam)) {
        primary_cam = slot_cam;
    } else {
        for (int i = 0; i < live_count; ++i) {
            const u32 b = live_targets[static_cast<size_t>(i)];
            if (mem.Read32(process, b + OFF_MODE) == MODE_FREELOOK) {
                primary_cam = b;
                break;
            }
        }
        if (primary_cam == 0 && live_count > 0) {
            primary_cam = live_targets[0];
        }
    }
    if (primary_cam) {
        last_good_cam = primary_cam;
    }
}

void FreeCam::WriteFreelook(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam) {
    if (!IsGoldLive(mem, process, cam)) {
        return;
    }
    // Dual eulers only — no mode writes (mode thrash poisoned screens)
    WriteF(mem, process, cam + OFF_PITCH, pitch);
    WriteF(mem, process, cam + OFF_PITCH_ALT, pitch);
    if (OkFloat(yaw)) {
        WriteF(mem, process, cam + OFF_YAW, yaw);
        WriteF(mem, process, cam + OFF_YAW_ALT, yaw);
    }
}

void FreeCam::WriteZoomFov(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam) {
    // Identify the object before writing to it. This used to be a bare HeapPtr bounds
    // test, so the write landed on any heap object that happened to hold 0x0F at +0x80.
    if (!IsGoldLive(mem, process, cam)) {
        return;
    }
    WriteF(mem, process, cam + OFF_FOV, user_fov);
    // Dual FOV so house/interior layout stays coherent — but only where the mirror
    // already holds an FOV. Writing +0x6C blind is what overwrote a guest pointer with
    // 180.0f (FOV_ZOOM_MIN, what R clamps to). The game then dereferenced 0x43340000 —
    // the bit pattern of 180.0f — and walked an array from there, flooding the
    // emulation thread with unmapped reads at roughly 100k/s until it stalled.
    if (OkCameraFov(BFloat(mem.Read32(process, cam + OFF_FOV_ALT)))) {
        WriteF(mem, process, cam + OFF_FOV_ALT, user_fov);
    }
}

void FreeCam::Tick(Core::System& system, u32 process_id) {
    if (!freelook && !zoom_assist) {
        return;
    }
    if (!system.IsPoweredOn() || in_battle) {
        return;
    }

    last_process_id = process_id;
    if (quiet_until == 1) {
        quiet_until = system.CoreTiming().GetTicks() + QUIET_AFTER_FIELD;
    }

    const u64 now = system.CoreTiming().GetTicks();
    auto process = Resolve(system, process_id);
    if (!process) {
        return;
    }
    auto& mem = system.Memory();
    EnsureDevices();

    const u32 slot_cam = mem.Read32(*process, CAMERA_SLOT);
    if (HeapPtr(slot_cam) && last_slot_cam != 0 && slot_cam != last_slot_cam) {
        quiet_until = now + QUIET_AFTER_TRANSITION;
        live_count = 0;
        primary_cam = 0;
        last_collect_tick = 0;
        ResetYaw();
    }
    if (HeapPtr(slot_cam)) {
        last_slot_cam = slot_cam;
    }
    if (now < quiet_until) {
        return;
    }

    const u64 collect_gap = (live_count > 0) ? COLLECT_INTERVAL : COLLECT_MISS_INTERVAL;
    if (last_collect_tick == 0 || now >= last_collect_tick + collect_gap) {
        const u32 prev_pri = primary_cam;
        CollectLiveTargets(mem, *process, slot_cam);
        last_collect_tick = now;
        if (primary_cam && primary_cam != prev_pri) {
            SeedAnglesFromCam(mem, *process, primary_cam);
        }
    }

    // Zoom assist: pick any live gold as target; FOV may leave the discovery band.
    // Candidates are validated with IsGoldLive rather than the bare 0x0F flag byte —
    // a u32 15 at +0x80 is a common heap pattern, and accepting it is what let the FOV
    // write land on a non-camera object. Skipped entirely when nothing validated as
    // gold (the outdoor case), where the struct layout does not hold and zoom has no
    // visible effect anyway because the game re-derives FOV each frame.
    if (zoom_assist && live_count > 0) {
        u32 zcam = primary_cam;
        if (!IsGoldLive(mem, *process, zcam)) {
            zcam = 0;
            for (int i = 0; i < live_count; ++i) {
                const u32 b = live_targets[static_cast<size_t>(i)];
                if (IsGoldLive(mem, *process, b)) {
                    zcam = b;
                    break;
                }
            }
            if (!zcam && IsGoldLive(mem, *process, slot_cam)) {
                zcam = slot_cam;
            }
        }
        if (zcam) {
            if (!fov_seeded) {
                const float cur = BFloat(mem.Read32(*process, zcam + OFF_FOV));
                user_fov = OkFloat(cur) ? std::clamp(cur, FOV_ZOOM_MIN, FOV_ZOOM_MAX) : 280.f;
                fov_seeded = true;
            }
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
            // L = zoom out (wider FOV), R = zoom in (narrower FOV)
            if (l && !r) {
                user_fov = std::min(FOV_ZOOM_MAX, user_fov + FOV_STEP);
            } else if (r && !l) {
                user_fov = std::max(FOV_ZOOM_MIN, user_fov - FOV_STEP);
            }
            // Re-assert every tick so the game cannot snap FOV back
            WriteZoomFov(mem, *process, zcam);
        }
    }

    if (!freelook) {
        return;
    }
    if (live_count == 0) {
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

    if (std::fabs(sy) >= STICK_DEADZONE) {
        const float y = invert_y ? sy : -sy;
        pitch += y * sensitivity * PITCH_STEP;
        pitch = std::clamp(pitch, PITCH_MIN, PITCH_MAX);
    }
    if (!yaw_seeded && primary_cam) {
        const float y0 = BFloat(mem.Read32(*process, primary_cam + OFF_YAW));
        yaw = OkFloat(y0) ? y0 : 0.f;
        yaw_seeded = true;
    }
    if (std::fabs(sx) >= STICK_DEADZONE) {
        const float x = invert_x ? -sx : sx;
        yaw += x * sensitivity * YAW_STEP;
    }

    for (int i = 0; i < live_count; ++i) {
        WriteFreelook(mem, *process, live_targets[static_cast<size_t>(i)]);
    }

    // Rare status only — avoid log spam on hot path
    if ((diag++ % 300) == 0 && primary_cam) {
        LOG_INFO(Core, "Hoenn freelook n={} pri={:08X} p={:.1f} y={:.1f}", live_count, primary_cam,
                 pitch, yaw);
    }
}

} // namespace Hoenn
