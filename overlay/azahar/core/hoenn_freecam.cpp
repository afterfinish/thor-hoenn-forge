// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Pitch +0x98 / yaw +0x9C / FOV zoom on SLOT only.
//
// Dogfood 2026-07-24 (critical):
//   Enter house → freelook dead. START → Cam probe → Rescan → works again.
//   So recovery = re-acquire drive base (same as manual rescan), not more FOV
//   probes. Auto-run RequestRecover on: slot ptr change, FOV/pitch snap at
//   slot, quiet end after field load, freelook enable.
//
// Sticky hunt: do NOT take the first sticky (dead floats are sticky; live cams
// are often overwritten). Collect all, rank near slot / FOV-live, then override.
// NEVER rewrite CAMERA_SLOT.
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
constexpr u64 RECOVER_COOLDOWN = 80'000'000; // ~0.3s emu — don't spam hunt
constexpr u32 SCAN_RADIUS = 0x600000;
constexpr u32 SCAN_STEP = 0x10;
constexpr u32 BSS_SLOT_SCAN = 0x80;
constexpr u32 UNSTICKY_BEFORE_HUNT = 8;
constexpr float STICKY_EPS = 0.35f;
constexpr float HUNT_TEST_PITCH = -18.5f;
constexpr float FOV_SNAP = 40.f;
constexpr float PITCH_SNAP = 8.f;

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

float ScoreByPitch(float pitch_v, float yaw_v, u32 base, u32 anchor, bool is_new) {
    float s = std::fabs(pitch_v - PITCH_BASE) * 2.f;
    if (!OkFieldYaw(yaw_v)) {
        s += 20.f;
    }
    if (HeapPtr(anchor)) {
        const u32 dist = base > anchor ? base - anchor : anchor - base;
        s += static_cast<float>(dist) / static_cast<float>(0x8000);
    }
    if (is_new) {
        s -= 30.f;
    }
    return s;
}

bool LooksLikeCamObject(Memory::MemorySystem& mem, Kernel::Process& process, u32 base,
                        float& out_pitch, float& out_yaw, float& out_fov) {
    if (!HeapPtr(base) || base + OFF_FOV + 4 >= 0x0C000000) {
        return false;
    }
    out_pitch = BFloat(mem.Read32(process, base + OFF_PITCH));
    out_yaw = BFloat(mem.Read32(process, base + OFF_YAW));
    out_fov = BFloat(mem.Read32(process, base + OFF_FOV));
    return OkFieldPitch(out_pitch) && OkFieldYaw(out_yaw);
}

/** Lower score = better sticky pick (prefer near slot, live FOV, 082D band). */
float RankSticky(u32 base, u32 slot_cam, Memory::MemorySystem& mem, Kernel::Process& process) {
    float s = 0.f;
    if (base == slot_cam) {
        return -1000.f; // always prefer official slot if it was sticky
    }
    if (HeapPtr(slot_cam)) {
        const u32 dist = base > slot_cam ? base - slot_cam : slot_cam - base;
        s += static_cast<float>(dist) / 4096.f;
    }
    // Working cams in dogfood clustered ~0x082Dxxxx
    if (base >= 0x082C0000 && base < 0x08320000) {
        s -= 40.f;
    }
    if (base >= 0x08000000 && base < 0x08100000) {
        s += 25.f; // earlier false sticky band (080E…) deprioritize
    }
    const float fov = BFloat(mem.Read32(process, base + OFF_FOV));
    const float pitch_v = BFloat(mem.Read32(process, base + OFF_PITCH));
    if (OkFov(fov)) {
        s -= 15.f;
        s += std::fabs(fov - 270.f) * 0.02f;
    } else {
        s += 10.f;
    }
    if (OkFieldPitch(pitch_v)) {
        s += std::fabs(pitch_v - PITCH_BASE) * 0.5f;
    }
    return s;
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
    recover_after_quiet = true;
    in_battle = false;
    drive_override = 0;
    drive_lost = false;
    unsticky_streak = 0;
    wrote_pitch_last = false;
    hunt = HuntPhase::Idle;
    hunt_queue.clear();
    sticky_found.clear();
    slot_snapshot_valid = false;
    ResetYaw();
    LOG_INFO(Core, "Hoenn camera: core reconnect — will auto-recover after quiet");
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
        drive_lost = false;
        hunt = HuntPhase::Idle;
        LOG_WARNING(Core, "Hoenn camera: battle enter — pause");
        return;
    }
    if (IsFieldMod(name)) {
        in_battle = false;
        quiet_until = 1;
        recover_after_quiet = true;
        pitch = PITCH_BASE;
        ResetYaw();
        drive_override = 0;
        drive_lost = false;
        unsticky_streak = 0;
        wrote_pitch_last = false;
        hunt = HuntPhase::Idle;
        hunt_queue.clear();
        sticky_found.clear();
        slot_snapshot_valid = false;
        LOG_WARNING(Core, "Hoenn camera: DllField — quiet then AUTO-RECOVER (like Rescan)");
    }
}

