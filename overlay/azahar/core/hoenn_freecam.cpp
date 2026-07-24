// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Pitch +0x98 / yaw +0x9C / FOV +0xB0 (zoom).
//
// RE dumps 2026-07-24 @ cam=082D3458 (same pointer all three times):
//   LIVE  (1F / after leave+reenter+probe): +0x80=0x0F, +0xB0=225 FOV, freelook works
//   DEAD  (2F):                             +0x80=0,    +0xB0=garbage, pitch sticky unused
// Fix:
//   • IsLiveFieldCam = OkFov(+B0) && OkFieldPitch(+98) [flag +0x80 optional bonus]
//   • Each tick: if slot live → clear override, drive slot
//   • If slot dead → hunt nearby live FOV cam (not sticky-write junk)
//   • Never write freelook into a dead layout
//   • NEVER rewrite CAMERA_SLOT
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
constexpr u32 OFF_FLAG = 0x80; // RE: 0x0F live, 0 dead after 2F
constexpr u32 OFF_PITCH = 0x98;
constexpr u32 OFF_YAW = 0x9C;
constexpr u32 OFF_FOV = 0xB0;
constexpr u32 LIVE_FLAG_HINT = 0x0F; // observed when freelook works

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
constexpr u64 HUNT_COOLDOWN = 100'000'000; // ~0.4s between hunts
constexpr u32 SCAN_RADIUS = 0x500000;
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

bool OkFov(float f) {
    // RE: live FOV ~225; reject denorms/garbage that parse as tiny floats
    return std::isfinite(f) && f >= 100.f && f <= 1200.f;
}

bool OkFloat(float f) {
    return std::isfinite(f) && std::fabs(f) < 1.0e8f;
}

bool OkFieldPitch(float pitch_v) {
    return OkFloat(pitch_v) && pitch_v >= -40.f && pitch_v <= 8.f && std::fabs(pitch_v) > 0.05f;
}

bool OkFieldYaw(float yaw_v) {
    return OkFloat(yaw_v) && std::fabs(yaw_v) < 1.0e4f;
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
    last_slot_live = false;
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
        last_slot_live = false;
        LOG_WARNING(Core, "Hoenn camera: DllField — quiet then reacquire");
    }
}

void FreeCam::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        in_battle = true;
        drive_override = 0;
        ResetYaw();
        LOG_INFO(Core, "Hoenn camera: DllField unload — pause");
    } else if (name == "DllBattle") {
        in_battle = false;
        quiet_until = 1;
        pitch = PITCH_BASE;
        ResetYaw();
        drive_override = 0;
        last_slot_live = false;
        LOG_INFO(Core, "Hoenn camera: battle exit — quiet then reacquire");
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
        last_slot_live = false;
        LOG_WARNING(Core, "Hoenn free-look ON — live FOV layout only (RE dump fix)");
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
    const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
    const float pitch_v = BFloat(mem.Read32(process, base + OFF_PITCH));
    const float yaw_v = BFloat(mem.Read32(process, base + OFF_YAW));
    if (!OkFov(fov) || !OkFieldPitch(pitch_v) || !OkFieldYaw(yaw_v)) {
        return false;
    }
    // RE: dead object still had pitch floats that looked "ok" briefly but FOV was junk.
    // Require real FOV — that was the smoking gun in the dump.
    return true;
}

