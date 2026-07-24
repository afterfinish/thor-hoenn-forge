// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Pitch +0x98 / yaw +0x9C / FOV +0xB0 (L/R on primary GOLD only).
// Dual pitch/yaw mirrors +0x54/+0x58 (match working PRI layout).
//
// FAILED / regressed (do not repeat):
//   v3 mode @+0x8C only — no visual fix
//   v4 FOV=200 echoes — list junk
//   v5 dual-mode unlock — forced FREE on stereo golds (mode=0) → house/2F poison;
//     1F dead until leave/reenter; 2F freelook gone
//   v5 shadow writes — not proven render-owned; risk poison
//   silver multi · CAMERA_SLOT rewrite
//
// v6 gold-stable: GOLD only (flag 0x0F + FOV band). No mode writes. No shadow
// writes. RE still logs SHADOW? candidates read-only.
//
// v7 freeze-fix: house floor / leave transitions froze the emu because
// CollectLiveTargets scanned multi-MB radii (incl. shadow RE) and, when no
// GOLD was found, forced a full rescan every freecam tick (~16M). Hot path
// is now cheap (pointers + tight GOLD scan); echoes only on DumpREState;
// empty-result backoff + brief quiet on slot change.
//
// v7b: ZONE-SHIFT logger — read-only (mode/FOV/dist/flag/angle flips).
//
// FAILED probes (do not re-enable writes):
//   T3 dual angle +50/+94 in TOWN — sticky RAM, zero visual
//   T2 dual FOV +6C/+B0 in TOWN — game often OW FOV back to ~270; no visual zoom
//   T4 dual dist +64/+A8 in TOWN — sticky RAM (d_ow=0) even at 3396, zero visual
//   Town dual-block float thrash is exhausted — need off-object / code path (T7/T6)
//
// T7 closed for freelook: same-object pad only had dual band + state ints.
// T8 found FOV twin @08286380 (flag=0 CAMISH) tracks pri +6C err=0 — shadow class.
// T9 FAIL (stripped): FOV twin real; eulers on twin not freelook.
// Experiment modes (START menu): see FreeCam::Experiment — dogfood without rebuild.
// Known FAILs remain selectable for A/B; default Stable = house-proven GOLD eulers.
#include "core/hoenn_freecam.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

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
// Primary freelook fields (community + RE)
constexpr u32 OFF_MODE = 0x8C;
constexpr u32 OFF_PITCH = 0x98;
constexpr u32 OFF_YAW = 0x9C;
constexpr u32 OFF_FOV = 0xB0;
// Dual / mirror block (PRI dumps: +40..+74 == +80..+B8 shape)
constexpr u32 OFF_MODE_ALT = 0x48; // dual of +0x8C
constexpr u32 OFF_ANGLE_ALT = 0x50; // dual of +0x94
constexpr u32 OFF_PITCH_ALT = 0x54;
constexpr u32 OFF_YAW_ALT = 0x58;
constexpr u32 OFF_DIST_ALT = 0x64; // dual of +0xA8
constexpr u32 OFF_FOV_ALT = 0x6C;  // RE only (layout gate + dump)
constexpr u32 OFF_ANGLE = 0x94;
constexpr u32 OFF_DIST = 0xA8;
constexpr u32 LIVE_FLAG = 0x0F;
constexpr u32 MODE_FREELOOK = 0x00020001;
constexpr u32 MODE_TOWN = 0x000D0001;

constexpr float PITCH_BASE = -12.74f;
constexpr float PITCH_MIN = -25.f;
constexpr float PITCH_MAX = 0.f;
constexpr float PITCH_STEP = 0.30f;
constexpr float YAW_STEP = 0.85f;

constexpr float FOV_GOLD_LO = 150.f;
constexpr float FOV_GOLD_HI = 400.f;
constexpr float FOV_ZOOM_MIN = 220.f;
constexpr float FOV_ZOOM_MAX = 750.f;
constexpr float FOV_ASSIST = 480.f;
constexpr float FOV_STEP = 12.f;
constexpr float STICK_DEADZONE = 0.18f;
constexpr int ANDROID_STICK_C = 718;
constexpr float ANGLE_STEP = 1.20f;
constexpr float TOWN_FOV_STEP = 8.f;
constexpr float TOWN_FOV_MIN = 180.f;
constexpr float TOWN_FOV_MAX = 480.f;
constexpr float TOWN_DIST_STEP = 40.f;
constexpr float TOWN_DIST_MIN = 400.f;
constexpr float TOWN_DIST_MAX = 5000.f;
constexpr float ORBIT_YAW_STEP = 0.045f; // rad per stick unit

constexpr u64 QUIET_AFTER_FIELD = 200'000'000;
// Brief settle after interior/slot hop (avoids scanning mid-teardown)
constexpr u64 QUIET_AFTER_TRANSITION = 80'000'000;
// ~150ms when we have targets; longer when empty so miss storms don't freeze
constexpr u64 COLLECT_INTERVAL = 40'000'000;
constexpr u64 COLLECT_MISS_INTERVAL = 120'000'000;
// Hot path: tight radii only. Wide RE scan is DumpREState-only.
constexpr u32 SCAN_RADIUS_HOT = 0x30000;   // 192 KiB around slot / last_good
constexpr u32 SCAN_RADIUS_WIDE = 0x180000; // DumpRE / LogWideScan only
constexpr u32 ECHO_RADIUS = 0x180000;      // need reach 08285648-class
constexpr u32 SCAN_STEP = 0x10;
// Shadow must match primary FOV closely (rejects FOV=200 junk nodes)
constexpr float ECHO_FOV_MATCH = 2.0f;
// T8 FOV correlate — tight, throttled (never multi-MB every tick)
constexpr u32 T8_SCAN_RADIUS = 0x10000; // 64 KiB each side
constexpr u32 T8_SCAN_STEP = 0x10;
constexpr float T8_FOV_DELTA = 0.75f;   // trigger when +6C moves this much
constexpr u64 T8_SCAN_COOLDOWN = 100'000'000; // ~0.37s between full scans
constexpr float T8_FOV_MATCH = 1.25f;
constexpr int T8_MAX_HITS = 12;
constexpr int T8_MAX_TRACK = 8;
// T6: only aligned 000D0001 in DllField mode table (file offset)
constexpr u32 DLLFIELD_TOWN_MODE_OFF = 0x1195A0;

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

bool OkGoldFov(float f) {
    return std::isfinite(f) && f >= FOV_GOLD_LO && f <= FOV_GOLD_HI;
}

bool OkPitchLayout(float p) {
    return OkFloat(p) && p >= -45.f && p <= 15.f;
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

void WriteU32(Memory::MemorySystem& mem, Kernel::Process& process, u32 addr, u32 v) {
    mem.Write32(process, addr, v);
}

} // namespace

FreeCam& FreeCam::GetInstance() {
    static FreeCam i;
    return i;
}

void FreeCam::ResetYaw() {
    yaw_seeded = false;
    yaw = 0.f;
    orbit_seeded = false;
    orbit_angle = 15.85f;
    town_seeded = false;
    orbit_obj = 0;
    orbit_eye_off = 0;
    orbit_tgt_off = 0;
    orbit_have_tgt = false;
}

void FreeCam::OnCoreReconnect() {
    quiet_until = 1;
    in_battle = false;
    live_count = 0;
    primary_cam = 0;
    zone_prev = {};
    zone_seq = 0;
    pad_valid = false;
    pad_base = 0;
    pad_seq = 0;
    t8_fov_seeded = false;
    t8_last_scan_tick = 0;
    t8_seq = 0;
    t8_track_n = 0;
    dllfield_base = 0;
    dllfield_town_patched = false;
    t6_mode_unlock_count = 0;
    ResetYaw();
    LOG_INFO(Core, "Hoenn camera: core reconnect");
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

void FreeCam::OnModuleLoaded(std::string_view name, u32 load_address) {
    if (name == "DllBattle") {
        in_battle = true;
        live_count = 0;
        LOG_WARNING(Core, "Hoenn camera: battle enter — pause");
        return;
    }
    if (IsFieldMod(name)) {
        in_battle = false;
        quiet_until = 1;
        pitch = PITCH_BASE;
        ResetYaw();
        live_count = 0;
        primary_cam = 0;
        last_collect_tick = 0;
        dllfield_base = load_address;
        dllfield_town_patched = false;
        LOG_WARNING(Core, "Hoenn camera: DllField loaded @{:08X} — quiet (T6 base set)",
                    load_address);
    }
}

void FreeCam::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        in_battle = true;
        live_count = 0;
        dllfield_base = 0;
        dllfield_town_patched = false;
        ResetYaw();
        LOG_INFO(Core, "Hoenn camera: DllField unload");
    } else if (name == "DllBattle") {
        in_battle = false;
        quiet_until = 1;
        pitch = PITCH_BASE;
        ResetYaw();
        live_count = 0;
        last_collect_tick = 0;
        LOG_INFO(Core, "Hoenn camera: battle exit");
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
        live_count = 0;
        primary_cam = 0;
        last_collect_tick = 0;
        zone_prev = {};
        zone_seq = 0;
        pad_valid = false;
        pad_base = 0;
        pad_seq = 0;
        t8_fov_seeded = false;
        t8_last_scan_tick = 0;
        t8_seq = 0;
        t8_track_n = 0;
        town_shadow_base = 0;
        dllfield_town_patched = false;
        t6_mode_unlock_count = 0;
        if (experiment_mode < 0 || experiment_mode >= kExperimentCount) {
            experiment_mode = 0;
        }
        const_scan_done = false;
        LOG_WARNING(Core, "Hoenn free-look ON — exp={} {}", experiment_mode,
                    GetExperimentLabel(experiment_mode));
    } else {
        LOG_INFO(Core, "Hoenn free-look OFF");
    }
}

