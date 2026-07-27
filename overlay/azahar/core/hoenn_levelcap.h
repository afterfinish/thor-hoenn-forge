// Copyright Hoenn Forge — ORAS hardcore-nuzlocke level cap
#pragma once

#include <array>
#include <cstddef>
#include "common/common_types.h"

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
 * Hardcore-nuzlocke level cap.
 *
 * The rule: a party Pokemon at the current cap gains no experience, and anything that
 * pushes it past the cap (Rare Candy, a trade, a high-level catch) is pulled back down.
 * Caps follow the standard ORAS ladder — the eight gyms, then the Elite Four one member
 * at a time, then the champion.
 *
 * Enforcement is a periodic pass over the party in guest RAM, driven by the same cheat
 * engine event that drives Hoenn::FreeCam. There is no code patch and nothing is written
 * outside a party slot that has passed every structural check below.
 *
 * Two deliberate safety properties, both learned the hard way from the zoom-assist bug
 * that wrote an FOV over a guest pointer:
 *
 *  1. **Fail open.** Any uncertainty — party not located, growth curve unknown, stage
 *     unknown — means we do nothing at all. A cap that silently stops applying is a
 *     disappointing run; a cap that writes to the wrong address is a destroyed save.
 *  2. **Observe before enforce.** SetEnforce(false) runs the whole pipeline and logs what
 *     it would have written without touching guest memory, so the addresses can be proved
 *     against a real save before anything is armed.
 */
class LevelCap {
public:
    /// Eight gyms, four Elite Four members, champion.
    static constexpr int kStages = 13;
    static constexpr int kPartySlots = 6;
    /// National dex size in gen 6. Species ids above this are corrupt or not a Pokemon.
    static constexpr u16 kMaxSpecies = 721;

    /// Where the current stage comes from.
    enum class StageSource {
        /// The player advances the cap themselves as they beat each boss. The only source
        /// that can express the Elite Four, which are not badges.
        Manual = 0,
        /// Read the badge count out of the save block. Convenience only, and it can never
        /// carry past the eighth badge — see kBadgeStages.
        Badges = 1,
    };

    /// Stages a badge count can express. Past this the Elite Four are indistinguishable
    /// from each other by badges alone, so Badges mode defers to the manual stage.
    static constexpr int kBadgeStages = 8;

    static LevelCap& GetInstance();

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    /// False = observe only: locate, validate and log, but never write guest memory.
    void SetEnforce(bool enforce);
    bool IsEnforcing() const;

    void SetStageSource(StageSource source);
    StageSource GetStageSource() const;

    /// 0-based, clamped to [0, kStages-1]. Stage 0 is "no bosses beaten yet", capped at
    /// the first gym's level.
    void SetStage(int stage);
    int GetStage() const;
    void AdvanceStage();

    /// The cap in force right now, or 0 when the module cannot determine one.
    int GetActiveCap() const;

    /**
     * Experience-growth id per species, indexed by national dex number, as numbered by
     * the personal table (0 medium fast, 1 erratic, 2 fluctuating, 3 medium slow, 4 fast,
     * 5 slow). Extracted from the personal GARC during prepare so it stays correct even
     * when the personal randomizer has been run. Index 0 is unused padding.
     */
    void SetGrowthTable(const u8* rates, std::size_t count);
    bool HasGrowthTable() const;

    /// Party slots that passed validation on the last pass. Diagnostics for the quick menu.
    int GetPartyCount() const;
    bool HasParty() const;
    /// Clamps applied since the core came up. Diagnostics for the quick menu.
    u32 GetClampCount() const;

    void Tick(Core::System& system, u32 process_id);
    void OnCoreReconnect();

private:
    LevelCap() = default;

    bool enabled = false;
    bool enforce = false;
    StageSource stage_source = StageSource::Manual;
    int stage = 0;

    std::array<u8, kMaxSpecies + 1> growth{};
    bool growth_loaded = false;

    /// Base address of party slot 0 once found, else 0. The save block is allocated once
    /// and does not move across map transitions, so this survives for the session.
    u32 party_base = 0;
    int party_count = 0;
    u32 scan_cursor = 0;
    u32 last_process_id = 0;
    u64 next_tick = 0;
    u32 clamps = 0;
    /// Suppresses the per-slot observe log once it has said the same thing enough times.
    u32 observe_logs = 0;

    void ResetLocation();
    /// One bounded slice of the heap sweep. Returns true once party_base is set.
    bool ScanForParty(Memory::MemorySystem& mem, Kernel::Process& process);
    /// Every structural check a candidate slot must pass before we believe it, let alone
    /// write to it.
    bool ValidateSlot(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot) const;
    bool SlotIsEmpty(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot) const;
    int CountParty(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const;
    /// Growth curve for a slot: the table if loaded, else inferred from the level/exp pair
    /// when exactly one curve fits. Returns false when it cannot be pinned down.
    bool ResolveGrowth(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot,
                       u8& out_rate) const;
    /// @param armed write for real; false logs what it would have written and returns.
    void ApplyToSlot(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot, int cap,
                     bool armed);
    /// Re-sums the PK6 checksum over the encrypted-block range and writes it back. Only
    /// called when the checksum matched before our write, i.e. the copy in RAM keeps one.
    void RewriteChecksum(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot);
};

/// First experience value of `level` on growth curve `rate`, as the personal table
/// numbers the curves. Exposed for tests and for the party validator.
u32 ExpForLevel(u8 rate, int level);

/// The ORAS ladder, indexed by stage. Exposed so the UI can label the stages it offers.
int CapForStage(int stage);

} // namespace Hoenn
