// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Pitch +0x98 / yaw +0x9C / FOV +0xB0 (L/R zoom on primary only).
//
// Zone dogfood 2026-07-24 2F bad area:
//   RE dumps A/B/C: slot=082D3458 DEAD fl=0 fov=garbage, ov=082D3920 stuck
//   Earlier LIVE siblings: 082D3920 fov=225 fl=F, 082D4898 fov=254 fl=F
// Area camera switch leaves slot dead while another gold-LIVE object renders.
// Single override goes stale. Fix: multi-write pitch/yaw to ALL gold-LIVE
// targets (flag 0x0F + FOV 150–400). Never FOV multi-write. Never CAMERA_SLOT.
#include "core/hoenn_freecam.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
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
constexpr u32 OFF_PITCH = 0x98;
constexpr u32 OFF_YAW = 0x9C;
constexpr u32 OFF_FOV = 0xB0;
constexpr u32 LIVE_FLAG = 0x0F;

constexpr float PITCH_BASE = -12.74f;
constexpr float PITCH_MIN = -25.f;
constexpr float PITCH_MAX = 0.f;
constexpr float PITCH_STEP = 0.30f;
constexpr float YAW_STEP = 0.85f;

constexpr float FOV_LIVE_LO = 150.f;
constexpr float FOV_LIVE_HI = 400.f;
constexpr float FOV_ZOOM_MIN = 220.f;
constexpr float FOV_ZOOM_MAX = 750.f;
constexpr float FOV_ASSIST = 480.f;
constexpr float FOV_STEP = 12.f;
constexpr float STICK_DEADZONE = 0.18f;
constexpr int ANDROID_STICK_C = 718;

constexpr u64 QUIET_AFTER_FIELD = 200'000'000;
constexpr u64 COLLECT_INTERVAL = 40'000'000; // ~refresh multi-target set often
constexpr u32 SCAN_RADIUS = 0x200000;        // siblings are nearby (3920 vs 3458)
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

