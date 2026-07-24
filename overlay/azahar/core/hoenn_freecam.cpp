// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Pitch +0x98 / yaw +0x9C / FOV +0xB0.
//
// Logcat smoking gun (2026-07-24):
//   skip writes cam=082D3458 DEAD layout (fov=225 flag=0000000F)
// Real freelook cam rejected because pitch/yaw gate failed, then hunt locked
// onto fake FOV floats (798/110/…) → stick felt like zoom-to-nothing.
//
// LIVE = flag+0x80 == 0x0F AND FOV in field band (~150–400). Pitch not required.
// Never rewrite CAMERA_SLOT. Never write freelook into non-live bases.
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
constexpr u32 LIVE_FLAG = 0x0F; // RE dumps: present iff freelook works

constexpr float PITCH_BASE = -12.74f;
constexpr float PITCH_MIN = -25.f;
constexpr float PITCH_MAX = 0.f;
constexpr float PITCH_STEP = 0.30f;
constexpr float YAW_STEP = 0.85f;

// Field FOV band from RE (225 typical). Reject 798-class false hits.
constexpr float FOV_LIVE_LO = 150.f;
constexpr float FOV_LIVE_HI = 400.f;
constexpr float FOV_ZOOM_MIN = 220.f;
constexpr float FOV_ZOOM_MAX = 750.f;
constexpr float FOV_ASSIST = 480.f;
constexpr float FOV_STEP = 12.f;
constexpr float STICK_DEADZONE = 0.18f;
constexpr int ANDROID_STICK_C = 718;

constexpr u64 QUIET_AFTER_FIELD = 200'000'000;
constexpr u64 HUNT_COOLDOWN = 250'000'000; // ~1s — stop thrash hunt
constexpr u32 SCAN_RADIUS = 0x300000;
constexpr u32 SCAN_STEP = 0x20;
constexpr u32 DEAD_STREAK_HUNT = 20; // ~ freecam ticks before hunt
constexpr u32 LIVE_STREAK_TRUST = 3;

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
    last_cam = 0;
}

void FreeCam::OnCoreReconnect() {
    quiet_until = 1;
    in_battle = false;
    drive_override = 0;
    dead_streak = 0;
    live_streak = 0;
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
        drive_override = 0;
        LOG_WARNING(Core, "Hoenn camera: battle enter — pause");
        return;
    }
    if (IsFieldMod(name)) {
        in_battle = false;
        quiet_until = 1;
        pitch = PITCH_BASE;
        ResetYaw();
        drive_override = 0;
        dead_streak = 0;
        live_streak = 0;
        LOG_WARNING(Core, "Hoenn camera: DllField — quiet");
    }
}

void FreeCam::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        in_battle = true;
        drive_override = 0;
        ResetYaw();
        LOG_INFO(Core, "Hoenn camera: DllField unload");
    } else if (name == "DllBattle") {
        in_battle = false;
        quiet_until = 1;
        pitch = PITCH_BASE;
        ResetYaw();
        drive_override = 0;
        dead_streak = 0;
        live_streak = 0;
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
        drive_override = 0;
        dead_streak = 0;
        live_streak = 0;
        LOG_WARNING(Core, "Hoenn free-look ON — live=flag0x0F+FOV band only");
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
    // Gold standard from triple RE dump — do NOT gate on pitch (false DEAD with fov=225 fl=F)
    return flag == LIVE_FLAG && OkFieldFov(fov);
}

u32 FreeCam::FindLiveFieldCam(Memory::MemorySystem& mem, Kernel::Process& process,
                              u32 slot_cam) const {
    u32 best = 0;
    float best_score = 1e12f;

    auto consider = [&](u32 base) {
        if (!IsLiveFieldCam(mem, process, base)) {
            return;
        }
        const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
        float s = std::fabs(fov - 225.f);
        if (base == slot_cam) {
            s -= 100.f;
        }
        if (HeapPtr(slot_cam)) {
            const u32 dist = base > slot_cam ? base - slot_cam : slot_cam - base;
            s += static_cast<float>(dist) / 4096.f;
        }
        if (base >= 0x082C0000 && base < 0x08320000) {
            s -= 15.f;
        }
        if (s < best_score) {
            best_score = s;
            best = base;
        }
    };

    consider(slot_cam);
    consider(last_good_cam);

    auto scan_near = [&](u32 center) {
        if (!HeapPtr(center)) {
            return;
        }
        u32 lo = center > SCAN_RADIUS ? center - SCAN_RADIUS : 0x08000000;
        u32 hi = std::min(center + SCAN_RADIUS, 0x0BFFFFFFu);
        for (u32 base = lo; base + OFF_FOV + 4 < hi; base += SCAN_STEP) {
            consider(base);
        }
    };
    scan_near(slot_cam);
    scan_near(last_good_cam);
    scan_near(0x082D0000);

    const u32 bss_lo = static_cast<u32>(CAMERA_SLOT) > 0x40 ? static_cast<u32>(CAMERA_SLOT) - 0x40
                                                            : static_cast<u32>(CAMERA_SLOT);
    for (u32 a = bss_lo; a <= static_cast<u32>(CAMERA_SLOT) + 0x40; a += 4) {
        consider(mem.Read32(process, a));
    }
    return best;
}