bool FreeCam::IsFreelookEnabled() const {
    return freelook;
}

void FreeCam::SetExperimentMode(int mode) {
    if (mode < 0 || mode >= kExperimentCount) {
        mode = 0;
    }
    if (experiment_mode == mode) {
        return;
    }
    experiment_mode = mode;
    dllfield_town_patched = false;
    const_scan_done = false;
    t6_mode_unlock_count = 0;
    town_shadow_base = 0;
    orbit_obj = 0;
    mat_yaw = 0.f;
    LOG_WARNING(Core, "Hoenn freelook EXP={} {}", experiment_mode,
                GetExperimentLabel(experiment_mode));
}

int FreeCam::GetExperimentMode() const {
    return experiment_mode;
}

int FreeCam::GetExperimentCount() const {
    return kExperimentCount;
}

u64 FreeCam::GetScheduleInterval() const {
    // Default freecam ~15Hz; HighRateStack ~60Hz (still floored in CheatEngine)
    constexpr u64 kNormal = 16'000'000;
    constexpr u64 kFast = 4'000'000;
    if (!freelook && !zoom_assist) {
        return 50'000'000;
    }
    const ExpFlags f = FlagsFor(static_cast<Experiment>(experiment_mode));
    return f.high_rate ? kFast : kNormal;
}

std::string FreeCam::GetExperimentLabel(int mode) const {
    if (mode < 0 || mode >= kExperimentCount) {
        return "?(bad)";
    }
    static const char* kLabels[kExperimentCount] = {
        "0 Stable - GOLD dual eulers (house OK)",
        "1 Const-scan - patch 000D0001 in process image",
        "2 Matrix band - stick Ry into cam matrix slots",
        "3 Matrix twin - same on FOV-twin only",
        "4 Orbit world - XZ rotate eye/target triples",
        "5 Const-scan + mode freeze + gold",
        "6 Matrix + mode freeze",
        "7 High-rate stack - 60Hz + const + matrix + gold",
    };
    return std::string(kLabels[mode]);
}

FreeCam::ExpFlags FreeCam::FlagsFor(Experiment e) const {
    ExpFlags f;
    switch (e) {
    case Experiment::Stable:
        break;
    case Experiment::ConstScan:
        f.const_scan = true;
        break;
    case Experiment::MatrixBand:
        f.matrix_band = true;
        break;
    case Experiment::MatrixTwin:
        f.matrix_band = true;
        f.matrix_twin_only = true;
        break;
    case Experiment::OrbitWorld:
        f.orbit_world = true;
        break;
    case Experiment::ConstModeGold:
        f.const_scan = true;
        f.mode_unlock = true;
        break;
    case Experiment::MatrixMode:
        f.matrix_band = true;
        f.mode_unlock = true;
        break;
    case Experiment::HighRateStack:
        f.high_rate = true;
        f.const_scan = true;
        f.matrix_band = true;
        f.mode_unlock = true;
        break;
    }
    return f;
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

bool FreeCam::IsGoldLive(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const {
    if (!HeapPtr(base) || base + OFF_FOV + 4 >= 0x0C000000) {
        return false;
    }
    const u32 flag = mem.Read32(process, base + OFF_FLAG);
    const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
    return flag == LIVE_FLAG && OkGoldFov(fov);
}

void FreeCam::CollectLiveTargets(Memory::MemorySystem& mem, Kernel::Process& process,
                                 u32 slot_cam, bool collect_echoes) {
    live_count = 0;
    echo_count = 0;
    primary_cam = 0;
    std::unordered_set<u32> seen;

    struct Hit {
        u32 base;
        float score;
    };
    std::vector<Hit> hits;

    auto consider = [&](u32 base) {
        if (!IsGoldLive(mem, process, base) || seen.count(base)) {
            return;
        }
        seen.insert(base);
        const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
        float s = std::fabs(fov - 225.f);
        if (base == slot_cam) {
            s -= 100.f;
        }
        if (HeapPtr(slot_cam)) {
            const u32 dist = base > slot_cam ? base - slot_cam : slot_cam - base;
            s += static_cast<float>(dist) / 4096.f;
        }
        hits.push_back({base, s});
    };

    // Cheap first: slot + last known + BSS pointers (no heap walk)
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
    // Tight scans only on hot path (v7: multi-MB scans froze floor transitions)
    scan_near(slot_cam, SCAN_RADIUS_HOT);
    if (last_good_cam != slot_cam) {
        scan_near(last_good_cam, SCAN_RADIUS_HOT);
    }
    // Known house / stereo bands from RE (small windows, not 1.5 MiB each)
    scan_near(0x082D3000, 0x8000);
    scan_near(0x08188200, 0x1000);

    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b) { return a.score < b.score; });

    for (const auto& h : hits) {
        if (live_count >= kMaxLiveTargets) {
            break;
        }
        live_targets[static_cast<size_t>(live_count++)] = h.base;
    }

    // Prefer GOLD slot, then FREE-mode gold, then best hit
    primary_cam = 0;
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

    // SHADOW cams: DumpRE only. Writing them is banned (v5 poison); scanning
    // them every Tick was the floor-transition freeze (millions of Read32s).
    if (!collect_echoes) {
        return;
    }

    struct EchoHit {
        u32 base;
        float score;
    };
    std::vector<EchoHit> echoes;
    std::unordered_set<u32> echo_seen;
    const float pri_fov =
        primary_cam ? BFloat(mem.Read32(process, primary_cam + OFF_FOV)) : 225.f;
    const u32 echo_center =
        HeapPtr(slot_cam) ? slot_cam : (primary_cam ? primary_cam : last_good_cam);

    auto is_camera_ish_shadow = [&](u32 base, float fov) -> bool {
        // Reject FOV=200 fixed-band junk (echo-v4 false positives: +A4=-100, +B8=120)
        if (std::fabs(fov - 200.f) < 0.5f && std::fabs(pri_fov - 200.f) > 5.f) {
            return false;
        }
        const float a4 = BFloat(mem.Read32(process, base + 0xA4));
        const float b8 = BFloat(mem.Read32(process, base + 0xB8));
        const float fov_alt = BFloat(mem.Read32(process, base + OFF_FOV_ALT));
        // Gold layout constants: +A4≈32, +B8≈5.1, +6C≈FOV — accept if any match
        if (OkFloat(a4) && std::fabs(a4 - 32.f) < 1.f) {
            return true;
        }
        if (OkFloat(b8) && std::fabs(b8 - 5.1f) < 0.5f) {
            return true;
        }
        if (OkFloat(fov_alt) && std::fabs(fov_alt - fov) < 5.f) {
            return true;
        }
        // Soft accept: pitch layout + FOV match only (shadows may omit dual)
        const float p = BFloat(mem.Read32(process, base + OFF_PITCH));
        return OkPitchLayout(p) || std::fabs(p) < 0.01f;
    };

    auto consider_echo = [&](u32 base) {
        if (!HeapPtr(base) || base + OFF_FOV + 4 >= 0x0C000000 || echo_seen.count(base) ||
            seen.count(base)) {
            return;
        }
        const u32 flag = mem.Read32(process, base + OFF_FLAG);
        if (flag != 0) {
            return; // only proven flag=0 shadows from WIDE; other flags = prior chaos
        }
        const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
        if (!OkGoldFov(fov) || std::fabs(fov - pri_fov) > ECHO_FOV_MATCH) {
            return; // must match primary FOV — kills FOV=200 distance winners
        }
        if (!is_camera_ish_shadow(base, fov)) {
            return;
        }
        echo_seen.insert(base);
        // Score: FOV match first, tiny distance tie-break (not dominant)
        float s = std::fabs(fov - pri_fov) * 100.f;
        if (HeapPtr(echo_center)) {
            const u32 dist = base > echo_center ? base - echo_center : echo_center - base;
            s += static_cast<float>(dist) / 65536.f;
        }
        echoes.push_back({base, s});
    };

    auto scan_echo = [&](u32 center) {
        if (!HeapPtr(center)) {
            return;
        }
        const u32 lo = center > ECHO_RADIUS ? center - ECHO_RADIUS : 0x08000000;
        const u32 hi = std::min(center + ECHO_RADIUS, 0x0BFFFFFFu);
        for (u32 base = lo; base + OFF_FOV + 4 < hi; base += SCAN_STEP) {
            consider_echo(base);
        }
    };
    scan_echo(echo_center);
    if (primary_cam && primary_cam != echo_center) {
        scan_echo(primary_cam);
    }
    // Known FOV-matched shadow band (08285648 family from 4-dump WIDE)
    scan_echo(0x08285000);
    scan_echo(0x08284000);

    std::sort(echoes.begin(), echoes.end(),
              [](const EchoHit& a, const EchoHit& b) { return a.score < b.score; });
    for (const auto& e : echoes) {
        if (echo_count >= kMaxEchoTargets) {
            break;
        }
        echo_targets[static_cast<size_t>(echo_count++)] = e.base;
    }
}