u32 FreeCam::FindLiveFieldCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                              u32 prefer_near) const {
    u32 best = 0;
    float best_score = 1e12f;

    auto consider = [&](u32 base) {
        if (!IsLiveFieldCam(mem, process, base)) {
            return;
        }
        const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
        const float pitch_v = BFloat(mem.Read32(process, base + OFF_PITCH));
        const u32 flag = mem.Read32(process, base + OFF_FLAG);
        float s = std::fabs(fov - 225.f) * 0.05f + std::fabs(pitch_v - PITCH_BASE);
        if (flag == LIVE_FLAG_HINT) {
            s -= 50.f; // RE: 0x0F when freelook works
        } else if (flag != 0) {
            s -= 10.f;
        }
        const u32 anchor = HeapPtr(prefer_near) ? prefer_near : slot_cam;
        if (HeapPtr(anchor)) {
            const u32 dist = base > anchor ? base - anchor : anchor - base;
            s += static_cast<float>(dist) / 8192.f;
        }
        if (base == slot_cam) {
            s -= 100.f;
        }
        // Prefer 082D band (dogfood working cams)
        if (base >= 0x082C0000 && base < 0x08320000) {
            s -= 20.f;
        }
        if (s < best_score) {
            best_score = s;
            best = base;
        }
    };

    consider(slot_cam);
    consider(last_good_cam);
    consider(prefer_near);

    auto scan_near = [&](u32 center) {
        if (!HeapPtr(center)) {
            return;
        }
        u32 lo = center > SCAN_RADIUS ? center - SCAN_RADIUS : 0x08000000;
        u32 hi = center + SCAN_RADIUS;
        if (hi > 0x0C000000) {
            hi = 0x0C000000;
        }
        for (u32 base = lo; base + OFF_FOV + 4 < hi; base += SCAN_STEP) {
            consider(base);
        }
    };

    scan_near(slot_cam);
    if (HeapPtr(last_good_cam) && last_good_cam != slot_cam) {
        scan_near(last_good_cam);
    }
    scan_near(0x082D0000);

    // BSS alt pointers near CAMERA_SLOT
    const u32 bss_lo = static_cast<u32>(CAMERA_SLOT) > 0x80 ? static_cast<u32>(CAMERA_SLOT) - 0x80
                                                            : static_cast<u32>(CAMERA_SLOT);
    const u32 bss_hi = static_cast<u32>(CAMERA_SLOT) + 0x80;
    for (u32 a = bss_lo; a <= bss_hi; a += 4) {
        consider(mem.Read32(process, a));
    }

    return best;
}