void FreeCam::Reacquire(Memory::MemorySystem& mem, Kernel::Process& process, const char* reason) {
    const u32 slot_cam = mem.Read32(process, CAMERA_SLOT);
    drive_override = 0;
    probe_index = 0;

    if (IsLiveFieldCam(mem, process, slot_cam)) {
        SeedAnglesFromCam(mem, process, slot_cam);
        last_good_cam = slot_cam;
        dead_streak = 0;
        LOG_WARNING(Core, "Hoenn reacquire[{}]: SLOT LIVE {:08X} fov={:.0f} fl={:X}", reason,
                    slot_cam, BFloat(mem.Read32(process, slot_cam + OFF_FOV)),
                    mem.Read32(process, slot_cam + OFF_FLAG));
        return;
    }

    const u32 found = FindLiveFieldCam(mem, process, slot_cam);
    if (found) {
        if (found != slot_cam) {
            drive_override = found;
        }
        SeedAnglesFromCam(mem, process, found);
        last_good_cam = found;
        dead_streak = 0;
        LOG_WARNING(Core, "Hoenn reacquire[{}]: LIVE {:08X} (slot={:08X}) fov={:.0f} fl={:X}",
                    reason, found, slot_cam, BFloat(mem.Read32(process, found + OFF_FOV)),
                    mem.Read32(process, found + OFF_FLAG));
    } else {
        LOG_WARNING(Core, "Hoenn reacquire[{}]: no flag=0x0F+FOV cam (slot={:08X})", reason,
                    slot_cam);
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
    // Soft seed — do not require "field pitch" range (that gate caused DEAD false positives)
    if (OkFloat(p) && p >= PITCH_MIN - 5.f && p <= PITCH_MAX + 10.f) {
        pitch = std::clamp(p, PITCH_MIN, PITCH_MAX);
    } else {
        pitch = PITCH_BASE;
    }
    yaw = OkFloat(y) ? y : 0.f;
    yaw_seeded = true;
    last_cam = cam;
}

u32 FreeCam::ResolveDriveBase(Memory::MemorySystem& mem, Kernel::Process& process) const {
    if (drive_override && HeapPtr(drive_override)) {
        return drive_override;
    }
    if (probe_index > 0 && probe_index < static_cast<int>(candidates.size())) {
        const auto& c = candidates[static_cast<size_t>(probe_index)];
        if (!c.is_slot && HeapPtr(c.base)) {
            return c.base;
        }
    }
    return mem.Read32(process, CAMERA_SLOT);
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

    Reacquire(mem, *process, "menu-cam-probe");

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

    std::unordered_set<u32> seen;
    if (HeapPtr(slot_cam)) {
        seen.insert(slot_cam);
    }
    auto add = [&](u32 base) {
        if (!HeapPtr(base) || seen.count(base) || !IsLiveFieldCam(mem, *process, base)) {
            return;
        }
        seen.insert(base);
        CamCandidate c;
        c.base = base;
        c.live = true;
        c.fov = BFloat(mem.Read32(*process, base + OFF_FOV));
        c.pitch = BFloat(mem.Read32(*process, base + OFF_PITCH));
        c.yaw = BFloat(mem.Read32(*process, base + OFF_YAW));
        c.flag80 = mem.Read32(*process, base + OFF_FLAG);
        c.score = std::fabs(c.fov - 225.f);
        candidates.push_back(c);
    };

    if (HeapPtr(slot_cam)) {
        u32 lo = slot_cam > SCAN_RADIUS ? slot_cam - SCAN_RADIUS : 0x08000000;
        u32 hi = std::min(slot_cam + SCAN_RADIUS, 0x0BFFFFFFu);
        for (u32 b = lo; b + OFF_FOV + 4 < hi && candidates.size() < 20; b += SCAN_STEP) {
            add(b);
        }
    }
    add(drive_override);
    add(last_good_cam);

    if (candidates.size() > 1) {
        std::sort(candidates.begin() + 1, candidates.end(),
                  [](const CamCandidate& a, const CamCandidate& b) { return a.score < b.score; });
    }
    LOG_WARNING(Core, "Hoenn camProbe: n={} slot_live={}", candidates.size(), c0.live ? 1 : 0);
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
    const bool active = (drive_override != 0 && c.base == drive_override) ||
                        (drive_override == 0 && c.is_slot);
    if (c.is_slot) {
        std::snprintf(buf, sizeof(buf), "#%d SLOT %08X %s fov=%.0f fl=%X%s", index, c.base,
                      c.live ? "LIVE" : "DEAD", c.fov, c.flag80, active ? " <<" : "");
    } else {
        std::snprintf(buf, sizeof(buf), "#%d %08X LIVE fov=%.0f fl=%X%s", index, c.base, c.fov,
                      c.flag80, active ? " <<" : "");
    }
    return std::string(buf);
}

void FreeCam::SetCamProbeIndex(int index) {
    if (index < 0) {
        index = 0;
    }
    if (!candidates.empty() && index >= static_cast<int>(candidates.size())) {
        index = static_cast<int>(candidates.size()) - 1;
    }
    probe_index = index;
    drive_override = 0;
    ResetYaw();
    if (probe_index > 0 && probe_index < static_cast<int>(candidates.size())) {
        const auto& c = candidates[static_cast<size_t>(probe_index)];
        if (!c.is_slot) {
            drive_override = c.base;
        }
        pitch = PITCH_BASE;
        yaw = c.yaw;
        yaw_seeded = true;
        last_cam = c.base;
    }
}

int FreeCam::GetCamProbeIndex() const {
    return probe_index;
}

u32 FreeCam::GetActiveCamBase() const {
    return drive_override ? drive_override : last_cam;
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
    const bool live = IsLiveFieldCam(mem, *process, slot_cam);

    LOG_WARNING(Core, "========== Hoenn RE DUMP [{}] slot={:08X} live={} ov={:08X} ==========", t,
                slot_cam, live ? 1 : 0, drive_override);
    if (!HeapPtr(slot_cam)) {
        return "slot invalid";
    }

    u32 words[kDumpWords];
    for (int i = 0; i < kDumpWords; ++i) {
        words[i] = mem.Read32(*process, slot_cam + static_cast<u32>(i * 4));
    }
    for (u32 off = 0x80; off <= 0xB8; off += 4) {
        const u32 raw = mem.Read32(*process, slot_cam + off);
        LOG_WARNING(Core, "Hoenn RE DUMP +{:02X}: raw={:08X} f={:.6g}", off, raw, BFloat(raw));
    }
    for (int row = 0; row < kDumpWords; row += 4) {
        LOG_WARNING(Core, "Hoenn RE DUMP obj+{:02X}: {:08X} {:08X} {:08X} {:08X}", row * 4,
                    words[row], words[row + 1], words[row + 2], words[row + 3]);
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
        LOG_WARNING(Core, "Hoenn RE DIFF: {} words", n_diff);
    }

    dump_prev_base = slot_cam;
    std::memcpy(dump_prev_words, words, sizeof(words));
    dump_prev_valid = true;
    std::snprintf(dump_prev_tag, sizeof(dump_prev_tag), "%s", t);

    char toast[80];
    std::snprintf(toast, sizeof(toast), "RE %08X %s fl=%X fov=%.0f d=%d", slot_cam,
                  live ? "LIVE" : "DEAD", mem.Read32(*process, slot_cam + OFF_FLAG),
                  BFloat(mem.Read32(*process, slot_cam + OFF_FOV)), n_diff);
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
    const bool slot_live_now = IsLiveFieldCam(mem, *process, slot_cam);

    if (HeapPtr(slot_cam) && last_slot_cam != 0 && slot_cam != last_slot_cam) {
        LOG_WARNING(Core, "Hoenn TRANSITION slot {:08X} → {:08X}", last_slot_cam, slot_cam);
        dead_streak = 0;
        live_streak = 0;
        Reacquire(mem, *process, "slot-ptr");
        last_hunt_tick = now;
    }
    if (HeapPtr(slot_cam)) {
        last_slot_cam = slot_cam;
    }

    if (slot_live_now) {
        live_streak++;
        dead_streak = 0;
    } else {
        dead_streak++;
        live_streak = 0;
    }

    if (now < quiet_until) {
        return;
    }

    // Zoom L/R only on confirmed live base — never on hunt fakes (zoom-to-nothing)
    if (zoom_assist) {
        u32 zcam = 0;
        if (slot_live_now) {
            zcam = slot_cam;
        } else if (drive_override && IsLiveFieldCam(mem, *process, drive_override)) {
            zcam = drive_override;
        }
        if (zcam) {
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
                WriteF(mem, *process, zcam + OFF_FOV, user_fov);
            } else if (r && !l) {
                user_fov = std::max(FOV_ZOOM_MIN, user_fov - FOV_STEP);
                WriteF(mem, *process, zcam + OFF_FOV, user_fov);
            }
        }
    }

    if (!freelook) {
        return;
    }

    // Prefer live slot; drop bad overrides
    if (live_streak >= LIVE_STREAK_TRUST) {
        if (drive_override != 0) {
            LOG_WARNING(Core, "Hoenn: slot LIVE (flag0x0F+FOV) — drop override {:08X}",
                        drive_override);
            drive_override = 0;
            SeedAnglesFromCam(mem, *process, slot_cam);
        }
    } else {
        if (drive_override && !IsLiveFieldCam(mem, *process, drive_override)) {
            LOG_INFO(Core, "Hoenn: clear dead override {:08X}", drive_override);
            drive_override = 0;
        }
        // Only hunt after sustained dead — not one-frame pitch glitch
        if (drive_override == 0 && dead_streak >= DEAD_STREAK_HUNT &&
            now >= last_hunt_tick + HUNT_COOLDOWN) {
            Reacquire(mem, *process, "sustained-dead");
            last_hunt_tick = now;
        }
    }

    const u32 cam = ResolveDriveBase(mem, *process);
    if (!HeapPtr(cam)) {
        return;
    }

    // Soft: if current drive isn't gold-live but slot is, switch to slot
    if (!IsLiveFieldCam(mem, *process, cam)) {
        if (IsLiveFieldCam(mem, *process, slot_cam)) {
            drive_override = 0;
            SeedAnglesFromCam(mem, *process, slot_cam);
        } else if (dead_streak >= DEAD_STREAK_HUNT && now >= last_hunt_tick + HUNT_COOLDOWN) {
            if ((diag++ % 30) == 0) {
                LOG_WARNING(Core,
                            "Hoenn: no LIVE flag0x0F cam (slot fov={:.3g} fl={:X}) — not writing "
                            "(avoids zoom-to-nothing fakes)",
                            BFloat(mem.Read32(*process, slot_cam + OFF_FOV)),
                            mem.Read32(*process, slot_cam + OFF_FLAG));
            }
            Reacquire(mem, *process, "no-live-drive");
            last_hunt_tick = now;
            return;
        } else {
            // Grace: keep writing last good if we recently had freelook on this base
            if (cam != last_good_cam || last_good_cam == 0) {
                return;
            }
            // fall through write to last_good while briefly non-gold
        }
    }

    const u32 drive = IsLiveFieldCam(mem, *process, cam) ? cam
                      : (IsLiveFieldCam(mem, *process, slot_cam) ? slot_cam : cam);
    if (!IsLiveFieldCam(mem, *process, drive) && drive != last_good_cam) {
        return;
    }

    if (drive != last_cam) {
        LOG_WARNING(Core, "Hoenn drive {:08X} → {:08X} live={}", last_cam, drive,
                    IsLiveFieldCam(mem, *process, drive) ? 1 : 0);
        SeedAnglesFromCam(mem, *process, drive);
    }

    float sx = 0.f, sy = 0.f;
    try {
        if (c_stick) {
            std::tie(sx, sy) = c_stick->GetStatus();
        }
    } catch (...) {
        c_stick.reset();
    }

    // Pitch/yaw ONLY — never FOV from stick (zoom-to-nothing was fake-FOV writes)
    if (std::fabs(sy) >= STICK_DEADZONE) {
        const float y = invert_y ? sy : -sy;
        pitch += y * sensitivity * PITCH_STEP;
        pitch = std::clamp(pitch, PITCH_MIN, PITCH_MAX);
    }
    WriteF(mem, *process, drive + OFF_PITCH, pitch);
    if (IsLiveFieldCam(mem, *process, drive)) {
        last_good_cam = drive;
    }

    if (!yaw_seeded) {
        const float y0 = BFloat(mem.Read32(*process, drive + OFF_YAW));
        yaw = OkFloat(y0) ? y0 : 0.f;
        yaw_seeded = true;
    }
    if (std::fabs(sx) >= STICK_DEADZONE) {
        const float x = invert_x ? -sx : sx;
        yaw += x * sensitivity * YAW_STEP;
    }
    if (OkFloat(yaw)) {
        WriteF(mem, *process, drive + OFF_YAW, yaw);
    }

    if ((diag++ % 60) == 0) {
        LOG_INFO(Core,
                 "Hoenn ok drive={:08X} slot={:08X} live={} ov={:08X} fov={:.0f} fl={:X} p={:.1f}",
                 drive, slot_cam, IsLiveFieldCam(mem, *process, drive) ? 1 : 0, drive_override,
                 BFloat(mem.Read32(*process, drive + OFF_FOV)),
                 mem.Read32(*process, drive + OFF_FLAG), pitch);
    }
}

} // namespace Hoenn