void FreeCam::LogWideScan(Memory::MemorySystem& mem, Kernel::Process& process,
                          u32 slot_cam) const {
    // Read-only RE: list FOV candidates near slot (do NOT write these)
    if (!HeapPtr(slot_cam)) {
        return;
    }
    const float pri_fov =
        primary_cam ? BFloat(mem.Read32(process, primary_cam + OFF_FOV)) : 225.f;
    struct W {
        u32 base;
        float fov;
        float pitch;
        float yaw;
        u32 flag;
        float score;
    };
    std::vector<W> wide;
    const u32 lo = slot_cam > SCAN_RADIUS_WIDE ? slot_cam - SCAN_RADIUS_WIDE : 0x08000000;
    const u32 hi = std::min(slot_cam + SCAN_RADIUS_WIDE, 0x0BFFFFFFu);
    for (u32 base = lo; base + OFF_FOV + 4 < hi; base += SCAN_STEP) {
        const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
        if (!std::isfinite(fov) || fov < 100.f || fov > 500.f) {
            continue;
        }
        const float pitch_v = BFloat(mem.Read32(process, base + OFF_PITCH));
        const u32 fl = mem.Read32(process, base + OFF_FLAG);
        if (fl != LIVE_FLAG && !OkPitchLayout(pitch_v) && std::fabs(pitch_v) > 0.01f) {
            continue;
        }
        // Prefer FOV near primary (deep RE: real shadows match pri FOV, not 200 junk)
        float s = std::fabs(fov - pri_fov);
        if (fl == LIVE_FLAG) {
            s -= 50.f;
        } else if (fl == 0 && s <= ECHO_FOV_MATCH) {
            s -= 20.f; // flag=0 FOV-matched shadow class
        }
        wide.push_back({base, fov, pitch_v, BFloat(mem.Read32(process, base + OFF_YAW)), fl, s});
    }
    std::sort(wide.begin(), wide.end(),
              [](const W& a, const W& b) { return a.score < b.score; });
    const int n = std::min(static_cast<int>(wide.size()), 20);
    LOG_WARNING(Core, "Hoenn RE WIDE (read-only) near {:08X} pri_fov={:.1f}: {}/{}", slot_cam,
                pri_fov, n, wide.size());
    for (int i = 0; i < n; ++i) {
        const auto& w = wide[static_cast<size_t>(i)];
        const char* tag = w.flag == LIVE_FLAG                      ? "GOLD"
                          : (w.flag == 0 && std::fabs(w.fov - pri_fov) <= ECHO_FOV_MATCH)
                              ? "SHADOW?"
                              : "other";
        LOG_WARNING(Core, "Hoenn RE WIDE[{}] {:08X} fov={:.1f} p={:.2f} y={:.2f} fl={:08X} {}", i,
                    w.base, w.fov, w.pitch, w.yaw, w.flag, tag);
    }
    // Deep RE: full dual-block dump of top FOV-matched flag=0 shadows (read-only)
    int dumped = 0;
    for (const auto& w : wide) {
        if (dumped >= 2) {
            break;
        }
        if (w.flag != 0 || std::fabs(w.fov - pri_fov) > ECHO_FOV_MATCH) {
            continue;
        }
        LOG_WARNING(Core, "Hoenn RE SHADOW-FULL base={:08X} (FOV-matched flag=0)", w.base);
        for (u32 off = 0x40; off <= 0xB8; off += 4) {
            const u32 raw = mem.Read32(process, w.base + off);
            LOG_WARNING(Core, "Hoenn RE SHADOW +{:02X}: raw={:08X} f={:.6g}", off, raw,
                        BFloat(raw));
        }
        dumped++;
    }
}

void FreeCam::SeedAnglesFromCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam) {
    if (!HeapPtr(cam)) {
        pitch = PITCH_BASE;
        yaw = 0.f;
        yaw_seeded = true;
        return;
    }
    const float p = BFloat(mem.Read32(process, cam + OFF_PITCH));
    const float y = BFloat(mem.Read32(process, cam + OFF_YAW));
    if (OkPitchLayout(p)) {
        pitch = std::clamp(p, PITCH_MIN, PITCH_MAX);
    } else {
        pitch = PITCH_BASE;
    }
    yaw = OkFloat(y) ? y : 0.f;
    yaw_seeded = true;
}

u32 FreeCam::ResolveTownShadowBase(Memory::MemorySystem& mem, Kernel::Process& process,
                                   u32 gold) const {
    // T8 hit is the FOV float address. Infer object base (+B0 preferred).
    // Strict: flag must be 0 (reject 08289140-class junk with wild flags).
    if (!HeapPtr(gold)) {
        return 0;
    }
    const float g6 = BFloat(mem.Read32(process, gold + OFF_FOV_ALT));
    const float gb = BFloat(mem.Read32(process, gold + OFF_FOV));
    const float gref = OkFloat(g6) ? g6 : gb;
    if (!OkFloat(gref) || gref < 100.f || gref > 500.f) {
        return 0;
    }

    auto try_fov_addr = [&](u32 fov_addr) -> u32 {
        if (!HeapPtr(fov_addr)) {
            return 0;
        }
        const float f = BFloat(mem.Read32(process, fov_addr));
        if (!OkFloat(f) || std::fabs(f - gref) > T8_FOV_MATCH) {
            return 0;
        }
        // Prefer +B0 layout (T8 dogfood: 08286380 − B0 = 082862D0)
        for (u32 fov_off : {OFF_FOV, OFF_FOV_ALT}) {
            if (fov_addr < fov_off) {
                continue;
            }
            const u32 base = fov_addr - fov_off;
            if (!HeapPtr(base) || base == gold || base + OFF_FOV + 4 >= 0x0C000000) {
                continue;
            }
            const u32 fl = mem.Read32(process, base + OFF_FLAG);
            if (fl != 0) {
                continue; // only proven flag=0 twin
            }
            const float check = BFloat(mem.Read32(process, base + fov_off));
            if (std::fabs(check - gref) > T8_FOV_MATCH) {
                continue;
            }
            return base;
        }
        return 0;
    };

    // Prefer sticky last base if still valid
    if (HeapPtr(town_shadow_base) && town_shadow_base != gold) {
        const u32 fl = mem.Read32(process, town_shadow_base + OFF_FLAG);
        const float sf = BFloat(mem.Read32(process, town_shadow_base + OFF_FOV));
        const float sf6 = BFloat(mem.Read32(process, town_shadow_base + OFF_FOV_ALT));
        if (fl == 0 && (std::fabs(sf - gref) <= 5.f || std::fabs(sf6 - gref) <= 5.f)) {
            return town_shadow_base;
        }
    }

    for (int i = 0; i < t8_track_n; ++i) {
        if (u32 b = try_fov_addr(t8_track[static_cast<size_t>(i)])) {
            return b;
        }
    }
    // Bootstrap: known FOV-twin band from T8 (08286380 family)
    for (u32 a = 0x08285000; a < 0x08288000; a += 0x10) {
        if (u32 b = try_fov_addr(a)) {
            return b;
        }
    }
    return 0;
}

void FreeCam::TryPatchDllFieldTownConstant(Memory::MemorySystem& mem, Kernel::Process& process) {
    // T6-B: only rewrite the single aligned 000D0001 in DllField mode table.
    // CRO lives in process image (not heap 0x08xxxxxx) — do not use HeapPtr.
    if (dllfield_town_patched || dllfield_base == 0) {
        return;
    }
    const u32 addr = dllfield_base + DLLFIELD_TOWN_MODE_OFF;
    const u32 cur = mem.Read32(process, addr);
    if (cur == MODE_TOWN) {
        WriteU32(mem, process, addr, MODE_FREELOOK);
        const u32 rb = mem.Read32(process, addr);
        LOG_WARNING(Core,
                    "Hoenn T6 CRO-PATCH @{:08X} (file+{:X}) {:08X}→{:08X} rb={:08X}", addr,
                    DLLFIELD_TOWN_MODE_OFF, MODE_TOWN, MODE_FREELOOK, rb);
        dllfield_town_patched = (rb == MODE_FREELOOK);
    } else if (cur == MODE_FREELOOK) {
        dllfield_town_patched = true;
        LOG_INFO(Core, "Hoenn T6 CRO already FREE @{:08X}", addr);
    } else {
        // Wrong mapping / relocated — log once; still try memory mode unlock each tick
        LOG_WARNING(Core, "Hoenn T6 CRO-PATCH skip @{:08X} unexpected raw={:08X} (use live mode unlock)",
                    addr, cur);
        dllfield_town_patched = true; // don't spam
    }
}

