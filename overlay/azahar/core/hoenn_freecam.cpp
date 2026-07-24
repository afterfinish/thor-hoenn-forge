// Copyright Hoenn Forge — ORAS free look + zoom assist
//
// Pitch: stick Y → cam+0x98.
// Yaw:   stick X → cam+0x9C (probe #14 locked).
// Zoom:  L or R → cam+0xB0.
//
// Cam-address RE (START menu numbered probes):
//   Map transitions (house floors, post-battle) may leave *CAMERA_SLOT on a
//   dead object. Scan nearby heap for camera-like layouts; user picks #N;
//   we drive that base only. NEVER rewrite CAMERA_SLOT (F5 hard crash).
//   NEVER multi-write all candidates (F4 black screen).
//
// Freecam ⊥ L3 turbo. No InvalidateCacheRange on data float writes.
#include "core/hoenn_freecam.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

// Scan window around official slot pointer (new cams allocate nearby)
constexpr u32 SCAN_RADIUS = 0x400000;
constexpr u32 SCAN_STEP = 0x20;

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
 * Pitch/yaw look like angles on a field camera (FOV may be dead in interiors).
 * All-zero is treated as wiped/dead memory — not a live camera.
 */
bool OkAngleLayout(float pitch_v, float yaw_v) {
    if (!OkFloat(pitch_v) || !OkFloat(yaw_v)) {
        return false;
    }
    if (std::fabs(pitch_v) >= 89.f || std::fabs(yaw_v) >= 1.0e4f) {
        return false;
    }
    return std::fabs(pitch_v) > 1.0e-3f || std::fabs(yaw_v) > 1.0e-3f;
}