void FreeCam::Reacquire(Memory::MemorySystem& mem, Kernel::Process& process, const char* reason) {
    const u32 slot_cam = mem.Read32(process, CAMERA_SLOT);
    drive_override = 0;
    probe_index = 0;
    ResetYaw();

    if (IsLiveFieldCam(mem, process, slot_cam)) {
        SeedAnglesFromCam(mem, process, slot_cam);
        last_good_cam = slot_cam;
        last_slot_live = true;
        LOG_WARNING(Core, "Hoenn reacquire[{}]: SLOT live cam={:08X} fov={:.0f} flag={:08X}", reason,
                    slot_cam, BFloat(mem.Read32(process, slot_cam + OFF_FOV)),
                    mem.Read32(process, slot_cam + OFF_FLAG));
        return;
    }

    last_slot_live = false;
    const u32 found = FindLiveFieldCam(mem, process, slot_cam, last_good_cam);
    if (found && found != slot_cam) {
        drive_override = found;
        SeedAnglesFromCam(mem, process, found);
        last_good_cam = found;
        LOG_WARNING(Core,
                    "Hoenn reacquire[{}]: slot DEAD {:08X} → live override={:08X} fov={:.0f} "
                    "flag={:08X}",
                    reason, slot_cam, found, BFloat(mem.Read32(process, found + OFF_FOV)),
                    mem.Read32(process, found + OFF_FLAG));
    } else if (found == slot_cam) {
        SeedAnglesFromCam(mem, process, slot_cam);
        LOG_WARNING(Core, "Hoenn reacquire[{}]: only slot matched (weak)", reason);
    } else {
        LOG_WARNING(Core, "Hoenn reacquire[{}]: no live FOV cam (slot={:08X}) — wait map refresh",
                    reason, slot_cam);
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
    pitch = OkFieldPitch(p) ? std::clamp(p, PITCH_MIN, PITCH_MAX) : PITCH_BASE;
    yaw = OkFieldYaw(y) ? y : 0.f;
    yaw_seeded = true;
    last_cam = cam;
}

u32 FreeCam::ResolveCamBase(Memory::MemorySystem& mem, Kernel::Process& process) const {
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
    // Menu cam-probe open — same as user fix path: reacquire + list LIVE cams only
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

    {
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
        c0.score = c0.live ? -1000.f : 1000.f;
        candidates.push_back(c0);
    }

    std::unordered_set<u32> seen;
    if (HeapPtr(slot_cam)) {
        seen.insert(slot_cam);
    }

    auto add_if_live = [&](u32 base) {
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
        c.score = std::fabs(c.fov - 225.f) + std::fabs(c.pitch - PITCH_BASE);
        if (c.flag80 == LIVE_FLAG_HINT) {
            c.score -= 50.f;
        }
        candidates.push_back(c);
    };

    // Collect live FOV cams near slot
    if (HeapPtr(slot_cam)) {
        u32 lo = slot_cam > SCAN_RADIUS ? slot_cam - SCAN_RADIUS : 0x08000000;
        u32 hi = slot_cam + SCAN_RADIUS;
        if (hi > 0x0C000000) {
            hi = 0x0C000000;
        }
        for (u32 base = lo; base + OFF_FOV + 4 < hi && candidates.size() < 32; base += SCAN_STEP) {
            add_if_live(base);
        }
    }
    add_if_live(drive_override);
    add_if_live(last_good_cam);

    std::sort(candidates.begin() + 1, candidates.end(),
              [](const CamCandidate& a, const CamCandidate& b) { return a.score < b.score; });
    if (candidates.size() > static_cast<size_t>(kMaxCamCandidates)) {
        candidates.resize(static_cast<size_t>(kMaxCamCandidates));
    }

    LOG_WARNING(Core, "Hoenn camProbe: n={} slot={:08X} live={} override={:08X}", candidates.size(),
                slot_cam, candidates[0].live ? 1 : 0, drive_override);
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
    const bool active =
        (drive_override != 0 && c.base == drive_override) ||
        (drive_override == 0 && (c.is_slot || index == probe_index));
    const char* mark = active ? " <<" : "";
    if (c.is_slot) {
        std::snprintf(buf, sizeof(buf), "#%d SLOT %08X %s fov=%.0f fl=%X%s", index, c.base,
                      c.live ? "LIVE" : "DEAD", c.fov, c.flag80, mark);
    } else {
        std::snprintf(buf, sizeof(buf), "#%d %08X LIVE fov=%.0f p=%.1f fl=%X%s", index, c.base,
                      c.fov, c.pitch, c.flag80, mark);
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
        pitch = OkFieldPitch(c.pitch) ? std::clamp(c.pitch, PITCH_MIN, PITCH_MAX) : PITCH_BASE;
        yaw = OkFieldYaw(c.yaw) ? c.yaw : 0.f;
        yaw_seeded = true;
        last_cam = c.base;
        LOG_WARNING(Core, "Hoenn camProbe: ACTIVE #{} cam={:08X} live={}", probe_index, c.base,
                    c.live ? 1 : 0);
    } else {
        LOG_WARNING(Core, "Hoenn camProbe: ACTIVE #0 SLOT");
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

    LOG_WARNING(Core,
                "========== Hoenn RE DUMP [{}] ==========\n"
                "  slot*{:08X}={:08X} live={} ov={:08X} probe=#{}",
                t, static_cast<u32>(CAMERA_SLOT), slot_cam, live ? 1 : 0, drive_override,
                probe_index);

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

    const float saved_p = BFloat(mem.Read32(*process, slot_cam + OFF_PITCH));
    const float test_p = (std::fabs(saved_p + 17.5f) < 0.5f) ? -9.25f : -17.5f;
    WriteF(mem, *process, slot_cam + OFF_PITCH, test_p);
    const float rb_imm = BFloat(mem.Read32(*process, slot_cam + OFF_PITCH));
    WriteF(mem, *process, slot_cam + OFF_PITCH, saved_p);
    const bool imm_sticky = std::fabs(rb_imm - test_p) < 0.05f;
    LOG_WARNING(Core, "Hoenn RE DUMP stickiness +0x98: imm_sticky={} live_layout={}",
                imm_sticky ? 1 : 0, live ? 1 : 0);

    int n_diff = 0;
    if (dump_prev_valid && dump_prev_base == slot_cam) {
        for (int i = 0; i < kDumpWords; ++i) {
            if (words[i] != dump_prev_words[i]) {
                n_diff++;
                const u32 off = static_cast<u32>(i * 4);
                LOG_WARNING(Core, "Hoenn RE DIFF +{:02X}: {:08X} → {:08X}  (f {:.6g} → {:.6g})",
                            off, dump_prev_words[i], words[i], BFloat(dump_prev_words[i]),
                            BFloat(words[i]));
            }
        }
        LOG_WARNING(Core, "Hoenn RE DIFF: {} word(s) changed", n_diff);
    } else {
        LOG_WARNING(Core, "Hoenn RE DIFF: first snapshot");
    }

    dump_prev_base = slot_cam;
    std::memcpy(dump_prev_words, words, sizeof(words));
    dump_prev_valid = true;
    std::snprintf(dump_prev_tag, sizeof(dump_prev_tag), "%s", t);
    LOG_WARNING(Core, "========== Hoenn RE DUMP end [{}] ==========", t);

    char toast[96];
    std::snprintf(toast, sizeof(toast), "RE %08X %s sticky=%d diffs=%d", slot_cam,
                  live ? "LIVE" : "DEAD", imm_sticky ? 1 : 0, n_diff);
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
    const bool slot_live = IsLiveFieldCam(mem, *process, slot_cam);

    // Slot pointer change
    if (HeapPtr(slot_cam) && last_slot_cam != 0 && slot_cam != last_slot_cam) {
        LOG_WARNING(Core, "Hoenn TRANSITION slot {:08X} → {:08X}", last_slot_cam, slot_cam);
        Reacquire(mem, *process, "slot-ptr");
        last_hunt_tick = now;
    }
    if (HeapPtr(slot_cam)) {
        last_slot_cam = slot_cam;
    }

    // Live↔dead flip at same pointer (1F↔2F class — RE dump smoking gun)
    if (freelook && HeapPtr(slot_cam) && last_slot_live != slot_live) {
        LOG_WARNING(Core, "Hoenn layout flip slot={:08X} live {} → {} — reacquire", slot_cam,
                    last_slot_live ? 1 : 0, slot_live ? 1 : 0);
        Reacquire(mem, *process, slot_live ? "became-live" : "became-dead");
        last_hunt_tick = now;
    }
    last_slot_live = slot_live;

    if (now < quiet_until) {
        if ((diag++ % 40) == 0) {
            LOG_INFO(Core, "Hoenn camera: quiet…");
        }
        // Still track layout during quiet; reacquire when quiet ends via flip/below
        return;
    }

    // Zoom only on live FOV (slot preferred, else override if live)
    if (zoom_assist) {
        u32 zcam = 0;
        if (slot_live) {
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
                user_fov = std::min(FOV_MAX, user_fov + FOV_STEP);
                WriteF(mem, *process, zcam + OFF_FOV, user_fov);
            } else if (r && !l) {
                user_fov = std::max(FOV_MIN, user_fov - FOV_STEP);
                WriteF(mem, *process, zcam + OFF_FOV, user_fov);
            }
        }
    }

    if (!freelook) {
        return;
    }

    // --- Core fix: always prefer live slot; drop stale override ---
    if (slot_live) {
        if (drive_override != 0) {
            LOG_WARNING(Core, "Hoenn: slot LIVE again — drop override {:08X}", drive_override);
            drive_override = 0;
            SeedAnglesFromCam(mem, *process, slot_cam);
        }
    } else {
        // Slot dead: keep override only if still live; else hunt
        if (drive_override && !IsLiveFieldCam(mem, *process, drive_override)) {
            LOG_WARNING(Core, "Hoenn: override {:08X} went DEAD — clear", drive_override);
            drive_override = 0;
        }
        if (drive_override == 0 && now >= last_hunt_tick + HUNT_COOLDOWN) {
            Reacquire(mem, *process, "slot-dead-hunt");
            last_hunt_tick = now;
        }
    }

    const u32 cam = ResolveCamBase(mem, *process);
    if (!HeapPtr(cam)) {
        if ((diag++ % 50) == 0) {
            LOG_WARNING(Core, "Hoenn camera: no cam");
        }
        return;
    }

    // Refuse to write into dead layout (pitch sticky-but-useless on 2F)
    if (!IsLiveFieldCam(mem, *process, cam)) {
        if ((diag++ % 40) == 0) {
            LOG_WARNING(Core,
                        "Hoenn camera: skip writes cam={:08X} DEAD layout (fov={:.3g} flag={:08X}) "
                        "— hunting…",
                        cam, BFloat(mem.Read32(*process, cam + OFF_FOV)),
                        mem.Read32(*process, cam + OFF_FLAG));
        }
        if (now >= last_hunt_tick + HUNT_COOLDOWN) {
            Reacquire(mem, *process, "drive-dead");
            last_hunt_tick = now;
        }
        return;
    }

    if (cam != last_cam) {
        LOG_WARNING(Core, "Hoenn camera: drive {:08X} → {:08X} (live)", last_cam, cam);
        SeedAnglesFromCam(mem, *process, cam);
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
    WriteF(mem, *process, cam + OFF_PITCH, pitch);
    last_good_cam = cam;

    if (!yaw_seeded) {
        yaw = BFloat(mem.Read32(*process, cam + OFF_YAW));
        if (!OkFloat(yaw)) {
            yaw = 0.f;
        }
        yaw_seeded = true;
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
                 "Hoenn camera: ok LIVE cam={:08X} slot={:08X} slot_live={} ov={:08X} "
                 "fov={:.0f} fl={:X} p={:.2f} y={:.3g}",
                 cam, slot_cam, slot_live ? 1 : 0, drive_override,
                 BFloat(mem.Read32(*process, cam + OFF_FOV)), mem.Read32(*process, cam + OFF_FLAG),
                 pitch, yaw);
    }
}

} // namespace Hoenn
