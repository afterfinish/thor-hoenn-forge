// Copyright Hoenn Forge — ORAS free look + zoom assist
#pragma once

#include <array>
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
 * GOLD = flag+0x80==0x0F + FOV 150–400. SHADOW = flag=0 + FOV≈primary (max 2).
 * Dual mode +0x48/+0x8C; dual FOV +0x6C/+0xB0. Never slot rewrite / multi FOV.
 */
class FreeCam {
public:
    static constexpr int kMaxCamCandidates = 16;
    static constexpr int kMaxLiveTargets = 6;
    static constexpr int kMaxEchoTargets = 2;

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
    bool invert_x = false;
    bool invert_y = true;

    float user_fov = 480.f;
    float pitch = -12.74f;
    float yaw = 0.f;
    bool yaw_seeded = false;

    bool in_battle = false;
    u64 quiet_until = 0;
    u32 diag = 0;
    u32 last_process_id = 0;
    u32 last_slot_cam = 0;
    u32 last_good_cam = 0;
    u64 last_collect_tick = 0;
    float last_written_pitch = 0.f;
    u32 last_write_cam = 0;
    bool check_stickiness = false;
    u32 overwrite_streak = 0;

    std::array<u32, kMaxLiveTargets> live_targets{};
    int live_count = 0;
    std::array<u32, kMaxEchoTargets> echo_targets{};
    int echo_count = 0;
    u32 primary_cam = 0;

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

    bool IsGoldLive(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const;
    void CollectLiveTargets(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam);
    void WriteFreelookToAllLive(Memory::MemorySystem& mem, Kernel::Process& process);
    void LogWideScan(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam) const;
};

} // namespace Hoenn