void FreeCam::WriteTownModeUnlock(Memory::MemorySystem& mem, Kernel::Process& process) {
    // Dual mode TOWN→FREE on LIVE golds only. Never touch mode==0 stereo (v5).
    for (int i = 0; i < live_count; ++i) {
        const u32 cam = live_targets[static_cast<size_t>(i)];
        if (!IsGoldLive(mem, process, cam)) {
            continue;
        }
        const u32 m8 = mem.Read32(process, cam + OFF_MODE);
        const u32 m48 = mem.Read32(process, cam + OFF_MODE_ALT);
        if (m8 != MODE_TOWN && m48 != MODE_TOWN) {
            continue;
        }
        if (m8 == 0 || m48 == 0) {
            continue;
        }
        WriteU32(mem, process, cam + OFF_MODE, MODE_FREELOOK);
        WriteU32(mem, process, cam + OFF_MODE_ALT, MODE_FREELOOK);
        t6_mode_unlock_count++;
        if (t6_mode_unlock_count <= 3 || (t6_mode_unlock_count % 60) == 0) {
            LOG_WARNING(Core,
                        "Hoenn EXP mode-unlock cam={:08X} was {:08X}/{:08X} → FREE (n={})", cam,
                        m48, m8, t6_mode_unlock_count);
        }
    }
}

void FreeCam::WriteEulerToCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam,
                              const ExpFlags& f) {
    if (f.pitch) {
        WriteF(mem, process, cam + OFF_PITCH, pitch);
        WriteF(mem, process, cam + OFF_PITCH_ALT, pitch);
    }
    if (f.yaw && OkFloat(yaw)) {
        WriteF(mem, process, cam + OFF_YAW, yaw);
        WriteF(mem, process, cam + OFF_YAW_ALT, yaw);
    }
}

void FreeCam::ScanAndPatchTownConsts(Memory::MemorySystem& mem, Kernel::Process& process) {
    // Find mode-table rows: 00000002, 00000008, 000D0001 (DllField layout) OR bare 000D0001.
    // Process image scan (not heap). Cap work to avoid freeze.
    if (const_scan_done) {
        return;
    }
    int patched = 0;
    // Prefer known CRO base if we have it
    if (dllfield_base != 0) {
        const u32 addr = dllfield_base + DLLFIELD_TOWN_MODE_OFF;
        const u32 cur = mem.Read32(process, addr);
        if (cur == MODE_TOWN) {
            WriteU32(mem, process, addr, MODE_FREELOOK);
            if (mem.Read32(process, addr) == MODE_FREELOOK) {
                patched++;
                LOG_WARNING(Core, "Hoenn EXP const-scan CRO @{:08X} TOWN->FREE", addr);
            }
        }
    }
    // Signature scan in common CRO load band
    constexpr u32 kLo = 0x00100000;
    constexpr u32 kHi = 0x00400000;
    constexpr u32 kStep = 4;
    for (u32 a = kLo; a + 12 < kHi && patched < 8; a += kStep) {
        const u32 w = mem.Read32(process, a);
        if (w != MODE_TOWN) {
            continue;
        }
        // Prefer table row: prev words look like 2, 8
        if (a >= 8) {
            const u32 p0 = mem.Read32(process, a - 8);
            const u32 p1 = mem.Read32(process, a - 4);
            if (p0 == 2 && p1 == 8) {
                WriteU32(mem, process, a, MODE_FREELOOK);
                if (mem.Read32(process, a) == MODE_FREELOOK) {
                    patched++;
                    LOG_WARNING(Core, "Hoenn EXP const-scan table @{:08X} TOWN->FREE", a);
                }
                continue;
            }
        }
        // Standalone 000D0001 (first few only — avoid thrashing every int)
        if (patched < 2) {
            WriteU32(mem, process, a, MODE_FREELOOK);
            if (mem.Read32(process, a) == MODE_FREELOOK) {
                patched++;
                LOG_WARNING(Core, "Hoenn EXP const-scan bare @{:08X} TOWN->FREE", a);
            }
        }
    }
    const_scan_done = true;
    LOG_WARNING(Core, "Hoenn EXP const-scan done patched={} dll={:08X}", patched, dllfield_base);
}

void FreeCam::WriteMatrixBand(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam,
                              bool twin_only) {
    // Stick-driven yaw rotation matrix written into candidate bands (view/world mtx guess).
    // NOT dual eulers — untried path for town follow-cam.
    u32 bases[2] = {0, 0};
    if (twin_only) {
        bases[0] = ResolveTownShadowBase(mem, process, cam);
        if (!bases[0]) {
            if ((diag % 90) == 0) {
                LOG_WARNING(Core, "Hoenn EXP matrix-twin: no twin");
            }
            return;
        }
        town_shadow_base = bases[0];
    } else {
        bases[0] = cam;
        bases[1] = ResolveTownShadowBase(mem, process, cam);
        if (bases[1]) {
            town_shadow_base = bases[1];
        }
    }
    // Accumulate yaw from stick
    mat_yaw += stick_sx * (invert_x ? -1.f : 1.f) * sensitivity * 0.06f;
    const float c = std::cos(mat_yaw);
    const float s = std::sin(mat_yaw);
    // Row-major Ry (common) + identity translation
    const float rows[12] = {c, 0.f, s, 0.f, 0.f, 1.f, 0.f, 0.f, -s, 0.f, c, 0.f};
    // Candidate starts: before dual, after dual (research gap +0xB4+)
    static constexpr u32 kOffs[] = {0x00, 0x10, 0xC0, 0xE0, 0x100, 0x120, 0x140, 0x160};
    for (u32 base : bases) {
        if (!base || !HeapPtr(base)) {
            continue;
        }
        for (u32 off : kOffs) {
            for (int i = 0; i < 12; ++i) {
                WriteF(mem, process, base + off + static_cast<u32>(i * 4), rows[i]);
            }
        }
    }
    if ((diag % 60) == 0) {
        LOG_INFO(Core, "Hoenn EXP matrix write yaw={:.2f} twin_only={} bases={:08X}/{:08X}",
                 mat_yaw, twin_only ? 1 : 0, bases[0], bases[1]);
    }
}

bool FreeCam::LooksWorldPos(float x, float y, float z) const {
    if (!OkFloat(x) || !OkFloat(y) || !OkFloat(z)) {
        return false;
    }
    const float ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
    const float m = std::max(ax, std::max(ay, az));
    if (m < 20.f || m > 1.0e6f) {
        return false;
    }
    // Reject dual-block constants
    if (std::fabs(ax - 32.f) < 0.1f && std::fabs(az - 30.f) < 0.1f) {
        return false;
    }
    if (std::fabs(ay - 1.f) < 0.01f && m < 5.f) {
        return false;
    }
    return true;
}