void FreeCam::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        in_battle = true;
        drive_override = 0;
        drive_lost = false;
        hunt = HuntPhase::Idle;
        ResetYaw();
        LOG_INFO(Core, "Hoenn camera: DllField unload — pause");
    } else if (name == "DllBattle") {
        in_battle = false;
        quiet_until = 1;
        recover_after_quiet = true;
        pitch = PITCH_BASE;
        ResetYaw();
        drive_override = 0;
        drive_lost = false;
        slot_snapshot_valid = false;
        LOG_INFO(Core, "Hoenn camera: battle exit — quiet then AUTO-RECOVER");
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
        drive_lost = false;
        unsticky_streak = 0;
        wrote_pitch_last = false;
        recover_after_quiet = true; // re-acquire as soon as we can tick
        LOG_WARNING(Core, "Hoenn free-look ON — will auto-recover drive base (Rescan path)");
    } else {
        hunt = HuntPhase::Idle;
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
        LOG_WARNING(Core, "Hoenn zoom assist ON (slot FOV only)");
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

void FreeCam::DumpCamObject(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam,
                           const char* tag) {
    if (!HeapPtr(cam)) {
        LOG_WARNING(Core, "Hoenn camDump[{}] invalid {:08X}", tag, cam);
        return;
    }
    float f90 = BFloat(mem.Read32(process, cam + 0x90));
    float f94 = BFloat(mem.Read32(process, cam + 0x94));
    float f98 = BFloat(mem.Read32(process, cam + 0x98));
    float f9c = BFloat(mem.Read32(process, cam + 0x9C));
    float fb0 = BFloat(mem.Read32(process, cam + 0xB0));
    LOG_WARNING(Core,
                "Hoenn camDump[{}] cam={:08X} +90={:.3g} +94={:.3g} +98={:.3g} +9C={:.3g} "
                "+B0={:.3g}",
                tag, cam, f90, f94, f98, f9c, fb0);
}

void FreeCam::RequestRecover(Memory::MemorySystem& mem, Kernel::Process& process,
                             const char* reason) {
    const u64 now = 0; // cooldown uses CoreTiming in Tick; here just run
    (void)now;
    const u32 slot_cam = mem.Read32(process, CAMERA_SLOT);
    LOG_WARNING(Core, "Hoenn AUTO-RECOVER ({}) — clear override, reseed slot={:08X}, sticky hunt",
                reason, slot_cam);
    // Same as what user does with Rescan: drop stale outdoor override
    drive_override = 0;
    drive_lost = false;
    unsticky_streak = 0;
    wrote_pitch_last = false;
    probe_index = 0; // back to slot follow; hunt may set override
    ResetYaw();
    if (HeapPtr(slot_cam)) {
        SeedAnglesFromCam(mem, process, slot_cam);
        DumpCamObject(mem, process, slot_cam, "recover-slot");
    }
    slot_snapshot_valid = false;
    BeginStickyHunt(mem, process, slot_cam, 0);
}

void FreeCam::OnTransition(Memory::MemorySystem& mem, Kernel::Process& process, u32 old_slot,
                           u32 new_slot) {
    LOG_WARNING(Core, "Hoenn TRANSITION slot cam {:08X} → {:08X}", old_slot, new_slot);
    DumpCamObject(mem, process, old_slot, "old");
    DumpCamObject(mem, process, new_slot, "new");
    RequestRecover(mem, process, "slot-ptr-change");
}

bool FreeCam::DetectSlotContentSnap(Memory::MemorySystem& mem, Kernel::Process& process,
                                    u32 slot_cam) {
    if (!HeapPtr(slot_cam)) {
        slot_snapshot_valid = false;
        return false;
    }
    const float fov = BFloat(mem.Read32(process, slot_cam + OFF_FOV));
    const float pitch_v = BFloat(mem.Read32(process, slot_cam + OFF_PITCH));
    if (!slot_snapshot_valid) {
        last_slot_fov = fov;
        last_slot_pitch = pitch_v;
        slot_snapshot_valid = true;
        return false;
    }
    bool snap = false;
    if (OkFov(fov) && OkFov(last_slot_fov) && std::fabs(fov - last_slot_fov) >= FOV_SNAP) {
        snap = true;
    }
    // FOV went live↔dead (house enter often)
    if (OkFov(fov) != OkFov(last_slot_fov) && (OkFov(fov) || OkFov(last_slot_fov))) {
        snap = true;
    }
    if (OkFieldPitch(pitch_v) && OkFieldPitch(last_slot_pitch) &&
        std::fabs(pitch_v - last_slot_pitch) >= PITCH_SNAP) {
        // only count large pitch snaps when stick not driving (game cut)
        // checked by caller context — always note FOV; pitch alone is weak
    }
    last_slot_fov = fov;
    last_slot_pitch = pitch_v;
    return snap;
}

void FreeCam::BeginStickyHunt(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                              u32 dead_cam) {
    hunt_queue.clear();
    hunt_qi = 0;
    sticky_found.clear();
    hunt = HuntPhase::Idle;
    hunt_base = 0;
    hunt_slot_cam = slot_cam;

    std::unordered_set<u32> seen;
    auto enqueue = [&](u32 base) {
        if (!HeapPtr(base) || base == dead_cam || seen.count(base)) {
            return;
        }
        float p = 0.f, y = 0.f, f = 0.f;
        if (!LooksLikeCamObject(mem, process, base, p, y, f)) {
            return;
        }
        seen.insert(base);
        hunt_queue.push_back(base);
    };

    enqueue(slot_cam);
    enqueue(last_good_cam);

    auto scan_near = [&](u32 center) {
        if (!HeapPtr(center)) {
            return;
        }
        u32 lo = center > 0x200000 ? center - 0x200000 : 0x08000000;
        u32 hi = center + 0x200000;
        if (hi > 0x0C000000) {
            hi = 0x0C000000;
        }
        for (u32 base = lo; base + OFF_FOV + 4 < hi && hunt_queue.size() < 48; base += 0x20) {
            enqueue(base);
        }
    };
    scan_near(slot_cam);
    scan_near(last_good_cam);
    scan_near(0x082D0000);

    const u32 bss_lo = static_cast<u32>(CAMERA_SLOT) > BSS_SLOT_SCAN
                           ? static_cast<u32>(CAMERA_SLOT) - BSS_SLOT_SCAN
                           : static_cast<u32>(CAMERA_SLOT);
    const u32 bss_hi = static_cast<u32>(CAMERA_SLOT) + BSS_SLOT_SCAN;
    for (u32 a = bss_lo; a <= bss_hi; a += 4) {
        enqueue(mem.Read32(process, a));
    }

    LOG_WARNING(Core, "Hoenn stickyHunt: START queue={} slot={:08X}", hunt_queue.size(), slot_cam);
    if (!hunt_queue.empty()) {
        hunt = HuntPhase::Testing;
        hunt_qi = 0;
        hunt_wait = 0;
    }
}

void FreeCam::PickBestStickyOverride(Memory::MemorySystem& mem, Kernel::Process& process) {
    if (sticky_found.empty()) {
        // No sticky — drive official slot (freelook can still race the game)
        drive_override = 0;
        drive_lost = false;
        LOG_WARNING(Core, "Hoenn stickyHunt: no sticky — drive SLOT only");
        return;
    }

    u32 best = sticky_found[0];
    float best_s = 1e9f;
    for (u32 b : sticky_found) {
        const float s = RankSticky(b, hunt_slot_cam, mem, process);
        if (s < best_s) {
            best_s = s;
            best = b;
        }
    }

    // If slot itself is sticky, always use slot (no override)
    bool slot_sticky = false;
    for (u32 b : sticky_found) {
        if (b == hunt_slot_cam) {
            slot_sticky = true;
            break;
        }
    }
    if (slot_sticky) {
        drive_override = 0;
        drive_lost = false;
        SeedAnglesFromCam(mem, process, hunt_slot_cam);
        LOG_WARNING(Core, "Hoenn stickyHunt: SLOT is sticky → drive #0 slot={:08X}", hunt_slot_cam);
        return;
    }

    drive_override = best;
    drive_lost = false;
    unsticky_streak = 0;
    SeedAnglesFromCam(mem, process, best);
    LOG_WARNING(Core,
                "Hoenn stickyHunt: RECOVER override={:08X} score={:.1f} (slot={:08X} sticky_n={})",
                best, best_s, hunt_slot_cam, sticky_found.size());
}

void FreeCam::TickStickyHunt(Memory::MemorySystem& mem, Kernel::Process& process) {
    if (hunt == HuntPhase::Idle) {
        return;
    }

    if (hunt_base != 0 && hunt_wait > 0) {
        hunt_wait--;
        if (hunt_wait > 0) {
            return;
        }
        const float rb = BFloat(mem.Read32(process, hunt_base + OFF_PITCH));
        const bool sticky = std::fabs(rb - hunt_test_pitch) < STICKY_EPS;
        WriteF(mem, process, hunt_base + OFF_PITCH, hunt_saved_pitch);
        if (sticky) {
            sticky_found.push_back(hunt_base);
            LOG_INFO(Core, "Hoenn stickyHunt: sticky cam={:08X}", hunt_base);
        }
        hunt_base = 0;
        hunt_qi++;
    }

    if (hunt_qi >= hunt_queue.size()) {
        PickBestStickyOverride(mem, process);
        for (auto& c : candidates) {
            c.sticky = false;
            for (u32 s : sticky_found) {
                if (c.base == s) {
                    c.sticky = true;
                }
            }
        }
        LOG_WARNING(Core, "Hoenn stickyHunt: DONE sticky={}/{} override={:08X}", sticky_found.size(),
                    hunt_queue.size() + sticky_found.size() > 0 ? hunt_queue.size() : 0,
                    drive_override);
        hunt = HuntPhase::Idle;
        hunt_queue.clear();
        return;
    }

    hunt_base = hunt_queue[hunt_qi];
    if (!HeapPtr(hunt_base)) {
        hunt_qi++;
        hunt_base = 0;
        return;
    }
    hunt_saved_pitch = BFloat(mem.Read32(process, hunt_base + OFF_PITCH));
    hunt_test_pitch = (std::fabs(hunt_saved_pitch - HUNT_TEST_PITCH) < 1.f) ? -8.f : HUNT_TEST_PITCH;
    WriteF(mem, process, hunt_base + OFF_PITCH, hunt_test_pitch);
    hunt_wait = 2;
}

int FreeCam::ScanCamCandidates(Core::System& system) {
    // Menu Rescan — user-proven recovery path
    std::unordered_set<u32> old_bases = prev_scan_bases;
    if (old_bases.empty()) {
        for (const auto& c : candidates) {
            if (HeapPtr(c.base)) {
                old_bases.insert(c.base);
            }
        }
    }

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
    const u32 anchor = HeapPtr(last_good_cam) ? last_good_cam
                       : HeapPtr(slot_cam)    ? slot_cam
                                              : 0x08200000;

    // Full recover (same as auto)
    RequestRecover(mem, *process, "menu-rescan");

    {
        CamCandidate c0;
        c0.base = slot_cam;
        c0.is_slot = true;
        c0.score = -1000.f;
        if (HeapPtr(slot_cam)) {
            c0.fov = BFloat(mem.Read32(*process, slot_cam + OFF_FOV));
            c0.pitch = BFloat(mem.Read32(*process, slot_cam + OFF_PITCH));
            c0.yaw = BFloat(mem.Read32(*process, slot_cam + OFF_YAW));
            c0.is_new = !old_bases.empty() && old_bases.count(slot_cam) == 0;
        }
        candidates.push_back(c0);
    }

    std::vector<CamCandidate> found;
    std::unordered_set<u32> seen;
    if (HeapPtr(slot_cam)) {
        seen.insert(slot_cam);
    }

    auto try_add = [&](u32 base, bool from_bss, u32 bss_addr) {
        if (!HeapPtr(base) || seen.count(base)) {
            return;
        }
        float pitch_v = 0.f, yaw_v = 0.f, fov_v = 0.f;
        if (!LooksLikeCamObject(mem, *process, base, pitch_v, yaw_v, fov_v)) {
            return;
        }
        seen.insert(base);
        CamCandidate c;
        c.base = base;
        c.pitch = pitch_v;
        c.yaw = yaw_v;
        c.fov = fov_v;
        c.from_bss = from_bss;
        c.bss_slot = bss_addr;
        c.is_new = !old_bases.empty() && old_bases.count(base) == 0;
        c.score = ScoreByPitch(pitch_v, yaw_v, base, anchor, c.is_new);
        found.push_back(c);
    };

    const u32 bss_lo = static_cast<u32>(CAMERA_SLOT) > BSS_SLOT_SCAN
                           ? static_cast<u32>(CAMERA_SLOT) - BSS_SLOT_SCAN
                           : static_cast<u32>(CAMERA_SLOT);
    const u32 bss_hi = static_cast<u32>(CAMERA_SLOT) + BSS_SLOT_SCAN;
    for (u32 a = bss_lo; a <= bss_hi; a += 4) {
        if (a != static_cast<u32>(CAMERA_SLOT)) {
            try_add(mem.Read32(*process, a), true, a);
        }
    }

    auto scan_window = [&](u32 center) {
        if (!HeapPtr(center)) {
            return;
        }
        u32 lo = center > SCAN_RADIUS ? center - SCAN_RADIUS : 0x08000000;
        u32 hi = center + SCAN_RADIUS;
        if (hi > 0x0C000000) {
            hi = 0x0C000000;
        }
        for (u32 base = lo; base + OFF_FOV + 4 < hi; base += SCAN_STEP) {
            try_add(base, false, 0);
        }
    };
    scan_window(slot_cam);
    if (HeapPtr(last_good_cam)) {
        scan_window(last_good_cam);
    }

    std::sort(found.begin(), found.end(),
              [](const CamCandidate& a, const CamCandidate& b) { return a.score < b.score; });
    for (const auto& c : found) {
        if (candidates.size() >= static_cast<size_t>(kMaxCamCandidates)) {
            break;
        }
        candidates.push_back(c);
    }

    prev_scan_bases.clear();
    for (const auto& c : candidates) {
        if (HeapPtr(c.base)) {
            prev_scan_bases.insert(c.base);
        }
    }

    LOG_WARNING(Core, "Hoenn camProbe menu: n={} slot={:08X} (recover+hunt running)",
                candidates.size(), slot_cam);
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
    char buf[176];
    const bool active = (drive_override != 0 && c.base == drive_override) ||
                        (drive_override == 0 && index == probe_index);
    const char* mark = active ? " <<" : "";
    if (c.is_slot) {
        std::snprintf(buf, sizeof(buf), "#%d SLOT %08X p=%.1f%s%s", index, c.base, c.pitch,
                      c.sticky ? " STICKY" : "", mark);
    } else {
        std::snprintf(buf, sizeof(buf), "#%d %08X p=%.1f%s%s", index, c.base, c.pitch,
                      c.sticky ? " STICKY" : "", mark);
    }
    return std::string(buf);
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

void FreeCam::SetCamProbeIndex(int index) {
    if (index < 0) {
        index = 0;
    }
    if (!candidates.empty() && index >= static_cast<int>(candidates.size())) {
        index = static_cast<int>(candidates.size()) - 1;
    }
    probe_index = index;
    drive_override = 0;
    drive_lost = false;
    unsticky_streak = 0;
    wrote_pitch_last = false;
    yaw_seeded = false;
    last_cam = 0;
    if (probe_index > 0 && probe_index < static_cast<int>(candidates.size())) {
        const auto& c = candidates[static_cast<size_t>(probe_index)];
        pitch = OkFieldPitch(c.pitch) ? std::clamp(c.pitch, PITCH_MIN, PITCH_MAX) : PITCH_BASE;
        yaw = OkFieldYaw(c.yaw) ? c.yaw : 0.f;
        yaw_seeded = true;
        last_cam = c.base;
        // Manual pick = force this base as override
        if (!c.is_slot) {
            drive_override = c.base;
        }
        LOG_WARNING(Core, "Hoenn camProbe: ACTIVE #{} cam={:08X}", probe_index, c.base);
    } else {
        LOG_WARNING(Core, "Hoenn camProbe: ACTIVE #0 SLOT");
    }
}

int FreeCam::GetCamProbeIndex() const {
    return probe_index;
}

u32 FreeCam::GetActiveCamBase() const {
    if (drive_override) {
        return drive_override;
    }
    return last_cam;
}

u32 FreeCam::ResolveCamBase(Memory::MemorySystem& mem, Kernel::Process& process) const {
    if (drive_override && HeapPtr(drive_override)) {
        return drive_override;
    }
    if (probe_index <= 0 || candidates.empty() ||
        probe_index >= static_cast<int>(candidates.size())) {
        return mem.Read32(process, CAMERA_SLOT);
    }
    const auto& c = candidates[static_cast<size_t>(probe_index)];
    if (c.is_slot) {
        return mem.Read32(process, CAMERA_SLOT);
    }
    return c.base;
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

    // Slot pointer change → recover (house floors when ptr updates)
    if (HeapPtr(slot_cam) && last_slot_cam != 0 && slot_cam != last_slot_cam) {
        OnTransition(mem, *process, last_slot_cam, slot_cam);
        last_auto_recover_tick = now;
    }
    if (HeapPtr(slot_cam)) {
        last_slot_cam = slot_cam;
    }

    // FOV/content snap at same pointer (enter house often keeps or swaps softly)
    if (freelook && hunt == HuntPhase::Idle && HeapPtr(slot_cam) &&
        now >= last_auto_recover_tick + RECOVER_COOLDOWN) {
        if (DetectSlotContentSnap(mem, *process, slot_cam)) {
            LOG_WARNING(Core, "Hoenn FOV/content SNAP at slot — AUTO-RECOVER (enter house class)");
            RequestRecover(mem, *process, "slot-content-snap");
            last_auto_recover_tick = now;
        }
    }

    if (freelook && hunt != HuntPhase::Idle) {
        TickStickyHunt(mem, *process);
    }

    if (now < quiet_until) {
        if ((diag++ % 40) == 0) {
            LOG_INFO(Core, "Hoenn camera: quiet…");
        }
        return;
    }

    // After quiet (field load / battle / enable): same as user Rescan
    if (freelook && recover_after_quiet && hunt == HuntPhase::Idle) {
        recover_after_quiet = false;
        if (now >= last_auto_recover_tick + RECOVER_COOLDOWN) {
            RequestRecover(mem, *process, "after-quiet");
            last_auto_recover_tick = now;
        }
    }

    if (zoom_assist && HeapPtr(slot_cam)) {
        const float slot_fov = BFloat(mem.Read32(*process, slot_cam + OFF_FOV));
        if (OkFov(slot_fov)) {
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
                WriteF(mem, *process, slot_cam + OFF_FOV, user_fov);
            } else if (r && !l) {
                user_fov = std::max(FOV_MIN, user_fov - FOV_STEP);
                WriteF(mem, *process, slot_cam + OFF_FOV, user_fov);
            }
        }
    }

    if (!freelook) {
        return;
    }

    if (hunt != HuntPhase::Idle && hunt_base != 0) {
        return; // pause freelook mid-test
    }

    const u32 cam = ResolveCamBase(mem, *process);
    if (!HeapPtr(cam)) {
        if ((diag++ % 50) == 0) {
            LOG_WARNING(Core, "Hoenn camera: no cam slot={:08X} ov={:08X}", slot_cam, drive_override);
        }
        // Try recover if slot empty
        if (hunt == HuntPhase::Idle && now >= last_auto_recover_tick + RECOVER_COOLDOWN) {
            RequestRecover(mem, *process, "no-cam");
            last_auto_recover_tick = now;
        }
        return;
    }

    if (cam != last_cam) {
        LOG_WARNING(Core, "Hoenn camera: drive {:08X} → {:08X} ov={:08X}", last_cam, cam,
                    drive_override);
        SeedAnglesFromCam(mem, *process, cam);
        unsticky_streak = 0;
        wrote_pitch_last = false;
        drive_lost = false;
    }

    // Unsticky under stick → recover (not just pause)
    if (wrote_pitch_last) {
        const float rb = BFloat(mem.Read32(*process, cam + OFF_PITCH));
        if (std::fabs(rb - last_written_pitch) > STICKY_EPS) {
            unsticky_streak++;
            if (unsticky_streak == UNSTICKY_BEFORE_HUNT) {
                LOG_WARNING(Core, "Hoenn camera: LOST cam={:08X} — AUTO-RECOVER", cam);
                if (drive_override == cam) {
                    drive_override = 0;
                }
                if (now >= last_auto_recover_tick + RECOVER_COOLDOWN) {
                    RequestRecover(mem, *process, "unsticky");
                    last_auto_recover_tick = now;
                }
            }
        } else {
            unsticky_streak = 0;
            drive_lost = false;
            last_good_cam = cam;
        }
        wrote_pitch_last = false;
    }

    float sx = 0.f, sy = 0.f;
    try {
        if (c_stick) {
            std::tie(sx, sy) = c_stick->GetStatus();
        }
    } catch (...) {
        c_stick.reset();
    }

    const bool stick_y = std::fabs(sy) >= STICK_DEADZONE;
    const bool stick_x = std::fabs(sx) >= STICK_DEADZONE;
    if (stick_y) {
        const float y = invert_y ? sy : -sy;
        pitch += y * sensitivity * PITCH_STEP;
        pitch = std::clamp(pitch, PITCH_MIN, PITCH_MAX);
    }
    WriteF(mem, *process, cam + OFF_PITCH, pitch);
    if (stick_y) {
        last_written_pitch = pitch;
        wrote_pitch_last = true;
    }

    if (!yaw_seeded) {
        yaw = BFloat(mem.Read32(*process, cam + OFF_YAW));
        if (!OkFloat(yaw)) {
            yaw = 0.f;
        }
        yaw_seeded = true;
    }
    if (stick_x) {
        const float x = invert_x ? -sx : sx;
        yaw += x * sensitivity * YAW_STEP;
    }
    if (OkFloat(yaw)) {
        WriteF(mem, *process, cam + OFF_YAW, yaw);
    }

    if ((diag++ % 60) == 0) {
        LOG_INFO(Core, "Hoenn camera: ok cam={:08X} ov={:08X} slot={:08X} p={:.2f} y={:.3g}", cam,
                 drive_override, slot_cam, pitch, yaw);
    }
}

} // namespace Hoenn
