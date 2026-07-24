// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Pitch +0x98 / yaw +0x9C / FOV +0xB0 (L/R on primary only).
//
// Zone logs (multi-write build): freelook dead while multi still had gold LIVE
// 08188228/082D3920 with sticky pitch — those are NOT the render cam in the
// bad zone. Freelook works when slot itself is gold LIVE (082D3458/082D4898).
// Bad zone: slot DEAD (fl=0 fov=junk). Need SILVER: FOV 200–300 + pitch layout
// without requiring flag 0x0F. Multi-write gold+silver pitch/yaw only.
// Never rewrite CAMERA_SLOT. Never multi-write FOV.
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
constexpr u32 OFF_PITCH = 0x98;
constexpr u32 OFF_YAW = 0x9C;
constexpr u32 OFF_FOV = 0xB0;
constexpr u32 LIVE_FLAG = 0x0F;

constexpr float PITCH_BASE = -12.74f;
constexpr float PITCH_MIN = -25.f;
constexpr float PITCH_MAX = 0.f;
constexpr float PITCH_STEP = 0.30f;
constexpr float YAW_STEP = 0.85f;

constexpr float FOV_GOLD_LO = 150.f;
constexpr float FOV_GOLD_HI = 400.f;
// Tighter band for silver (no flag) — avoid 798-class fakes
constexpr float FOV_SILVER_LO = 200.f;
constexpr float FOV_SILVER_HI = 300.f;
constexpr float FOV_ZOOM_MIN = 220.f;
constexpr float FOV_ZOOM_MAX = 750.f;
constexpr float FOV_ASSIST = 480.f;
constexpr float FOV_STEP = 12.f;
constexpr float STICK_DEADZONE = 0.18f;
constexpr int ANDROID_STICK_C = 718;

constexpr u64 QUIET_AFTER_FIELD = 200'000'000;
constexpr u64 COLLECT_INTERVAL = 25'000'000; // refresh often in zones
constexpr u32 SCAN_RADIUS = 0x280000;
constexpr u32 SCAN_STEP = 0x10;

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

bool OkSilverFov(float f) {
    return std::isfinite(f) && f >= FOV_SILVER_LO && f <= FOV_SILVER_HI;
}

bool OkPitchLayout(float p) {
    return OkFloat(p) && p >= -45.f && p <= 15.f;
}

bool OkYawLayout(float y) {
    return OkFloat(y) && std::fabs(y) < 1.0e4f;
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
}

void FreeCam::OnCoreReconnect() {
    quiet_until = 1;
    in_battle = false;
    live_count = 0;
    primary_cam = 0;
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

void FreeCam::OnModuleLoaded(std::string_view name, u32 /*load_address*/) {
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
        LOG_WARNING(Core, "Hoenn camera: DllField — quiet");
    }
}

void FreeCam::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        in_battle = true;
        live_count = 0;
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
        LOG_WARNING(Core, "Hoenn free-look ON — gold+silver multi-write");
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