void FreeCam::WriteOrbitWorld(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam,
                              float sx, float sy) {
    // Orbit eye XZ around target (or mid) by stick X; stick Y elevates eye Y.
    if (!HeapPtr(cam)) {
        return;
    }
    u32 objs[2] = {cam, 0};
    const u32 twin = ResolveTownShadowBase(mem, process, cam);
    if (twin) {
        objs[1] = twin;
        town_shadow_base = twin;
    }

    auto read3 = [&](u32 base, u32 off, float& x, float& y, float& z) {
        x = BFloat(mem.Read32(process, base + off));
        y = BFloat(mem.Read32(process, base + off + 4));
        z = BFloat(mem.Read32(process, base + off + 8));
    };
    auto write3 = [&](u32 base, u32 off, float x, float y, float z) {
        WriteF(mem, process, base + off, x);
        WriteF(mem, process, base + off + 4, y);
        WriteF(mem, process, base + off + 8, z);
    };

    // Discover once per object
    if (!orbit_obj || (orbit_obj != cam && orbit_obj != twin)) {
        orbit_obj = 0;
        for (u32 obj : objs) {
            if (!obj) {
                continue;
            }
            for (u32 off = 0; off + 12 <= 0x1C0; off += 4) {
                // skip dual freelook band known floats
                if (off >= 0x40 && off <= 0xB8) {
                    continue;
                }
                float x, y, z;
                read3(obj, off, x, y, z);
                if (!LooksWorldPos(x, y, z)) {
                    continue;
                }
                orbit_obj = obj;
                orbit_eye_off = off;
                orbit_have_tgt = false;
                // pair target further along
                for (u32 t = off + 12; t + 12 <= 0x1C0; t += 4) {
                    if (t >= 0x40 && t <= 0xB8) {
                        continue;
                    }
                    float tx, ty, tz;
                    read3(obj, t, tx, ty, tz);
                    if (LooksWorldPos(tx, ty, tz)) {
                        orbit_tgt_off = t;
                        orbit_have_tgt = true;
                        break;
                    }
                }
                LOG_WARNING(Core,
                            "Hoenn EXP orbit obj={:08X} eye=+{:03X} tgt=+{:03X} have_t={}", obj,
                            orbit_eye_off, orbit_tgt_off, orbit_have_tgt ? 1 : 0);
                break;
            }
            if (orbit_obj) {
                break;
            }
        }
    }
    if (!orbit_obj) {
        if ((diag % 90) == 0) {
            LOG_WARNING(Core, "Hoenn EXP orbit: no world-pos triple on gold/twin");
        }
        return;
    }

    float ex, ey, ez;
    read3(orbit_obj, orbit_eye_off, ex, ey, ez);
    float cx = ex, cy = ey, cz = ez;
    if (orbit_have_tgt) {
        float tx, ty, tz;
        read3(orbit_obj, orbit_tgt_off, tx, ty, tz);
        cx = (ex + tx) * 0.5f;
        cy = (ey + ty) * 0.5f;
        cz = (ez + tz) * 0.5f;
    }
    const float dyaw = sx * sensitivity * ORBIT_YAW_STEP;
    if (std::fabs(dyaw) > 1e-6f) {
        const float c = std::cos(dyaw);
        const float s = std::sin(dyaw);
        const float dx = ex - cx;
        const float dz = ez - cz;
        ex = cx + dx * c - dz * s;
        ez = cz + dx * s + dz * c;
    }
    // sy already signed elevation rate from Tick
    if (std::fabs(sy) >= STICK_DEADZONE) {
        ey += sy * sensitivity * 8.f;
    }
    write3(orbit_obj, orbit_eye_off, ex, ey, ez);
}

void FreeCam::WriteFreelookToAllLive(Memory::MemorySystem& mem, Kernel::Process& process) {
    const ExpFlags f = FlagsFor(static_cast<Experiment>(experiment_mode));
    const bool stick_active =
        std::fabs(stick_sx) >= STICK_DEADZONE || std::fabs(stick_sy) >= STICK_DEADZONE;

    if (f.const_scan) {
        ScanAndPatchTownConsts(mem, process);
    }
    if (f.mode_unlock) {
        WriteTownModeUnlock(mem, process);
    }

    for (int i = 0; i < live_count; ++i) {
        const u32 cam = live_targets[static_cast<size_t>(i)];
        if (!IsGoldLive(mem, process, cam)) {
            continue;
        }
        if (f.gold_euler) {
            WriteEulerToCam(mem, process, cam, f);
        }
        if (f.matrix_band && stick_active && (cam == primary_cam || primary_cam == 0)) {
            WriteMatrixBand(mem, process, cam, f.matrix_twin_only);
        }
        if (f.orbit_world && stick_active && (cam == primary_cam || primary_cam == 0)) {
            const float x = invert_x ? -stick_sx : stick_sx;
            const float y = invert_y ? stick_sy : -stick_sy;
            WriteOrbitWorld(mem, process, cam, x, y);
        }
    }
}

int FreeCam::ScanCamCandidates(Core::System& system) {
    candidates.clear();
    if (!system.IsPoweredOn()) {
        return 0;
    }
    auto process = Resolve(system, last_process_id);
    if (!process) {
        return 0;
    }
    auto& mem = system.Memory();
    const u32 slot_cam = mem.Read32(*process, CAMERA_SLOT);
    CollectLiveTargets(mem, *process, slot_cam);
    if (primary_cam) {
        SeedAnglesFromCam(mem, *process, primary_cam);
    }

    CamCandidate c0;
    c0.base = slot_cam;
    c0.is_slot = true;
    c0.live = IsGoldLive(mem, *process, slot_cam);
    if (HeapPtr(slot_cam)) {
        c0.fov = BFloat(mem.Read32(*process, slot_cam + OFF_FOV));
        c0.pitch = BFloat(mem.Read32(*process, slot_cam + OFF_PITCH));
        c0.yaw = BFloat(mem.Read32(*process, slot_cam + OFF_YAW));
        c0.flag80 = mem.Read32(*process, slot_cam + OFF_FLAG);
    }
    candidates.push_back(c0);

    for (int i = 0; i < live_count; ++i) {
        const u32 b = live_targets[static_cast<size_t>(i)];
        if (b == slot_cam) {
            continue;
        }
        CamCandidate c;
        c.base = b;
        c.live = true;
        c.fov = BFloat(mem.Read32(*process, b + OFF_FOV));
        c.pitch = BFloat(mem.Read32(*process, b + OFF_PITCH));
        c.yaw = BFloat(mem.Read32(*process, b + OFF_YAW));
        c.flag80 = mem.Read32(*process, b + OFF_FLAG);
        candidates.push_back(c);
    }
    LOG_WARNING(Core, "Hoenn camProbe: gold_n={} pri={:08X}", live_count, primary_cam);
    return static_cast<int>(candidates.size());
}

int FreeCam::GetCamCandidateCount() const {
    return static_cast<int>(candidates.size());
}

std::string FreeCam::GetCamCandidateLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(candidates.size())) {
        return "?(empty)";
    }
    const auto& c = candidates[static_cast<size_t>(index)];
    char buf[160];
    if (c.is_slot) {
        std::snprintf(buf, sizeof(buf), "#%d SLOT %08X %s fov=%.0f fl=%X n=%d", index, c.base,
                      c.live ? "GOLD" : "DEAD", c.fov, c.flag80, live_count);
    } else {
        std::snprintf(buf, sizeof(buf), "#%d %08X GOLD fov=%.0f fl=%X", index, c.base, c.fov,
                      c.flag80);
    }
    return std::string(buf);
}

void FreeCam::SetCamProbeIndex(int index) {
    probe_index = std::max(0, index);
    if (probe_index > 0 && probe_index < static_cast<int>(candidates.size())) {
        const auto& c = candidates[static_cast<size_t>(probe_index)];
        if (c.live) {
            primary_cam = c.base;
            pitch = PITCH_BASE;
            yaw = c.yaw;
            yaw_seeded = true;
        }
    }
}

int FreeCam::GetCamProbeIndex() const {
    return probe_index;
}

u32 FreeCam::GetActiveCamBase() const {
    return primary_cam ? primary_cam : last_good_cam;
}

std::string FreeCam::DumpREState(Core::System& system, const char* tag) {
    const char* t = tag && tag[0] ? tag : "manual";
    if (!system.IsPoweredOn()) {
        return "not powered on";
    }
    auto process = Resolve(system, last_process_id);
    if (!process) {
        return "no process";
    }
    auto& mem = system.Memory();
    const u32 slot_cam = mem.Read32(*process, CAMERA_SLOT);
    CollectLiveTargets(mem, *process, slot_cam, /*collect_echoes=*/true);

    LOG_WARNING(Core,
                "========== Hoenn RE DUMP [{}] slot={:08X} gold_n={} echo_n={} pri={:08X} ==========",
                t, slot_cam, live_count, echo_count, primary_cam);
    for (int i = 0; i < live_count; ++i) {
        const u32 b = live_targets[static_cast<size_t>(i)];
        const u32 mode = mem.Read32(*process, b + OFF_MODE);
        LOG_WARNING(Core, "Hoenn RE GOLD[{}] {:08X} fov={:.0f} fl={:X} mode={:08X} p={:.2f} y={:.2f} {}",
                    i, b, BFloat(mem.Read32(*process, b + OFF_FOV)),
                    mem.Read32(*process, b + OFF_FLAG), mode,
                    BFloat(mem.Read32(*process, b + OFF_PITCH)),
                    BFloat(mem.Read32(*process, b + OFF_YAW)),
                    mode == MODE_FREELOOK ? "FREE" : "locked?");
    }
    for (int i = 0; i < echo_count; ++i) {
        const u32 b = echo_targets[static_cast<size_t>(i)];
        LOG_WARNING(Core, "Hoenn RE ECHO[{}] {:08X} fov={:.0f} fl={:X} p={:.2f} y={:.2f}", i, b,
                    BFloat(mem.Read32(*process, b + OFF_FOV)), mem.Read32(*process, b + OFF_FLAG),
                    BFloat(mem.Read32(*process, b + OFF_PITCH)),
                    BFloat(mem.Read32(*process, b + OFF_YAW)));
    }
    LogWideScan(mem, *process, slot_cam);

    // Dump PRIMARY object (not only slot) — dead-slot cases need this for RE
    auto dump_obj = [&](u32 base, const char* label) {
        if (!HeapPtr(base)) {
            LOG_WARNING(Core, "Hoenn RE OBJ {} invalid", label);
            return;
        }
        LOG_WARNING(Core, "Hoenn RE OBJ {} base={:08X}", label, base);
        for (u32 off = 0x40; off <= 0xB8; off += 4) {
            const u32 raw = mem.Read32(*process, base + off);
            LOG_WARNING(Core, "Hoenn RE {} +{:02X}: raw={:08X} f={:.6g}", label, off, raw,
                        BFloat(raw));
        }
    };
    dump_obj(primary_cam ? primary_cam : slot_cam, "PRI");
    if (HeapPtr(slot_cam) && slot_cam != primary_cam) {
        dump_obj(slot_cam, "SLOT");
    }
    for (int i = 0; i < echo_count; ++i) {
        char lab[16];
        std::snprintf(lab, sizeof(lab), "ECHO%d", i);
        dump_obj(echo_targets[static_cast<size_t>(i)], lab);
    }

    u32 words[kDumpWords];
    const u32 diff_base = primary_cam ? primary_cam : slot_cam;
    if (HeapPtr(diff_base)) {
        for (int i = 0; i < kDumpWords; ++i) {
            words[i] = mem.Read32(*process, diff_base + static_cast<u32>(i * 4));
        }
        int n_diff = 0;
        if (dump_prev_valid && dump_prev_base == diff_base) {
            for (int i = 0; i < kDumpWords; ++i) {
                if (words[i] != dump_prev_words[i]) {
                    n_diff++;
                }
            }
            LOG_WARNING(Core, "Hoenn RE DIFF pri: {} words", n_diff);
        }
        dump_prev_base = diff_base;
        std::memcpy(dump_prev_words, words, sizeof(words));
        dump_prev_valid = true;
    }
    std::snprintf(dump_prev_tag, sizeof(dump_prev_tag), "%s", t);

    char toast[112];
    std::snprintf(toast, sizeof(toast), "RE g=%d sh=%d pri=%08X slot=%s", live_count, echo_count,
                  primary_cam, IsGoldLive(mem, *process, slot_cam) ? "GOLD" : "dead");
    return std::string(toast);
}

