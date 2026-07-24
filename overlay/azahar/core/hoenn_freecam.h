// Copyright Hoenn Forge — ORAS free look + zoom assist
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "common/common_types.h"
#include "core/frontend/input.h"

namespace Core {
class System;
}

namespace Kernel {
class Process;
}

namespace Memory {
class MemorySystem;
}

namespace Hoenn {

/**
 * Pitch +0x98 / yaw +0x9C / FOV +0xB0.
 *
 * RE + logcat 2026-07-24:
 *   LIVE freelook:  flag+0x80 == 0x0F, FOV ~180–350 (often 225)
 *   DEAD after 2F:  flag 0, FOV garbage — pitch may still be "sticky"
 * Bug: requiring pitch-in-range for "live" false-killed real cam
 *   (log: skip writes cam=082D3458 DEAD fov=225 flag=0x0F) then hunted
 *   fake FOV floats (798/110/…) → stick felt like zoom-to-nothing.
 * Fix: live = flag 0x0F + sane FOV only; never hunt without flag 0x0F.
 * Never rewrite CAMERA_SLOT.
 */
class FreeCam {
public:
    static constexpr int kMaxCamCandidates = 16;

    static FreeCam& GetInstance();

    void SetFreelookEnabled(bool enabled);
    bool IsFreelookEnabled() const;
    void SetZoomAssistEnabled(bool enabled);
    bool IsZoomAssistEnabled() const;

    void SetEnabled(bool enabled) {
        SetFreelookEnabled(enabled);
    }
    bool IsEnabled() const {
        return IsFreelookEnabled();
    }

    void SetSensitivity(float s);
    float GetSensitivity() const;
    void SetInvertX(bool invert);
    void SetInvertY(bool invert);

    int ScanCamCandidates(Core::System& system);
    int GetCamCandidateCount() const;
    std::string GetCamCandidateLabel(int index) const;
    void SetCamProbeIndex(int index);
    int GetCamProbeIndex() const;
    u32 GetActiveCamBase() const;
    std::string DumpREState(Core::System& system, const char* tag);

    void Tick(Core::System& system, u32 process_id);
    void OnModuleLoaded(std::string_view module_name, u32 load_address = 0);
    void OnModuleUnloaded(std::string_view module_name);
    void OnCoreReconnect();

private:
    FreeCam() = default;

    struct CamCandidate {
        u32 base = 0;
        float fov = 0.f;
        float pitch = 0.f;
        float yaw = 0.f;
        u32 flag80 = 0;
        bool is_slot = false;
        bool live = false;
        float score = 0.f;
    };

    bool freelook = false;
    bool zoom_assist = false;
    float sensitivity = 3.0f;
    bool invert_x = true;
    bool invert_y = false;

    float user_fov = 480.f;
    float pitch = -12.74f;
    float yaw = 0.f;
    bool yaw_seeded = false;

    bool in_battle = false;
    u64 quiet_until = 0;
    u32 diag = 0;
    u32 last_cam = 0;
    u32 last_process_id = 0;
    u32 last_good_cam = 0;
    u32 last_slot_cam = 0;
    u32 dead_streak = 0;
    u32 live_streak = 0;
    u64 last_hunt_tick = 0;

    u32 drive_override = 0;
    int probe_index = 0;
    std::vector<CamCandidate> candidates;

    static constexpr int kDumpWords = 64;
    u32 dump_prev_base = 0;
    u32 dump_prev_words[kDumpWords]{};
    bool dump_prev_valid = false;
    char dump_prev_tag[32]{};

    std::unique_ptr<Input::AnalogDevice> c_stick;
    std::unique_ptr<Input::ButtonDevice> btn_l;
    std::unique_ptr<Input::ButtonDevice> btn_r;

    void EnsureDevices();
    void ResetYaw();
    void SeedAnglesFromCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam);
    u32 ResolveDriveBase(Memory::MemorySystem& mem, Kernel::Process& process) const;

    /** Gold standard from RE: flag 0x0F + field FOV band. No pitch gate. */
    bool IsLiveFieldCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const;
    u32 FindLiveFieldCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam) const;
    void Reacquire(Memory::MemorySystem& mem, Kernel::Process& process, const char* reason);
};

} // namespace Hoenn
