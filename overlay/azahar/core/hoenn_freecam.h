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
 * Pitch: stick Y → cam+0x98 (proven).
 * Yaw:   stick X → cam+0x9C (RE probe #14, dogfood 2026-07-23).
 * Zoom:  L/R → cam+0xB0.
 *
 * Cam-address RE probe (START menu, numbered):
 *   After map transitions the official slot may point at a dead object while
 *   the live camera lives elsewhere. Scan heap candidates and drive one
 *   selected base. Never rewrite CAMERA_SLOT (hard-crash history).
 *
 * Independent of L3 turbo. No multi-write blast of all candidates.
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

    /**
     * RE: scan heap for camera-like objects. Fills numbered list.
     * #0 is always "slot" (follow *CAMERA_SLOT). #1..N are candidates.
     * Safe to call from JNI while powered on. Does not write CAMERA_SLOT.
     * @return number of entries (including #0)
     */
    int ScanCamCandidates(Core::System& system);
    int GetCamCandidateCount() const;
    /** Human label for START menu: "#3 cam=082D2F48 fov=250 p=-12.7" */
    std::string GetCamCandidateLabel(int index) const;
    /** Drive this candidate; 0 = follow official slot (auto). */
    void SetCamProbeIndex(int index);
    int GetCamProbeIndex() const;
    /** Currently driven cam base (0 if unknown). */
    u32 GetActiveCamBase() const;

    void Tick(Core::System& system, u32 process_id);
    void OnModuleLoaded(std::string_view module_name, u32 load_address = 0);
    void OnModuleUnloaded(std::string_view module_name);
    /** After savestate load / cheat engine reconnect. */
    void OnCoreReconnect();

private:
    FreeCam() = default;

    struct CamCandidate {
        u32 base = 0;
        float fov = 0.f;
        float pitch = 0.f;
        float yaw = 0.f;
        bool is_slot = false; // index 0 entry
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
    u32 zero_fov_streak = 0;
    u32 diag = 0;
    u32 last_cam = 0;
    u32 last_process_id = 0;

    // Cam-address probe: 0 = follow slot; >0 = drive candidates[index].base
    int probe_index = 0;
    std::vector<CamCandidate> candidates;

    std::unique_ptr<Input::AnalogDevice> c_stick;
    std::unique_ptr<Input::ButtonDevice> btn_l;
    std::unique_ptr<Input::ButtonDevice> btn_r;

    void EnsureDevices();
    void ResetYaw();
    u32 ResolveCamBase(Memory::MemorySystem& mem, Kernel::Process& process) const;
};

} // namespace Hoenn