void FreeCam::WatchZoneShift(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                             float stick_x, float stick_y) {
    // Read-only. Detect area cam script / follow-cam flips (town freelook RE).
    // Prefer primary GOLD; fall back to slot if live.
    u32 cam = 0;
    if (primary_cam && IsGoldLive(mem, process, primary_cam)) {
        cam = primary_cam;
    } else if (IsGoldLive(mem, process, slot_cam)) {
        cam = slot_cam;
    } else if (HeapPtr(slot_cam) && slot_cam + OFF_FOV + 4 < 0x0C000000) {
        cam = slot_cam; // may be DEAD mid-hop; still useful for mode dump
    } else {
        return;
    }

    ZoneSnap cur;
    cur.slot = slot_cam;
    cur.pri = primary_cam;
    cur.mode48 = mem.Read32(process, cam + OFF_MODE_ALT);
    cur.mode8c = mem.Read32(process, cam + OFF_MODE);
    cur.flag = mem.Read32(process, cam + OFF_FLAG);
    cur.pitch = BFloat(mem.Read32(process, cam + OFF_PITCH));
    cur.yaw = BFloat(mem.Read32(process, cam + OFF_YAW));
    cur.fov = BFloat(mem.Read32(process, cam + OFF_FOV));
    cur.fov_alt = BFloat(mem.Read32(process, cam + OFF_FOV_ALT));
    cur.dist = BFloat(mem.Read32(process, cam + OFF_DIST));
    cur.dist_alt = BFloat(mem.Read32(process, cam + OFF_DIST_ALT));
    cur.angle50 = BFloat(mem.Read32(process, cam + OFF_ANGLE_ALT));
    cur.angle94 = BFloat(mem.Read32(process, cam + OFF_ANGLE));
    cur.valid = true;

    if (!zone_prev.valid) {
        zone_prev = cur;
        LOG_WARNING(Core,
                    "Hoenn ZONE-BASE #{} cam={:08X} slot={:08X} pri={:08X} mode48={:08X} "
                    "mode8C={:08X} fl={:08X} p={:.2f} y={:.2f} fov={:.1f}/{:.1f} dist={:.0f}/{:.0f} "
                    "ang={:.2f}/{:.2f}",
                    zone_seq, cam, cur.slot, cur.pri, cur.mode48, cur.mode8c, cur.flag, cur.pitch,
                    cur.yaw, cur.fov_alt, cur.fov, cur.dist_alt, cur.dist, cur.angle50, cur.angle94);
        return;
    }

    const bool stick_active =
        std::fabs(stick_x) >= STICK_DEADZONE || std::fabs(stick_y) >= STICK_DEADZONE;

    char reasons[160]{};
    auto add = [&](const char* r) {
        if (reasons[0]) {
            std::strncat(reasons, "+", sizeof(reasons) - std::strlen(reasons) - 1);
        }
        std::strncat(reasons, r, sizeof(reasons) - std::strlen(reasons) - 1);
    };

    if (cur.slot != zone_prev.slot) {
        add("slot");
    }
    if (cur.pri != zone_prev.pri) {
        add("pri");
    }
    if (cur.mode48 != zone_prev.mode48 || cur.mode8c != zone_prev.mode8c) {
        add("mode");
    }
    if (cur.flag != zone_prev.flag) {
        add("flag");
    }
    if (std::fabs(cur.fov - zone_prev.fov) > 1.5f ||
        std::fabs(cur.fov_alt - zone_prev.fov_alt) > 1.5f) {
        add("fov");
    }
    // Dual FOV match flip (town dump had +6C ≠ +B0)
    const bool prev_match = std::fabs(zone_prev.fov - zone_prev.fov_alt) <= 2.f;
    const bool cur_match = std::fabs(cur.fov - cur.fov_alt) <= 2.f;
    if (prev_match != cur_match) {
        add(cur_match ? "fovSync" : "fovDesync");
    }
    if (std::fabs(cur.dist - zone_prev.dist) > 40.f ||
        std::fabs(cur.dist_alt - zone_prev.dist_alt) > 40.f) {
        add("dist");
    }
    if (std::fabs(cur.angle50 - zone_prev.angle50) > 2.f ||
        std::fabs(cur.angle94 - zone_prev.angle94) > 2.f) {
        add("angle");
    }
    // Game-driven euler only when stick neutral (ignore our freelook writes)
    if (!stick_active) {
        if (std::fabs(cur.pitch - zone_prev.pitch) > 1.5f) {
            add("pitch");
        }
        if (std::fabs(cur.yaw - zone_prev.yaw) > 2.f) {
            add("yaw");
        }
    }

    if (!reasons[0]) {
        zone_prev = cur;
        return;
    }

    zone_seq++;
    const char* mode_tag = (cur.mode8c == MODE_FREELOOK)   ? "FREE"
                           : (cur.mode8c == MODE_TOWN)     ? "TOWN"
                           : "OTHER";
    LOG_WARNING(Core,
                "Hoenn ZONE-SHIFT #{} [{}] cam={:08X} slot={:08X}→{:08X} pri={:08X}→{:08X} "
                "mode48={:08X}→{:08X} mode8C={:08X}→{:08X} ({}) fl={:08X}→{:08X}",
                zone_seq, reasons, cam, zone_prev.slot, cur.slot, zone_prev.pri, cur.pri,
                zone_prev.mode48, cur.mode48, zone_prev.mode8c, cur.mode8c, mode_tag,
                zone_prev.flag, cur.flag);
    LOG_WARNING(Core,
                "Hoenn ZONE-VALS #{} p={:.2f}→{:.2f} y={:.2f}→{:.2f} fov={:.1f}/{:.1f}→{:.1f}/{:.1f} "
                "dist={:.0f}/{:.0f}→{:.0f}/{:.0f} ang={:.2f}/{:.2f}→{:.2f}/{:.2f} stick={}",
                zone_seq, zone_prev.pitch, cur.pitch, zone_prev.yaw, cur.yaw, zone_prev.fov_alt,
                zone_prev.fov, cur.fov_alt, cur.fov, zone_prev.dist_alt, zone_prev.dist,
                cur.dist_alt, cur.dist, zone_prev.angle50, zone_prev.angle94, cur.angle50,
                cur.angle94, stick_active ? "Y" : "N");
    zone_prev = cur;
}

