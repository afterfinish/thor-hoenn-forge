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
 * Pitch +0x98 / yaw +0x9C. Zoom FOV on live slot only.
 *
 * RE dump 2026-07-24 (same base 082D3458):
 *   LIVE 1F / fixed: +0x80=0x0F, +0xB0 FOV~225, pitch drives view
 *   DEAD 2F:         +0x80=0,    +0xB0 garbage, pitch sticky but unused
 * Fix: drive only "live field cam" layout (FOV+pitch); if slot dead, hunt
 * nearby live FOV object; if slot becomes live again, drop override.
 * Never rewrite CAMERA_SLOT.
 */
class FreeCam {
public:
    static constexpr int kMaxCamCandidates = 20;

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
    bool last_slot_live = false;
    u64 last_hunt_tick = 0;

    // 0 = follow slot when live; else force this heap base (FOV-live hunt)
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
    u32 ResolveCamBase(Memory::MemorySystem& mem, Kernel::Process& process) const;

    /** RE: live freelook layout (FOV real + pitch range + flag often 0x0F). */
    bool IsLiveFieldCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const;
    u32 FindLiveFieldCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                         u32 prefer_near) const;
    /** Re-acquire: prefer live slot, else hunt. Same effect as cam-probe open. */
    void Reacquire(Memory::MemorySystem& mem, Kernel::Process& process, const char* reason);
};

} // namespace Hoenn
