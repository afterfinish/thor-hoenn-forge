// Copyright Hoenn Forge — ORAS free look + zoom assist
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
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
 * Pitch +0x98 / yaw +0x9C. Zoom FOV on SLOT only.
 *
 * Dogfood: freelook dies after house enter / floor change; **manual Rescan
 * restores it**. So recovery is "re-acquire drive base", not FOV tricks.
 * Auto-run the same recover path on map transitions / FOV snaps / quiet end.
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

    /** Same path as START → Cam probe → Rescan (user-proven recovery). */
    int ScanCamCandidates(Core::System& system);
    int GetCamCandidateCount() const;
    std::string GetCamCandidateLabel(int index) const;
    void SetCamProbeIndex(int index);
    int GetCamProbeIndex() const;
    u32 GetActiveCamBase() const;

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
        bool is_slot = false;
        bool is_new = false;
        bool from_bss = false;
        bool sticky = false;
        float score = 0.f;
        u32 bss_slot = 0;
    };

    enum class HuntPhase { Idle, Testing };

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
    bool recover_after_quiet = false;
    u32 diag = 0;
    u32 last_cam = 0;
    u32 last_process_id = 0;
    u32 last_good_cam = 0;
    u32 last_slot_cam = 0;
    float last_slot_fov = -1.f;
    float last_slot_pitch = 0.f;
    bool slot_snapshot_valid = false;
    u64 last_auto_recover_tick = 0;

    u32 drive_override = 0;
    bool drive_lost = false;
    u32 unsticky_streak = 0;
    float last_written_pitch = 0.f;
    bool wrote_pitch_last = false;

    int probe_index = 0;
    std::vector<CamCandidate> candidates;
    std::unordered_set<u32> prev_scan_bases;

    HuntPhase hunt = HuntPhase::Idle;
    std::vector<u32> hunt_queue;
    size_t hunt_qi = 0;
    u32 hunt_base = 0;
    float hunt_saved_pitch = 0.f;
    float hunt_test_pitch = 0.f;
    u32 hunt_wait = 0;
    u32 hunt_slot_cam = 0; // ranking anchor
    std::vector<u32> sticky_found;

    std::unique_ptr<Input::AnalogDevice> c_stick;
    std::unique_ptr<Input::ButtonDevice> btn_l;
    std::unique_ptr<Input::ButtonDevice> btn_r;

    void EnsureDevices();
    void ResetYaw();
    u32 ResolveCamBase(Memory::MemorySystem& mem, Kernel::Process& process) const;
    void SeedAnglesFromCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam);
    void DumpCamObject(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam,
                       const char* tag);
    /** Clear stale override + sticky-hunt (menu Rescan / auto house-enter). */
    void RequestRecover(Memory::MemorySystem& mem, Kernel::Process& process, const char* reason);
    void BeginStickyHunt(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                         u32 dead_cam);
    void TickStickyHunt(Memory::MemorySystem& mem, Kernel::Process& process);
    void PickBestStickyOverride(Memory::MemorySystem& mem, Kernel::Process& process);
    void OnTransition(Memory::MemorySystem& mem, Kernel::Process& process, u32 old_slot,
                      u32 new_slot);
    bool DetectSlotContentSnap(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam);
};

} // namespace Hoenn
