// Copyright Hoenn Forge — experimental overworld follower probe
#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
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
 * Rough follower experiment — not product-quality.
 *
 * Path-lag thrash only. Discovery is budgeted so the emu cannot freeze.
 */
class FollowerProbe {
public:
    static FollowerProbe& GetInstance();

    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void SetPartyLeadSpecies(int species);
    int GetPartyLeadSpecies() const;

    void OnModuleLoaded(std::string_view module_name, u32 load_address = 0);
    void OnModuleUnloaded(std::string_view module_name);
    void OnCoreReconnect();
    void Tick(Core::System& system, u32 process_id);

    std::string StatusLine() const;

private:
    FollowerProbe() = default;

    bool enabled = false;
    bool in_battle = false;
    u32 dllfield_base = 0;
    u32 last_process_id = 0;
    u32 tick_n = 0;
    int party_lead_species = 0;
    float last_pad_x = 0.f;
    float last_pad_y = 0.f;

    // Path history
    static constexpr int kHist = 16;
    std::array<float, kHist> hx{}, hy{}, hz{};
    int hist_n = 0;
    int hist_i = 0;

    // Pad-correlation candidates for player lock
    static constexpr int kMaxCand = 12;
    struct Cand {
        u32 base = 0;
        u32 off = 0;
        float score = 0.f;
        float px = 0.f, pz = 0.f;
        bool live = false;
    };
    std::array<Cand, kMaxCand> cands{};
    int cand_n = 0;
    int corr_ticks = 0;

    u32 player_obj = 0;
    u32 player_pos_off = 0;
    // Up to 3 trail targets
    static constexpr int kMaxTrail = 3;
    std::array<u32, kMaxTrail> trail_base{};
    std::array<u32, kMaxTrail> trail_off{};
    int trail_n = 0;
    float last_px = 0.f, last_py = 0.f, last_pz = 0.f;
    bool have_player = false;

    u32 scan_cursor = 0;
    int fail_cooldown = 0;

    std::unique_ptr<Input::AnalogDevice> circle_pad;

    void EnsurePad();
    void SeedCandidates(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam_slot);
    void ScoreCandidates(Memory::MemorySystem& mem, Kernel::Process& process, float pad_x,
                         float pad_y);
    void TryFindTrailsBudgeted(Memory::MemorySystem& mem, Kernel::Process& process);
    void PushHist(float x, float y, float z);
    bool ReadPos(Memory::MemorySystem& mem, Kernel::Process& process, u32 base, u32 off, float& x,
                 float& y, float& z) const;
    void WritePos(Memory::MemorySystem& mem, Kernel::Process& process, u32 base, u32 off, float x,
                  float y, float z);
    bool LooksPos(float x, float y, float z) const;
};

} // namespace Hoenn