void FreeCam::Tick(Core::System& system, u32 process_id) {
    if (!freelook && !zoom_assist) {
        return;
    }
    if (!system.IsPoweredOn()) {
        return;
    }

    last_process_id = process_id;
    if (quiet_until == 1) {
        quiet_until = system.CoreTiming().GetTicks() + QUIET_AFTER_FIELD;
    }
    if (in_battle) {
        return;
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
        LOG_WARNING(Core, "Hoenn TRANSITION slot {:08X} → {:08X} — quiet", last_slot_cam, slot_cam);
        // Do not force an immediate multi-scan mid-teardown (v6 freeze on 2F↔1F / leave).
        quiet_until = now + QUIET_AFTER_TRANSITION;
        live_count = 0;
        primary_cam = 0;
        last_collect_tick = 0;
        check_stickiness = false;
        zone_prev = {}; // re-base after hop so next zone log is clean
        pad_valid = false;
        pad_base = 0;
        t8_fov_seeded = false;
        t8_track_n = 0;
    }
    if (HeapPtr(slot_cam)) {
        last_slot_cam = slot_cam;
    }

    if (now < quiet_until) {
        return;
    }

    const u64 collect_gap = (live_count > 0) ? COLLECT_INTERVAL : COLLECT_MISS_INTERVAL;
    if (last_collect_tick == 0 || now >= last_collect_tick + collect_gap) {
        const int prev_n = live_count;
        const u32 prev_pri = primary_cam;
        CollectLiveTargets(mem, *process, slot_cam, /*collect_echoes=*/false);
        last_collect_tick = now;
        if (live_count != prev_n || primary_cam != prev_pri) {
            LOG_WARNING(Core, "Hoenn gold multi: n={} pri={:08X} slot={:08X}", live_count,
                        primary_cam, slot_cam);
            for (int i = 0; i < live_count; ++i) {
                const u32 b = live_targets[static_cast<size_t>(i)];
                LOG_INFO(Core, "Hoenn gold[{}] {:08X} fov={:.0f} fl={:X}", i, b,
                         BFloat(mem.Read32(*process, b + OFF_FOV)),
                         mem.Read32(*process, b + OFF_FLAG));
            }
            if (primary_cam && primary_cam != prev_pri) {
                SeedAnglesFromCam(mem, *process, primary_cam);
            }
        }
    }

    if (zoom_assist && primary_cam && IsGoldLive(mem, *process, primary_cam)) {
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
            user_fov = std::min(FOV_ZOOM_MAX, user_fov + FOV_STEP);
            WriteF(mem, *process, primary_cam + OFF_FOV, user_fov);
        } else if (r && !l) {
            user_fov = std::max(FOV_ZOOM_MIN, user_fov - FOV_STEP);
            WriteF(mem, *process, primary_cam + OFF_FOV, user_fov);
        }
    }

    float sx = 0.f, sy = 0.f;
    try {
        if (c_stick) {
            std::tie(sx, sy) = c_stick->GetStatus();
        }
    } catch (...) {
        c_stick.reset();
    }

    // Sample BEFORE freelook writes so we see game-driven zone flips cleanly.
    WatchZoneShift(mem, *process, slot_cam, sx, sy);
    WatchPadVariance(mem, *process, slot_cam, sx, sy);
    WatchFovCorrelate(mem, *process, slot_cam, now);

    if (!freelook) {
        return;
    }

    if (live_count == 0) {
        if ((diag++ % 40) == 0) {
            LOG_WARNING(Core, "Hoenn: no GOLD cams (slot={:08X}) — backoff", slot_cam);
        }
        // v7: do NOT zero last_collect_tick — that forced a full rescan every tick and
        // froze the emu while cams were dead during floor / leave transitions.
        check_stickiness = false;
        return;
    }

    // Stickiness: did game overwrite our last pitch on primary?
    if (check_stickiness && last_write_cam && IsGoldLive(mem, *process, last_write_cam)) {
        const float rb = BFloat(mem.Read32(*process, last_write_cam + OFF_PITCH));
        if (std::fabs(rb - last_written_pitch) > 1.5f) {
            overwrite_streak++;
            if (overwrite_streak == 3 || (overwrite_streak % 30) == 0) {
                LOG_WARNING(Core,
                            "Hoenn STICKY-FAIL cam={:08X} wrote p={:.2f} rb={:.2f} (game overwrites "
                            "— locked/scripted camera)",
                            last_write_cam, last_written_pitch, rb);
            }
        } else {
            overwrite_streak = 0;
        }
    }
    check_stickiness = false;

    stick_sx = sx;
    stick_sy = sy;
    const ExpFlags exp_f = FlagsFor(static_cast<Experiment>(experiment_mode));

    if (std::fabs(sy) >= STICK_DEADZONE && exp_f.pitch) {
        const float y = invert_y ? sy : -sy;
        pitch += y * sensitivity * PITCH_STEP;
        pitch = std::clamp(pitch, PITCH_MIN, PITCH_MAX);
    }
    if (!yaw_seeded && primary_cam) {
        const float y0 = BFloat(mem.Read32(*process, primary_cam + OFF_YAW));
        yaw = OkFloat(y0) ? y0 : 0.f;
        yaw_seeded = true;
    }
    if (std::fabs(sx) >= STICK_DEADZONE && exp_f.yaw) {
        const float x = invert_x ? -sx : sx;
        yaw += x * sensitivity * YAW_STEP;
    }

    WriteFreelookToAllLive(mem, *process);

    if (primary_cam && IsGoldLive(mem, *process, primary_cam)) {
        last_written_pitch = pitch;
        last_write_cam = primary_cam;
        check_stickiness = true;
    }

    if ((diag++ % 60) == 0) {
        const float rb =
            primary_cam ? BFloat(mem.Read32(*process, primary_cam + OFF_PITCH)) : 0.f;
        const u32 mode = primary_cam ? mem.Read32(*process, primary_cam + OFF_MODE) : 0;
        LOG_INFO(Core,
                 "Hoenn ok exp={} gold_n={} pri={:08X} twin={:08X} p={:.1f} rb={:.1f} "
                 "mode={:08X} ow={} mat_yaw={:.2f}",
                 experiment_mode, live_count, primary_cam, town_shadow_base, pitch, rb, mode,
                 overwrite_streak, mat_yaw);
    }
}

void FreeCam::WatchFovCorrelate(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                                u64 now_ticks) {
    // T8 read-only. Town dual-block writes failed; when follow-cam animates +6C,
    // find OTHER heap floats that match that FOV (possible render owners).
    u32 cam = 0;
    if (primary_cam && IsGoldLive(mem, process, primary_cam)) {
        cam = primary_cam;
    } else if (IsGoldLive(mem, process, slot_cam)) {
        cam = slot_cam;
    } else {
        return;
    }

    const float fov6 = BFloat(mem.Read32(process, cam + OFF_FOV_ALT));
    const float fovb = BFloat(mem.Read32(process, cam + OFF_FOV));
    if (!OkFloat(fov6) || fov6 < 100.f || fov6 > 500.f) {
        return;
    }

    if (!t8_fov_seeded) {
        t8_last_fov = fov6;
        t8_fov_seeded = true;
        return;
    }

    const float df = fov6 - t8_last_fov;
    const bool moved = std::fabs(df) >= T8_FOV_DELTA;
    t8_last_fov = fov6;

    // Cheap re-check of previously tracked FOV addresses every FOV move
    if (moved && t8_track_n > 0) {
        for (int i = 0; i < t8_track_n; ++i) {
            const u32 a = t8_track[static_cast<size_t>(i)];
            if (!HeapPtr(a) || a + 4 >= 0x0C000000) {
                continue;
            }
            const float f = BFloat(mem.Read32(process, a));
            const float err = std::fabs(f - fov6);
            LOG_WARNING(Core,
                        "Hoenn T8-TRACK[{}] @{:08X} f={:.2f} pri6={:.2f} err={:.2f} {}", i, a, f,
                        fov6, err, err <= T8_FOV_MATCH ? "MATCH" : "drift");
        }
    }

    if (!moved) {
        return;
    }
    if (t8_last_scan_tick != 0 && now_ticks < t8_last_scan_tick + T8_SCAN_COOLDOWN) {
        return; // throttle full scans
    }
    t8_last_scan_tick = now_ticks;
    t8_seq++;

    struct Hit {
        u32 addr;
        float fov;
        float pitch;
        u32 flag;
        u32 rel; // distance from cam
        bool cam_layout;
    };
    Hit hits[T8_MAX_HITS];
    int n_hit = 0;
    std::unordered_set<u32> seen;

    auto consider = [&](u32 addr) {
        if (n_hit >= T8_MAX_HITS || !HeapPtr(addr) || addr + 4 >= 0x0C000000) {
            return;
        }
        // Skip known dual FOV slots on primary cam
        if (addr == cam + OFF_FOV || addr == cam + OFF_FOV_ALT) {
            return;
        }
        if (seen.count(addr)) {
            return;
        }
        const float f = BFloat(mem.Read32(process, addr));
        if (!OkFloat(f) || std::fabs(f - fov6) > T8_FOV_MATCH) {
            return;
        }
        // Reject FOV=200 junk band unless primary is also ~200
        if (std::fabs(f - 200.f) < 0.5f && std::fabs(fov6 - 200.f) > 5.f) {
            return;
        }
        seen.insert(addr);

        // If this looks like FOV at +B0 of a cam object, also sample pitch/flag
        float pitch_v = 0.f;
        u32 flag = 0xFFFFFFFFu;
        bool layout = false;
        if (addr >= OFF_FOV && HeapPtr(addr - OFF_FOV)) {
            const u32 base = addr - OFF_FOV;
            flag = mem.Read32(process, base + OFF_FLAG);
            pitch_v = BFloat(mem.Read32(process, base + OFF_PITCH));
            const float a4 = BFloat(mem.Read32(process, base + 0xA4));
            if (flag == LIVE_FLAG || flag == 0 ||
                (OkFloat(a4) && std::fabs(a4 - 32.f) < 1.f) || OkPitchLayout(pitch_v)) {
                layout = true;
            }
        }
        // Also try as FOV at +6C of dual
        if (!layout && addr >= OFF_FOV_ALT && HeapPtr(addr - OFF_FOV_ALT)) {
            const u32 base = addr - OFF_FOV_ALT;
            flag = mem.Read32(process, base + OFF_FLAG);
            pitch_v = BFloat(mem.Read32(process, base + OFF_PITCH));
            if (flag == LIVE_FLAG || flag == 0 || OkPitchLayout(pitch_v)) {
                layout = true;
            }
        }

        const u32 rel = addr > cam ? addr - cam : cam - addr;
        hits[n_hit++] = {addr, f, pitch_v, flag, rel, layout};
    };

    auto scan_near = [&](u32 center) {
        if (!HeapPtr(center)) {
            return;
        }
        const u32 lo = center > T8_SCAN_RADIUS ? center - T8_SCAN_RADIUS : 0x08000000;
        const u32 hi = std::min(center + T8_SCAN_RADIUS, 0x0BFFFFFFu);
        for (u32 a = lo; a + 4 < hi; a += T8_SCAN_STEP) {
            consider(a);
        }
    };

    scan_near(cam);
    if (HeapPtr(slot_cam) && slot_cam != cam) {
        scan_near(slot_cam);
    }
    // Known shadow / stereo bands from prior RE
    scan_near(0x08285000);
    scan_near(0x08188200);

    // Sort: cam-layout first, then closer to cam
    std::sort(hits, hits + n_hit, [](const Hit& a, const Hit& b) {
        if (a.cam_layout != b.cam_layout) {
            return a.cam_layout > b.cam_layout;
        }
        return a.rel < b.rel;
    });

    LOG_WARNING(Core,
                "Hoenn T8-SCAN #{} cam={:08X} fov6={:.2f} fovB={:.2f} df={:.2f} hits={} "
                "(throttle scan ±0x{:X})",
                t8_seq, cam, fov6, fovb, df, n_hit, T8_SCAN_RADIUS);

    t8_track_n = 0;
    for (int i = 0; i < n_hit; ++i) {
        const Hit& h = hits[i];
        LOG_WARNING(Core,
                    "Hoenn T8-HIT[{}] @{:08X} fov={:.2f} p={:.2f} fl={:08X} dist={:X} {}", i,
                    h.addr, h.fov, h.pitch, h.flag, h.rel, h.cam_layout ? "CAMISH" : "raw");
        if (t8_track_n < kT8Track) {
            t8_track[static_cast<size_t>(t8_track_n++)] = h.addr;
        }
    }
    if (n_hit == 0) {
        LOG_WARNING(Core, "Hoenn T8-SCAN #{} no external FOV matches (render FOV not on nearby heap "
                          "or not stored as same float)",
                    t8_seq);
    }
}

