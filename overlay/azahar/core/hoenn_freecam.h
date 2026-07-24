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
 * Free-look + zoom + selectable experiment modes (START menu).
 * Default experiment = Stable (house-proven GOLD dual eulers).
 * Failed one-shot probes are available as menu options for dogfood without rebuild.
 */
class FreeCam {
public:
    static constexpr int kMaxCamCandidates = 16;
    static constexpr int kMaxLiveTargets = 6;
    static constexpr int kMaxEchoTargets = 2;
    /**
     * START menu experiments — failed float thrash (old 1–19) removed.
     * 0 = ship baseline. 1+ = untried / code-adjacent paths only.
     */
    static constexpr int kExperimentCount = 8;

    enum class Experiment : int {
        Stable = 0,         // GOLD dual eulers (house OK)
        ConstScan = 1,      // scan process for 000D0001 mode-table hits → FREE
        MatrixBand = 2,     // stick builds Ry matrix into post-dual / pre-dual bands
        MatrixTwin = 3,     // matrix on FOV-twin only
        OrbitWorld = 4,     // XZ orbit world-pos triples (improved)
        ConstModeGold = 5,  // const-scan + TOWN mode freeze + gold eulers
        MatrixMode = 6,     // matrix band + TOWN mode freeze
        HighRateStack = 7,  // ~60Hz tick + const-scan + matrix + gold eulers
    };

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

    void SetExperimentMode(int mode);
    int GetExperimentMode() const;
    int GetExperimentCount() const;
    std::string GetExperimentLabel(int mode) const;
    /** CoreTiming gap when freelook/zoom on (HighRateStack shortens). */
    u64 GetScheduleInterval() const;

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

    struct ExpFlags {
        bool gold_euler = true;
        bool pitch = true;
        bool yaw = true;
        bool mode_unlock = false;
        bool const_scan = false;
        bool matrix_band = false;
        bool matrix_twin_only = false;
        bool orbit_world = false;
        bool high_rate = false;
    };

    bool freelook = false;
    bool zoom_assist = false;
    float sensitivity = 3.0f;
    bool invert_x = false;
    bool invert_y = true;
    int experiment_mode = 0;

    float user_fov = 480.f;
    float pitch = -12.74f;
    float yaw = 0.f;
    bool yaw_seeded = false;
    float orbit_angle = 15.85f;
    bool orbit_seeded = false;
    float town_fov = 270.f;
    float town_dist = 2300.f;
    bool town_seeded = false;

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

    struct ZoneSnap {
        u32 slot = 0;
        u32 pri = 0;
        u32 mode48 = 0;
        u32 mode8c = 0;
        u32 flag = 0;
        float pitch = 0.f;
        float yaw = 0.f;
        float fov = 0.f;
        float fov_alt = 0.f;
        float dist = 0.f;
        float dist_alt = 0.f;
        float angle50 = 0.f;
        float angle94 = 0.f;
        bool valid = false;
    };
    ZoneSnap zone_prev{};
    u32 zone_seq = 0;

    static constexpr int kPadWords = 192;
    u32 pad_base = 0;
    u32 pad_prev[kPadWords]{};
    bool pad_valid = false;
    u32 pad_seq = 0;

    float t8_last_fov = 0.f;
    bool t8_fov_seeded = false;
    u64 t8_last_scan_tick = 0;
    u32 t8_seq = 0;
    static constexpr int kT8Track = 8;
    std::array<u32, kT8Track> t8_track{};
    int t8_track_n = 0;
    u32 town_shadow_base = 0;
    u32 t6_mode_unlock_count = 0;

    u32 dllfield_base = 0;
    bool dllfield_town_patched = false;

    // Orbit world-pos cache
    u32 orbit_obj = 0;
    u32 orbit_eye_off = 0;
    u32 orbit_tgt_off = 0;
    bool orbit_have_tgt = false;

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
    void CollectLiveTargets(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                            bool collect_echoes = false);
    void WriteFreelookToAllLive(Memory::MemorySystem& mem, Kernel::Process& process);
    void LogWideScan(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam) const;
    void WatchZoneShift(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                        float stick_x, float stick_y);
    void WatchPadVariance(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                          float stick_x, float stick_y);
    void WatchFovCorrelate(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam,
                           u64 now_ticks);
    u32 ResolveTownShadowBase(Memory::MemorySystem& mem, Kernel::Process& process, u32 gold) const;
    void TryPatchDllFieldTownConstant(Memory::MemorySystem& mem, Kernel::Process& process);
    void WriteTownModeUnlock(Memory::MemorySystem& mem, Kernel::Process& process);
    ExpFlags FlagsFor(Experiment e) const;
    void WriteEulerToCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam,
                         const ExpFlags& f);
    void WriteOrbitWorld(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam, float sx,
                         float sy);
    void WriteMatrixBand(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam, bool twin_only);
    void ScanAndPatchTownConsts(Memory::MemorySystem& mem, Kernel::Process& process);
    bool LooksWorldPos(float x, float y, float z) const;

    float stick_sx = 0.f;
    float stick_sy = 0.f;
    float mat_yaw = 0.f; // radians, matrix experiments
    bool const_scan_done = false;
};

} // namespace Hoenn