/** Pitch alone in a field-cam-ish range (for scanning). */
bool OkPitchRange(float pitch_v) {
    return OkFloat(pitch_v) && pitch_v > -45.f && pitch_v < 15.f &&
           std::fabs(pitch_v) > 1.0e-3f;
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

float ScoreCandidate(float fov, float pitch_v, u32 base, u32 slot_cam) {
    // Lower is better
    float s = 0.f;
    if (OkFov(fov)) {
        s += std::fabs(fov - 270.f) * 0.02f;
    } else {
        s += 50.f; // FOV dead — still allow (interiors) but deprioritize
    }
    s += std::fabs(pitch_v - PITCH_BASE) * 0.5f;
    if (HeapPtr(slot_cam)) {
        const u32 dist = base > slot_cam ? base - slot_cam : slot_cam - base;
        s += static_cast<float>(dist) / static_cast<float>(0x10000);
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
    in_battle = false;
    zero_fov_streak = 0;
    // Keep probe_index so user can re-test after savestate; reseed yaw
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
        LOG_WARNING(Core, "Hoenn free-look ON — pitch=Y (+0x98); yaw=X (+0x9C); cam probe #{}",
                    probe_index);
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

int FreeCam::ScanCamCandidates(Core::System& system) {
    candidates.clear();
    if (!system.IsPoweredOn()) {
        LOG_WARNING(Core, "Hoenn camProbe: not powered on");
        return 0;
    }

    auto process = Resolve(system, last_process_id);
    if (!process) {
        LOG_WARNING(Core, "Hoenn camProbe: no process");
        return 0;
    }
    auto& mem = system.Memory();
    const u32 slot_cam = mem.Read32(*process, CAMERA_SLOT);

    // #0 always = official slot (auto follow)
    {
        CamCandidate c0;
        c0.base = slot_cam;
        c0.is_slot = true;
        c0.score = -1.f; // sort first
        if (HeapPtr(slot_cam)) {
            c0.fov = BFloat(mem.Read32(*process, slot_cam + OFF_FOV));
            c0.pitch = BFloat(mem.Read32(*process, slot_cam + OFF_PITCH));
            c0.yaw = BFloat(mem.Read32(*process, slot_cam + OFF_YAW));
        }
        candidates.push_back(c0);
    }

    u32 lo = 0x08000000;
    u32 hi = 0x0A000000;
    if (HeapPtr(slot_cam)) {
        lo = slot_cam > SCAN_RADIUS ? slot_cam - SCAN_RADIUS : 0x08000000;
        hi = slot_cam + SCAN_RADIUS;
        if (hi > 0x0C000000) {
            hi = 0x0C000000;
        }
    }

    std::vector<CamCandidate> found;
    u32 checked = 0;
    for (u32 base = lo; base + OFF_FOV + 4 < hi; base += SCAN_STEP) {
        checked++;
        if (base == slot_cam) {
            continue;
        }
        const float fov = BFloat(mem.Read32(*process, base + OFF_FOV));
        const float pitch_v = BFloat(mem.Read32(*process, base + OFF_PITCH));
        const float yaw_v = BFloat(mem.Read32(*process, base + OFF_YAW));

        const bool fov_ok = OkFov(fov);
        const bool angles_ok = OkAngleLayout(pitch_v, yaw_v);
        const bool pitch_ok = OkPitchRange(pitch_v);
        // Need at least FOV+pitch or solid angle pair (interiors often FOV=0)
        if (!(fov_ok && pitch_ok) && !angles_ok) {
            continue;
        }
        // Reject absurd FOV garbage when FOV is the only signal
        if (fov_ok && !pitch_ok && !angles_ok) {
            continue;
        }

        CamCandidate c;
        c.base = base;
        c.fov = fov;
        c.pitch = pitch_v;
        c.yaw = yaw_v;
        c.is_slot = false;
        c.score = ScoreCandidate(fov, pitch_v, base, slot_cam);
        found.push_back(c);
    }

    std::sort(found.begin(), found.end(),
              [](const CamCandidate& a, const CamCandidate& b) { return a.score < b.score; });

    // Dedup: keep best, max kMaxCamCandidates-1 heap hits
    const size_t max_heap = static_cast<size_t>(kMaxCamCandidates - 1);
    for (const auto& c : found) {
        if (candidates.size() >= max_heap + 1) {
            break;
        }
        bool dup = false;
        for (const auto& e : candidates) {
            if (e.base == c.base) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            candidates.push_back(c);
        }
    }

    // Clamp active probe if list shrank
    if (probe_index >= static_cast<int>(candidates.size())) {
        probe_index = 0;
    }

    LOG_WARNING(Core,
                "Hoenn camProbe: scan done n={} (slot={:08X}) checked={} window={:08X}-{:08X} "
                "active=#{}",
                candidates.size(), slot_cam, checked, lo, hi, probe_index);
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        LOG_WARNING(Core, "Hoenn camProbe: #{} cam={:08X} fov={:.1f} pitch={:.2f} yaw={:.3g} {}",
                    i, c.base, c.fov, c.pitch, c.yaw, c.is_slot ? "SLOT" : "heap");
    }
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
    char buf[128];
    if (c.is_slot) {
        if (HeapPtr(c.base)) {
            std::snprintf(buf, sizeof(buf),
                          "#%d SLOT → %08X  fov=%.0f p=%.1f%s", index, c.base, c.fov, c.pitch,
                          index == probe_index ? "  ◀" : "");
        } else {
            std::snprintf(buf, sizeof(buf), "#%d SLOT (invalid %08X)%s", index, c.base,
                          index == probe_index ? "  ◀" : "");
        }
    } else {
        std::snprintf(buf, sizeof(buf), "#%d cam=%08X  fov=%.0f p=%.1f%s", index, c.base, c.fov,
                      c.pitch, index == probe_index ? "  ◀" : "");
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
    ResetYaw();
    if (probe_index == 0) {
        LOG_WARNING(Core, "Hoenn camProbe: ACTIVE #0 SLOT (auto follow *0x{:08X})",
                    static_cast<u32>(CAMERA_SLOT));
    } else if (probe_index < static_cast<int>(candidates.size())) {
        const auto& c = candidates[static_cast<size_t>(probe_index)];
        LOG_WARNING(Core, "Hoenn camProbe: ACTIVE #{} cam={:08X} fov={:.1f} pitch={:.2f}",
                    probe_index, c.base, c.fov, c.pitch);
    }
}

int FreeCam::GetCamProbeIndex() const {
    return probe_index;
}

u32 FreeCam::GetActiveCamBase() const {
    if (probe_index > 0 && probe_index < static_cast<int>(candidates.size())) {
        return candidates[static_cast<size_t>(probe_index)].base;
    }
    return last_cam;
}

u32 FreeCam::ResolveCamBase(Memory::MemorySystem& mem, Kernel::Process& process) const {
    // #0 or no scan: follow official slot (never write it)
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

    const u32 cam = ResolveCamBase(mem, *process);
    if (!HeapPtr(cam)) {
        if ((diag++ % 50) == 0) {
            LOG_WARNING(Core, "Hoenn camera: no cam base ({:08X}) probe=#{}", cam, probe_index);
        }
        return;
    }

    if (cam != last_cam) {
        LOG_WARNING(Core, "Hoenn camera: cam {:08X} → {:08X} probe=#{} — reseed yaw", last_cam, cam,
                    probe_index);
        yaw_seeded = false;
        last_cam = cam;
        // Brief quiet so the game finishes constructing the new camera object
        // (only when following slot auto; manual probe is intentional switch)
        if (probe_index <= 0) {
            quiet_until = now + QUIET_AFTER_FIELD / 4;
            zero_fov_streak = 0;
            return;
        }
    }

    const float cur_fov = BFloat(mem.Read32(*process, cam + OFF_FOV));
    const float peek_pitch = BFloat(mem.Read32(*process, cam + OFF_PITCH));
    const float peek_yaw = BFloat(mem.Read32(*process, cam + OFF_YAW));
    const bool fov_live = OkFov(cur_fov);
    const bool angles_live = OkAngleLayout(peek_pitch, peek_yaw);

    // Full dead object: neither FOV nor angle layout looks like a field camera.
    // Do NOT rewrite CAMERA_SLOT. Wait or try another probe #.
    if (!fov_live && !angles_live) {
        zero_fov_streak++;
        yaw_seeded = false;
        if ((diag++ % 40) == 0) {
            LOG_WARNING(Core,
                        "Hoenn camera: dead cam={:08X} probe=#{} fov={:.2f} pitch={:.2f} yaw={:.2f} "
                        "— try START → Cam probe",
                        cam, probe_index, cur_fov, peek_pitch, peek_yaw);
        }
        return;
    }

    if (!fov_live) {
        zero_fov_streak++;
        if ((diag++ % 60) == 0) {
            LOG_INFO(Core,
                     "Hoenn camera: FOV inactive cam={:08X} probe=#{} fov={:.2f} — pitch/yaw only",
                     cam, probe_index, cur_fov);
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
        LOG_WARNING(Core, "Hoenn yaw SEED +0x9C = {:.4g} cam={:08X} probe=#{}", yaw, cam,
                    probe_index);
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
                 "Hoenn camera: ok cam={:08X} probe=#{} fov={:.0f} pitch={:.2f} yaw={:.3g} "
                 "sx={:+.2f} sy={:+.2f}",
                 cam, probe_index, BFloat(mem.Read32(*process, cam + OFF_FOV)), pitch, yaw, sx, sy);
    }
}

} // namespace Hoenn