void FreeCam::WatchPadVariance(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                               float stick_x, float stick_y) {
    // T7 read-only. Town dual-block floats are dead for freelook — hunt words
    // outside +40..+B8 and shallow pointed objects that change with follow-cam.
    u32 cam = 0;
    if (primary_cam && IsGoldLive(mem, process, primary_cam)) {
        cam = primary_cam;
    } else if (IsGoldLive(mem, process, slot_cam)) {
        cam = slot_cam;
    } else {
        return;
    }
    if (!HeapPtr(cam) || cam + static_cast<u32>(kPadWords * 4) >= 0x0C000000) {
        return;
    }

    u32 cur[kPadWords];
    for (int i = 0; i < kPadWords; ++i) {
        cur[i] = mem.Read32(process, cam + static_cast<u32>(i * 4));
    }

    if (!pad_valid || pad_base != cam) {
        pad_base = cam;
        std::memcpy(pad_prev, cur, sizeof(cur));
        pad_valid = true;
        LOG_WARNING(Core, "Hoenn PAD-BASE cam={:08X} words={} (0x000-0x{:03X})", cam, kPadWords,
                    (kPadWords - 1) * 4);
        return;
    }

    const bool stick_active =
        std::fabs(stick_x) >= STICK_DEADZONE || std::fabs(stick_y) >= STICK_DEADZONE;

    // Known dual freelook band — still report but tag so NEW stands out
    auto is_known_dual = [](u32 off) -> bool {
        return (off >= 0x40 && off <= 0xB8);
    };
    // Our freelook writes when stick moves — ignore those offsets if stick active
    auto is_our_write = [](u32 off) -> bool {
        return off == 0x54 || off == 0x58 || off == 0x98 || off == 0x9C;
    };

    struct Diff {
        u32 off;
        u32 a;
        u32 b;
        bool known;
        bool novel; // outside dual band
    };
    Diff diffs[48];
    int n_diff = 0;
    int n_novel = 0;

    for (int i = 0; i < kPadWords && n_diff < 48; ++i) {
        if (cur[i] == pad_prev[i]) {
            continue;
        }
        const u32 off = static_cast<u32>(i * 4);
        if (stick_active && is_our_write(off)) {
            continue; // noise from our pitch/yaw poke
        }
        Diff& d = diffs[n_diff++];
        d.off = off;
        d.a = pad_prev[i];
        d.b = cur[i];
        d.known = is_known_dual(off);
        d.novel = !d.known;
        if (d.novel) {
            n_novel++;
        }
    }

    // Always update baseline so we don't re-log stale
    std::memcpy(pad_prev, cur, sizeof(cur));

    if (n_diff == 0) {
        return;
    }

    // Prefer logging when something outside dual band moved, or FOV dual moved (zone)
    bool fov_moved = false;
    for (int i = 0; i < n_diff; ++i) {
        if (diffs[i].off == 0x6C || diffs[i].off == 0xB0) {
            fov_moved = true;
            break;
        }
    }
    // Skip pure freelook-noise ticks: only known dual, stick active, no FOV
    if (stick_active && n_novel == 0 && !fov_moved) {
        return;
    }
    // Idle freelook-off noise: require novel or fov
    if (n_novel == 0 && !fov_moved) {
        // Still log rare pure dual non-stick changes at low rate (mode scripts)
        if ((pad_seq % 8) != 0 && n_diff < 3) {
            return;
        }
    }

    pad_seq++;
    LOG_WARNING(Core,
                "Hoenn PAD-DIFF #{} cam={:08X} n={} novel={} stick={} — hunt fields outside dual",
                pad_seq, cam, n_diff, n_novel, stick_active ? "Y" : "N");

    // Log novel first, then known
    auto log_one = [&](const Diff& d) {
        const float fa = BFloat(d.a);
        const float fb = BFloat(d.b);
        const char* tag = d.novel ? "NEW" : "dual";
        if (OkFloat(fa) && OkFloat(fb) && std::fabs(fa) < 1.0e6f && std::fabs(fb) < 1.0e6f) {
            LOG_WARNING(Core, "Hoenn PAD +{:03X} [{}] {:08X}→{:08X} f={:.5g}→{:.5g}", d.off, tag,
                        d.a, d.b, fa, fb);
        } else {
            LOG_WARNING(Core, "Hoenn PAD +{:03X} [{}] {:08X}→{:08X}", d.off, tag, d.a, d.b);
        }
    };
    int logged = 0;
    for (int i = 0; i < n_diff && logged < 20; ++i) {
        if (diffs[i].novel) {
            log_one(diffs[i]);
            logged++;
        }
    }
    for (int i = 0; i < n_diff && logged < 28; ++i) {
        if (!diffs[i].novel) {
            log_one(diffs[i]);
            logged++;
        }
    }

    // Shallow pointer chase: heap ptrs in first 0x40 that change, or static ptrs
    // whose +0xB0 FOV-ish float moved with us (secondary cam / matrix owner)
    if (fov_moved || n_novel > 0) {
        int ptr_hits = 0;
        for (int i = 0; i < 16 && ptr_hits < 4; ++i) {
            const u32 p = cur[i];
            if (!HeapPtr(p) || p + 0xB4 >= 0x0C000000) {
                continue;
            }
            const float pfov = BFloat(mem.Read32(process, p + OFF_FOV));
            const float pp = BFloat(mem.Read32(process, p + OFF_PITCH));
            const u32 pfl = mem.Read32(process, p + OFF_FLAG);
            if (OkGoldFov(pfov) || (OkPitchLayout(pp) && pfl == LIVE_FLAG)) {
                LOG_WARNING(Core,
                            "Hoenn PAD-PTR cam+{:02X}→{:08X} fov={:.1f} p={:.2f} fl={:08X}", i * 4,
                            p, pfov, pp, pfl);
                ptr_hits++;
            }
        }
    }
}

} // namespace Hoenn