bool OkFieldFov(float f) {
    return std::isfinite(f) && f >= FOV_LIVE_LO && f <= FOV_LIVE_HI;
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
        LOG_WARNING(Core, "Hoenn free-look ON — multi-write all flag0x0F+FOV cams");
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

bool FreeCam::IsLiveFieldCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const {
    if (!HeapPtr(base) || base + OFF_FOV + 4 >= 0x0C000000) {
        return false;
    }
    const u32 flag = mem.Read32(process, base + OFF_FLAG);
    const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
    return flag == LIVE_FLAG && OkFieldFov(fov);
}

void FreeCam::CollectLiveTargets(Memory::MemorySystem& mem, Kernel::Process& process,
                                 u32 slot_cam) {
    live_count = 0;
    primary_cam = 0;
    std::unordered_set<u32> seen;

    auto add = [&](u32 base) {
        if (live_count >= kMaxLiveTargets) {
            return;
        }
        if (!IsLiveFieldCam(mem, process, base) || seen.count(base)) {
            return;
        }
        seen.insert(base);
        live_targets[static_cast<size_t>(live_count++)] = base;
    };

    // Slot first if live (primary)
    if (IsLiveFieldCam(mem, process, slot_cam)) {
        add(slot_cam);
        primary_cam = slot_cam;
    }

    auto scan_near = [&](u32 center) {
        if (!HeapPtr(center) || live_count >= kMaxLiveTargets) {
            return;
        }
        const u32 lo = center > SCAN_RADIUS ? center - SCAN_RADIUS : 0x08000000;
        const u32 hi = std::min(center + SCAN_RADIUS, 0x0BFFFFFFu);
        for (u32 base = lo; base + OFF_FOV + 4 < hi && live_count < kMaxLiveTargets;
             base += SCAN_STEP) {
            add(base);
        }
    };

    scan_near(slot_cam);
    scan_near(last_good_cam);
    // Known sibling band from zone logs (082Dxxxx)
    if (HeapPtr(slot_cam) && (slot_cam & 0xFFFF0000u) == 0x082D0000u) {
        scan_near(0x082D0000);
    } else {
        scan_near(0x082D0000);
    }

    // BSS pointers near official slot
    const u32 bss0 = static_cast<u32>(CAMERA_SLOT);
    for (u32 a = bss0 - 0x40; a <= bss0 + 0x40; a += 4) {
        add(mem.Read32(process, a));
    }

    add(last_good_cam);

    if (primary_cam == 0 && live_count > 0) {
        // Prefer FOV closest to 225 among live set
        float best = 1e9f;
        for (int i = 0; i < live_count; ++i) {
            const u32 b = live_targets[static_cast<size_t>(i)];
            const float fov = BFloat(mem.Read32(process, b + OFF_FOV));
            const float s = std::fabs(fov - 225.f);
            if (s < best) {
                best = s;
                primary_cam = b;
            }
        }
    }

    if (live_count > 0) {
        last_good_cam = primary_cam ? primary_cam : live_targets[0];
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
    if (OkFloat(p) && p >= PITCH_MIN - 5.f && p <= PITCH_MAX + 10.f) {
        pitch = std::clamp(p, PITCH_MIN, PITCH_MAX);
    } else {
        pitch = PITCH_BASE;
    }
    yaw = OkFloat(y) ? y : 0.f;
    yaw_seeded = true;
}

void FreeCam::WriteFreelookToAllLive(Memory::MemorySystem& mem, Kernel::Process& process) {
    for (int i = 0; i < live_count; ++i) {
        const u32 cam = live_targets[static_cast<size_t>(i)];
        if (!HeapPtr(cam)) {
            continue;
        }
        // Re-check gold LIVE — drop stale mid-frame
        if (!IsLiveFieldCam(mem, process, cam)) {
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
    c0.live = IsLiveFieldCam(mem, *process, slot_cam);
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
        c.score = std::fabs(c.fov - 225.f);
        candidates.push_back(c);
    }

    LOG_WARNING(Core, "Hoenn camProbe: live_n={} primary={:08X} slot={:08X}", live_count,
                primary_cam, slot_cam);
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
        std::snprintf(buf, sizeof(buf), "#%d SLOT %08X %s fov=%.0f fl=%X multi=%d", index, c.base,
                      c.live ? "LIVE" : "DEAD", c.fov, c.flag80, live_count);
    } else {
        std::snprintf(buf, sizeof(buf), "#%d %08X LIVE fov=%.0f fl=%X", index, c.base, c.fov,
                      c.flag80);
    }
    return std::string(buf);
}

void FreeCam::SetCamProbeIndex(int index) {
    probe_index = std::max(0, index);
    // Multi-write ignores single probe pick for drive; still reseed from pick if live
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
    CollectLiveTargets(mem, *process, slot_cam);

    LOG_WARNING(Core,
                "========== Hoenn RE DUMP [{}] slot={:08X} live_n={} primary={:08X} ==========", t,
                slot_cam, live_count, primary_cam);
    for (int i = 0; i < live_count; ++i) {
        const u32 b = live_targets[static_cast<size_t>(i)];
        LOG_WARNING(Core, "Hoenn RE LIVE[{}] {:08X} fov={:.0f} fl={:X} p={:.2f}", i, b,
                    BFloat(mem.Read32(*process, b + OFF_FOV)), mem.Read32(*process, b + OFF_FLAG),
                    BFloat(mem.Read32(*process, b + OFF_PITCH)));
    }

    if (!HeapPtr(slot_cam)) {
        return "slot invalid";
    }

    const bool slot_live = IsLiveFieldCam(mem, *process, slot_cam);
    u32 words[kDumpWords];
    for (int i = 0; i < kDumpWords; ++i) {
        words[i] = mem.Read32(*process, slot_cam + static_cast<u32>(i * 4));
    }
    for (u32 off = 0x80; off <= 0xB8; off += 4) {
        const u32 raw = mem.Read32(*process, slot_cam + off);
        LOG_WARNING(Core, "Hoenn RE DUMP +{:02X}: raw={:08X} f={:.6g}", off, raw, BFloat(raw));
    }

    int n_diff = 0;
    if (dump_prev_valid && dump_prev_base == slot_cam) {
        for (int i = 0; i < kDumpWords; ++i) {
            if (words[i] != dump_prev_words[i]) {
                n_diff++;
                LOG_WARNING(Core, "Hoenn RE DIFF +{:02X}: {:08X} → {:08X}", i * 4,
                            dump_prev_words[i], words[i]);
            }
        }
    }
    dump_prev_base = slot_cam;
    std::memcpy(dump_prev_words, words, sizeof(words));
    dump_prev_valid = true;
    std::snprintf(dump_prev_tag, sizeof(dump_prev_tag), "%s", t);

    char toast[96];
    std::snprintf(toast, sizeof(toast), "RE slot %s multi=%d pri=%08X", slot_live ? "LIVE" : "DEAD",
                  live_count, primary_cam);
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
        LOG_WARNING(Core, "Hoenn TRANSITION slot {:08X} → {:08X} — refresh multi-targets",
                    last_slot_cam, slot_cam);
        last_collect_tick = 0; // force collect
    }
    if (HeapPtr(slot_cam)) {
        last_slot_cam = slot_cam;
    }

    if (now < quiet_until) {
        return;
    }

    // Refresh gold-LIVE set often (zone switches among siblings)
    if (last_collect_tick == 0 || now >= last_collect_tick + COLLECT_INTERVAL) {
        const int prev_n = live_count;
        const u32 prev_pri = primary_cam;
        CollectLiveTargets(mem, *process, slot_cam);
        last_collect_tick = now;
        if (live_count != prev_n || primary_cam != prev_pri) {
            LOG_WARNING(Core, "Hoenn multi-cam: n={} primary={:08X} slot={:08X}", live_count,
                        primary_cam, slot_cam);
            for (int i = 0; i < live_count; ++i) {
                const u32 b = live_targets[static_cast<size_t>(i)];
                LOG_INFO(Core, "Hoenn multi-cam[{}] {:08X} fov={:.0f} fl={:X}", i, b,
                         BFloat(mem.Read32(*process, b + OFF_FOV)),
                         mem.Read32(*process, b + OFF_FLAG));
            }
            if (primary_cam && primary_cam != prev_pri) {
                SeedAnglesFromCam(mem, *process, primary_cam);
            }
        }
    }

    // Zoom only on primary LIVE (never multi-FOV)
    if (zoom_assist && primary_cam && IsLiveFieldCam(mem, *process, primary_cam)) {
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
            LOG_WARNING(Core,
                        "Hoenn: no gold LIVE cams (slot={:08X} fov={:.3g} fl={:X}) — wait zone",
                        slot_cam, HeapPtr(slot_cam) ? BFloat(mem.Read32(*process, slot_cam + OFF_FOV))
                                                    : 0.f,
                        HeapPtr(slot_cam) ? mem.Read32(*process, slot_cam + OFF_FLAG) : 0);
        }
        // Force sooner recollect
        if (now >= last_collect_tick + COLLECT_INTERVAL / 2) {
            last_collect_tick = 0;
        }
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

    // Core fix: pitch+yaw to every gold-LIVE sibling (zone camera switches)
    WriteFreelookToAllLive(mem, *process);

    if ((diag++ % 60) == 0) {
        LOG_INFO(Core, "Hoenn ok multi={} pri={:08X} slot={:08X} p={:.1f} y={:.2g}", live_count,
                 primary_cam, slot_cam, pitch, yaw);
    }
}

} // namespace Hoenn