bool FreeCam::IsGoldLive(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const {
    if (!HeapPtr(base) || base + OFF_FOV + 4 >= 0x0C000000) {
        return false;
    }
    const u32 flag = mem.Read32(process, base + OFF_FLAG);
    const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
    return flag == LIVE_FLAG && OkGoldFov(fov);
}

bool FreeCam::IsSilverLive(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const {
    if (!HeapPtr(base) || base + OFF_FOV + 4 >= 0x0C000000) {
        return false;
    }
    // No flag gate — bad-zone render cam may not use 0x0F
    const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
    const float pitch_v = BFloat(mem.Read32(process, base + OFF_PITCH));
    const float yaw_v = BFloat(mem.Read32(process, base + OFF_YAW));
    if (!OkSilverFov(fov) || !OkPitchLayout(pitch_v) || !OkYawLayout(yaw_v)) {
        return false;
    }
    // Reject denorm-looking neighbor junk: require pitch magnitude or yaw motion-ish
    if (std::fabs(pitch_v) < 0.01f && std::fabs(yaw_v) < 0.01f) {
        return false;
    }
    return true;
}

bool FreeCam::IsDriveTarget(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const {
    return IsGoldLive(mem, process, base) || IsSilverLive(mem, process, base);
}

void FreeCam::CollectLiveTargets(Memory::MemorySystem& mem, Kernel::Process& process,
                                 u32 slot_cam) {
    live_count = 0;
    primary_cam = 0;
    std::unordered_set<u32> seen;

    struct Hit {
        u32 base;
        bool gold;
        float score;
    };
    std::vector<Hit> hits;
    hits.reserve(64);

    auto consider = [&](u32 base) {
        if (!HeapPtr(base) || seen.count(base)) {
            return;
        }
        const bool gold = IsGoldLive(mem, process, base);
        const bool silver = !gold && IsSilverLive(mem, process, base);
        if (!gold && !silver) {
            return;
        }
        seen.insert(base);
        const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
        float s = std::fabs(fov - 225.f);
        if (gold) {
            s -= 100.f;
        }
        if (base == slot_cam) {
            s -= 50.f;
        }
        if (HeapPtr(slot_cam)) {
            const u32 dist = base > slot_cam ? base - slot_cam : slot_cam - base;
            s += static_cast<float>(dist) / 8192.f;
        }
        const u32 fl = mem.Read32(process, base + OFF_FLAG);
        if (fl == LIVE_FLAG) {
            s -= 20.f;
        }
        hits.push_back({base, gold, s});
    };

    consider(slot_cam);
    consider(last_good_cam);

    auto scan_near = [&](u32 center) {
        if (!HeapPtr(center)) {
            return;
        }
        const u32 lo = center > SCAN_RADIUS ? center - SCAN_RADIUS : 0x08000000;
        const u32 hi = std::min(center + SCAN_RADIUS, 0x0BFFFFFFu);
        for (u32 base = lo; base + OFF_FOV + 4 < hi; base += SCAN_STEP) {
            consider(base);
        }
    };
    scan_near(slot_cam);
    scan_near(last_good_cam);
    scan_near(0x082D0000);
    scan_near(0x08180000); // saw 08188228 in logs

    const u32 bss0 = static_cast<u32>(CAMERA_SLOT);
    for (u32 a = bss0 - 0x80; a <= bss0 + 0x80; a += 4) {
        consider(mem.Read32(process, a));
    }

    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b) { return a.score < b.score; });

    for (const auto& h : hits) {
        if (live_count >= kMaxLiveTargets) {
            break;
        }
        live_targets[static_cast<size_t>(live_count++)] = h.base;
    }

    // Primary: prefer gold slot, else best gold, else best overall
    if (IsGoldLive(mem, process, slot_cam)) {
        primary_cam = slot_cam;
    } else {
        for (const auto& h : hits) {
            if (h.gold) {
                primary_cam = h.base;
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

void FreeCam::LogWideScan(Memory::MemorySystem& mem, Kernel::Process& process,
                          u32 slot_cam) const {
    // RE helper: every FOV-in-range object near slot (any flag) — find bad-zone cam
    if (!HeapPtr(slot_cam)) {
        return;
    }
    struct W {
        u32 base;
        float fov;
        float pitch;
        u32 flag;
        float dist;
    };
    std::vector<W> wide;
    const u32 lo = slot_cam > SCAN_RADIUS ? slot_cam - SCAN_RADIUS : 0x08000000;
    const u32 hi = std::min(slot_cam + SCAN_RADIUS, 0x0BFFFFFFu);
    for (u32 base = lo; base + OFF_FOV + 4 < hi; base += SCAN_STEP) {
        const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
        if (!std::isfinite(fov) || fov < 100.f || fov > 500.f) {
            continue;
        }
        const float pitch_v = BFloat(mem.Read32(process, base + OFF_PITCH));
        if (!OkPitchLayout(pitch_v) && !(mem.Read32(process, base + OFF_FLAG) == LIVE_FLAG)) {
            continue;
        }
        const u32 dist = base > slot_cam ? base - slot_cam : slot_cam - base;
        wide.push_back({base, fov, pitch_v, mem.Read32(process, base + OFF_FLAG),
                        static_cast<float>(dist)});
    }
    std::sort(wide.begin(), wide.end(), [](const W& a, const W& b) {
        return std::fabs(a.fov - 225.f) < std::fabs(b.fov - 225.f);
    });
    const int n = std::min(static_cast<int>(wide.size()), 24);
    LOG_WARNING(Core, "Hoenn RE WIDE scan near {:08X}: showing {}/{}", slot_cam, n, wide.size());
    for (int i = 0; i < n; ++i) {
        const auto& w = wide[static_cast<size_t>(i)];
        const bool g = (w.flag == LIVE_FLAG && OkGoldFov(w.fov));
        LOG_WARNING(Core, "Hoenn RE WIDE[{}] {:08X} fov={:.1f} p={:.2f} fl={:08X} dist={:X} {}", i,
                    w.base, w.fov, w.pitch, w.flag, static_cast<u32>(w.dist),
                    g ? "GOLD" : "sil/other");
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
    yaw = OkYawLayout(y) ? y : 0.f;
    yaw_seeded = true;
}

void FreeCam::WriteFreelookToAllLive(Memory::MemorySystem& mem, Kernel::Process& process) {
    for (int i = 0; i < live_count; ++i) {
        const u32 cam = live_targets[static_cast<size_t>(i)];
        if (!IsDriveTarget(mem, process, cam)) {
            continue;
        }
        WriteF(mem, process, cam + OFF_PITCH, pitch);
        if (OkFloat(yaw)) {
            WriteF(mem, process, cam + OFF_YAW, yaw);
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
    c0.gold = IsGoldLive(mem, *process, slot_cam);
    c0.silver = !c0.gold && IsSilverLive(mem, *process, slot_cam);
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
        c.gold = IsGoldLive(mem, *process, b);
        c.silver = !c.gold;
        c.fov = BFloat(mem.Read32(*process, b + OFF_FOV));
        c.pitch = BFloat(mem.Read32(*process, b + OFF_PITCH));
        c.yaw = BFloat(mem.Read32(*process, b + OFF_YAW));
        c.flag80 = mem.Read32(*process, b + OFF_FLAG);
        candidates.push_back(c);
    }
    LOG_WARNING(Core, "Hoenn camProbe: multi={} pri={:08X}", live_count, primary_cam);
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
    const char* tier = c.gold ? "GOLD" : (c.silver || c.is_slot ? "sil" : "DEAD");
    if (c.is_slot) {
        std::snprintf(buf, sizeof(buf), "#%d SLOT %08X %s fov=%.0f fl=%X n=%d", index, c.base, tier,
                      c.fov, c.flag80, live_count);
    } else {
        std::snprintf(buf, sizeof(buf), "#%d %08X %s fov=%.0f fl=%X", index, c.base, tier, c.fov,
                      c.flag80);
    }
    return std::string(buf);
}

void FreeCam::SetCamProbeIndex(int index) {
    probe_index = std::max(0, index);
    if (probe_index > 0 && probe_index < static_cast<int>(candidates.size())) {
        const auto& c = candidates[static_cast<size_t>(probe_index)];
        primary_cam = c.base;
        pitch = PITCH_BASE;
        yaw = c.yaw;
        yaw_seeded = true;
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
    CollectLiveTargets(mem, *process, slot_cam);

    LOG_WARNING(Core,
                "========== Hoenn RE DUMP [{}] slot={:08X} multi={} pri={:08X} ==========", t,
                slot_cam, live_count, primary_cam);
    for (int i = 0; i < live_count; ++i) {
        const u32 b = live_targets[static_cast<size_t>(i)];
        const bool g = IsGoldLive(mem, *process, b);
        LOG_WARNING(Core, "Hoenn RE LIVE[{}] {:08X} fov={:.0f} fl={:X} p={:.2f} {}", i, b,
                    BFloat(mem.Read32(*process, b + OFF_FOV)), mem.Read32(*process, b + OFF_FLAG),
                    BFloat(mem.Read32(*process, b + OFF_PITCH)), g ? "GOLD" : "SILVER");
    }
    LogWideScan(mem, *process, slot_cam);

    if (!HeapPtr(slot_cam)) {
        return "slot invalid";
    }
    for (u32 off = 0x80; off <= 0xB8; off += 4) {
        const u32 raw = mem.Read32(*process, slot_cam + off);
        LOG_WARNING(Core, "Hoenn RE DUMP +{:02X}: raw={:08X} f={:.6g}", off, raw, BFloat(raw));
    }

    u32 words[kDumpWords];
    for (int i = 0; i < kDumpWords; ++i) {
        words[i] = mem.Read32(*process, slot_cam + static_cast<u32>(i * 4));
    }
    int n_diff = 0;
    if (dump_prev_valid && dump_prev_base == slot_cam) {
        for (int i = 0; i < kDumpWords; ++i) {
            if (words[i] != dump_prev_words[i]) {
                n_diff++;
            }
        }
        LOG_WARNING(Core, "Hoenn RE DIFF: {} words vs prev", n_diff);
    }
    dump_prev_base = slot_cam;
    std::memcpy(dump_prev_words, words, sizeof(words));
    dump_prev_valid = true;
    std::snprintf(dump_prev_tag, sizeof(dump_prev_tag), "%s", t);

    const bool sg = IsGoldLive(mem, *process, slot_cam);
    char toast[96];
    std::snprintf(toast, sizeof(toast), "RE multi=%d pri=%08X slot=%s", live_count, primary_cam,
                  sg ? "GOLD" : "dead");
    return std::string(toast);
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
        LOG_WARNING(Core, "Hoenn TRANSITION slot {:08X} → {:08X}", last_slot_cam, slot_cam);
        last_collect_tick = 0;
    }
    if (HeapPtr(slot_cam)) {
        last_slot_cam = slot_cam;
    }

    if (now < quiet_until) {
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
    const bool stick_active =
        std::fabs(sx) >= STICK_DEADZONE || std::fabs(sy) >= STICK_DEADZONE;

    // Faster recollect when stick active (zone cam may switch under us)
    const u64 collect_gap = stick_active ? COLLECT_INTERVAL / 2 : COLLECT_INTERVAL;
    if (last_collect_tick == 0 || now >= last_collect_tick + collect_gap) {
        const int prev_n = live_count;
        const u32 prev_pri = primary_cam;
        CollectLiveTargets(mem, *process, slot_cam);
        last_collect_tick = now;
        if (live_count != prev_n || primary_cam != prev_pri) {
            LOG_WARNING(Core, "Hoenn multi: n={} pri={:08X} slot={:08X}", live_count, primary_cam,
                        slot_cam);
            for (int i = 0; i < live_count; ++i) {
                const u32 b = live_targets[static_cast<size_t>(i)];
                LOG_INFO(Core, "Hoenn multi[{}] {:08X} fov={:.0f} fl={:X} {}", i, b,
                         BFloat(mem.Read32(*process, b + OFF_FOV)),
                         mem.Read32(*process, b + OFF_FLAG),
                         IsGoldLive(mem, *process, b) ? "GOLD" : "SILVER");
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

    if (!freelook) {
        return;
    }

    if (live_count == 0) {
        if ((diag++ % 40) == 0) {
            LOG_WARNING(Core, "Hoenn: no gold/silver cams slot={:08X} — force rescan", slot_cam);
        }
        last_collect_tick = 0;
        return;
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

    WriteFreelookToAllLive(mem, *process);

    if ((diag++ % 60) == 0) {
        LOG_INFO(Core, "Hoenn ok multi={} pri={:08X} slot={:08X} p={:.1f}", live_count, primary_cam,
                 slot_cam, pitch);
    }
}

} // namespace Hoenn
