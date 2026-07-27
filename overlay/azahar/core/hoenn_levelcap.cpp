// Copyright Hoenn Forge — ORAS hardcore-nuzlocke level cap
//
// Holds every party Pokemon at or below the cap for the boss you are about to fight.
// The cap ladder is the standard ORAS hardcore-nuzlocke one: eight gyms, then the Elite
// Four one member at a time, then the champion.
//
// Nothing here patches game code. Once per cheat-engine tick we locate the party in the
// guest heap, and for any slot whose experience has run past the cap we write it back to
// the first experience value of the cap level. That is the whole mechanism: a Pokemon at
// the cap keeps battling and keeps winning, it just never banks the experience.
//
// The party is found by sweeping the heap for a structure that looks like a PK6 party
// slot and then proving it, rather than by hardcoding an address. A hardcoded address
// would be one more build-specific constant to break, and — unlike the camera slot — a
// wrong guess here writes over somebody's save file.

#include "core/hoenn_levelcap.h"

#include <algorithm>
#include <array>
#include <vector>

#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"

namespace Hoenn {

namespace {

// PK6 party slot. Offsets are the decrypted, unshuffled layout — the form the game works
// in while playing. The save file stores the same structure with the four 56-byte blocks
// shuffled and XOR'd; if ORAS turns out to keep that form live in RAM instead, validation
// simply never passes and the module stays inert rather than writing nonsense.
constexpr u32 kSlotSize = 0x104;
constexpr u32 kStoredSize = 0xE8;
constexpr u32 OFF_SANITY = 0x04;
constexpr u32 OFF_CHECKSUM = 0x06;
constexpr u32 OFF_BLOCKS = 0x08;
constexpr u32 OFF_SPECIES = 0x08;
constexpr u32 OFF_EXP = 0x10;
constexpr u32 OFF_LEVEL = 0xEC;
constexpr u32 OFF_HP_CUR = 0xF0;
constexpr u32 OFF_HP_MAX = 0xF2;
constexpr std::array<u32, 5> kStatOffsets = {0xF4, 0xF6, 0xF8, 0xFA, 0xFC};

// Blissey tops out at 714 HP. Nothing legitimate reaches four digits, and the bound is
// what makes five consecutive stat reads a ~1-in-a-billion filter against random heap.
constexpr u16 kMaxStat = 1000;

// The whole guest heap. An earlier version stopped at 0x0A000000 on the reasoning that the
// save block would sit near the camera object at 0x085F67DC; that was a guess dressed up as
// an optimisation, and unmapped pages are skipped so cheaply that the full range costs
// almost nothing. Do not narrow this again without evidence.
constexpr u32 kHeapLo = 0x08000000;
constexpr u32 kHeapHi = 0x0C000000;
constexpr u32 kPageSize = 0x1000;
// Bytes swept per tick. The sweep reads whole pages into a local buffer, so this is cheap
// enough to finish the range in a couple of seconds without a visible hitch.
constexpr u32 kScanBytesPerTick = 0x400000;

// Plausible-looking slots to describe in full before falling back to counting them. A
// structure that passes species and level but fails later is the single most useful thing
// to see when the layout assumption is in doubt.
constexpr u32 kNearMissLogLimit = 8;

// Stop repeating the same observe-mode line forever; the first handful prove the point.
constexpr u32 kObserveLogLimit = 12;

// Our own cadence, independent of the cheat engine's. Free look pulls that event up to
// ~60 Hz while the stick is live, and sweeping two megabytes sixty times a second would
// be a real cost for a check that only needs to beat the end of a battle.
constexpr u64 kTickInterval = 50'000'000;

constexpr std::array<int, LevelCap::kStages> kCaps = {14, 16, 21, 28, 30, 35, 45,
                                                      46, 52, 53, 54, 55, 59};

u32 Cube(int n) {
    return static_cast<u32>(n) * static_cast<u32>(n) * static_cast<u32>(n);
}

} // namespace

int CapForStage(int stage) {
    return kCaps[static_cast<std::size_t>(std::clamp(stage, 0, LevelCap::kStages - 1))];
}

u32 ExpForLevel(u8 rate, int level) {
    if (level <= 1) {
        return 0;
    }
    const int n = std::min(level, 100);
    const u32 n3 = Cube(n);
    switch (rate) {
    case 0: // medium fast
        return n3;
    case 1: // erratic
        if (n < 50) {
            return n3 * static_cast<u32>(100 - n) / 50;
        }
        if (n < 68) {
            return n3 * static_cast<u32>(150 - n) / 100;
        }
        if (n < 98) {
            return n3 * static_cast<u32>((1911 - 10 * n) / 3) / 500;
        }
        return n3 * static_cast<u32>(160 - n) / 100;
    case 2: // fluctuating
        if (n < 15) {
            return n3 * static_cast<u32>((n + 1) / 3 + 24) / 50;
        }
        if (n < 36) {
            return n3 * static_cast<u32>(n + 14) / 50;
        }
        return n3 * static_cast<u32>(n / 2 + 32) / 50;
    case 3: { // medium slow — the only curve that goes negative at low levels
        const s64 v = 6LL * Cube(n) / 5 - 15LL * n * n + 100LL * n - 140;
        return v < 0 ? 0u : static_cast<u32>(v);
    }
    case 4: // fast
        return 4 * n3 / 5;
    case 5: // slow
        return 5 * n3 / 4;
    default:
        return n3;
    }
}

LevelCap& LevelCap::GetInstance() {
    static LevelCap instance;
    return instance;
}

void LevelCap::SetEnabled(bool value) {
    if (enabled == value) {
        return;
    }
    enabled = value;
    if (!enabled) {
        ResetLocation();
    }
}

bool LevelCap::IsEnabled() const {
    return enabled;
}

void LevelCap::SetEnforce(bool value) {
    enforce = value;
}

bool LevelCap::IsEnforcing() const {
    return enforce;
}

void LevelCap::SetStageSource(StageSource source) {
    stage_source = source;
}

LevelCap::StageSource LevelCap::GetStageSource() const {
    return stage_source;
}

void LevelCap::SetStage(int value) {
    stage = std::clamp(value, 0, kStages - 1);
}

int LevelCap::GetStage() const {
    return stage;
}

void LevelCap::AdvanceStage() {
    SetStage(stage + 1);
}

int LevelCap::GetActiveCap() const {
    if (!enabled) {
        return 0;
    }
    // Badge mode has nothing to add yet: the badge word in the save block has not been
    // located, and even once it is it cannot separate the four Elite Four stages. Either
    // way the manual stage is the answer.
    return CapForStage(stage);
}

void LevelCap::SetGrowthTable(const u8* rates, std::size_t count) {
    growth.fill(0);
    if (rates == nullptr || count == 0) {
        growth_loaded = false;
        return;
    }
    const std::size_t n = std::min<std::size_t>(count, growth.size());
    std::copy_n(rates, n, growth.begin());
    growth_loaded = true;
    LOG_INFO(Core_Cheats, "Hoenn level cap: growth table loaded ({} species)", n);
}

bool LevelCap::HasGrowthTable() const {
    return growth_loaded;
}

int LevelCap::GetPartyCount() const {
    return party_count;
}

bool LevelCap::HasParty() const {
    return party_base != 0;
}

u32 LevelCap::GetClampCount() const {
    return clamps;
}

void LevelCap::OnCoreReconnect() {
    ResetLocation();
}

void LevelCap::ResetLocation() {
    party_base = 0;
    party_count = 0;
    scan_cursor = kHeapLo;
    observe_logs = 0;
    scan_plausible = 0;
    scan_near_logs = 0;
    scan_reject.fill(0);
    // A savestate load rewinds the timing counter, so a deadline from the old timeline
    // would park the module until the emulator caught back up.
    next_tick = 0;
}

bool LevelCap::SlotIsEmpty(Memory::MemorySystem& mem, Kernel::Process& process,
                           u32 slot) const {
    return mem.Read16(process, slot + OFF_SPECIES) == 0;
}

bool LevelCap::ResolveGrowth(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot,
                             u8& out_rate) const {
    if (growth_loaded) {
        const u16 species = mem.Read16(process, slot + OFF_SPECIES);
        if (species == 0 || species > kMaxSpecies) {
            return false;
        }
        out_rate = growth[species];
        return out_rate <= 5;
    }
    // No table yet. Accept only when exactly one curve brackets this level/exp pair, so an
    // ambiguous match can never pick a curve and enforce the wrong experience value. This
    // path is for observe mode; enforcement requires the real table.
    const u8 level = mem.Read8(process, slot + OFF_LEVEL);
    const u32 exp = mem.Read32(process, slot + OFF_EXP);
    if (level < 1 || level > 100) {
        return false;
    }
    int hits = 0;
    for (u8 r = 0; r < 6; ++r) {
        const bool above = exp >= ExpForLevel(r, level);
        const bool below = level >= 100 || exp < ExpForLevel(r, level + 1);
        if (above && below) {
            out_rate = r;
            ++hits;
        }
    }
    return hits == 1;
}

bool LevelCap::ValidateSlot(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot,
                            Reject* why) const {
    const auto fail = [why](Reject r) {
        if (why != nullptr) {
            *why = r;
        }
        return false;
    };
    if (why != nullptr) {
        *why = Reject::None;
    }

    if (slot < kHeapLo || slot + kSlotSize > kHeapHi) {
        return fail(Reject::Range);
    }
    if (!mem.IsValidVirtualAddress(process, slot) ||
        !mem.IsValidVirtualAddress(process, slot + kSlotSize - 1)) {
        return fail(Reject::Unmapped);
    }
    const u16 species = mem.Read16(process, slot + OFF_SPECIES);
    if (species == 0 || species > kMaxSpecies) {
        return fail(Reject::Species);
    }
    const u8 level = mem.Read8(process, slot + OFF_LEVEL);
    if (level < 1 || level > 100) {
        return fail(Reject::Level);
    }
    const u16 hp_max = mem.Read16(process, slot + OFF_HP_MAX);
    const u16 hp_cur = mem.Read16(process, slot + OFF_HP_CUR);
    if (hp_max < 1 || hp_max > kMaxStat || hp_cur > hp_max) {
        return fail(Reject::Hp);
    }
    for (const u32 off : kStatOffsets) {
        const u16 stat = mem.Read16(process, slot + off);
        if (stat < 1 || stat > kMaxStat) {
            return fail(Reject::Stats);
        }
    }
    // Zero on every normal Pokemon; the game sets it on a corrupted entry.
    if (mem.Read16(process, slot + OFF_SANITY) != 0) {
        return fail(Reject::Sanity);
    }
    // The real discriminator. Experience, level and the species' growth curve are three
    // independent facts that only agree on an actual Pokemon.
    u8 rate = 0;
    if (!ResolveGrowth(mem, process, slot, rate)) {
        return fail(Reject::Growth);
    }
    const u32 exp = mem.Read32(process, slot + OFF_EXP);
    if (exp < ExpForLevel(rate, level)) {
        return fail(Reject::ExpBelow);
    }
    if (level < 100 && exp >= ExpForLevel(rate, level + 1)) {
        return fail(Reject::ExpAbove);
    }
    return true;
}

int LevelCap::CountParty(Memory::MemorySystem& mem, Kernel::Process& process,
                         u32 base) const {
    int count = 0;
    for (int i = 0; i < kPartySlots; ++i) {
        const u32 slot = base + static_cast<u32>(i) * kSlotSize;
        if (ValidateSlot(mem, process, slot)) {
            ++count;
            continue;
        }
        // The party packs from slot 0, so the first non-Pokemon ends it. Anything that is
        // neither a valid Pokemon nor an empty slot means we are not looking at a party.
        if (!SlotIsEmpty(mem, process, slot)) {
            return i == 0 ? 0 : count;
        }
        break;
    }
    return count;
}

void LevelCap::LogScanPass() {
    ++scan_passes;
    // A pass takes about three seconds. Say it loudly a few times, then back off, because
    // the GPU camera already logs twice a second and will flush this out of the ring buffer.
    if (scan_passes > 3 && scan_passes % 10 != 0) {
        scan_plausible = 0;
        scan_reject.fill(0);
        return;
    }
    LOG_INFO(Core_Cheats, "Hoenn level cap: enabled={} enforce={} growth_table={} stage={} cap={}",
             enabled, enforce, growth_loaded, stage, GetActiveCap());
    LOG_INFO(Core_Cheats,
             "Hoenn level cap: swept {:#x}-{:#x}, pass {}, NO PARTY FOUND. {} plausible slots "
             "rejected at range/unmapped/species/level/hp/stats/sanity/growth/exp-lo/exp-hi = "
             "{}/{}/{}/{}/{}/{}/{}/{}/{}/{}",
             kHeapLo, kHeapHi, scan_passes, scan_plausible,
             scan_reject[static_cast<std::size_t>(Reject::Range)],
             scan_reject[static_cast<std::size_t>(Reject::Unmapped)],
             scan_reject[static_cast<std::size_t>(Reject::Species)],
             scan_reject[static_cast<std::size_t>(Reject::Level)],
             scan_reject[static_cast<std::size_t>(Reject::Hp)],
             scan_reject[static_cast<std::size_t>(Reject::Stats)],
             scan_reject[static_cast<std::size_t>(Reject::Sanity)],
             scan_reject[static_cast<std::size_t>(Reject::Growth)],
             scan_reject[static_cast<std::size_t>(Reject::ExpBelow)],
             scan_reject[static_cast<std::size_t>(Reject::ExpAbove)]);
    scan_plausible = 0;
    scan_reject.fill(0);
    // Let the next pass describe fresh near misses; the party may not have existed yet on
    // the first one.
    scan_near_logs = 0;
}

bool LevelCap::ScanForParty(Memory::MemorySystem& mem, Kernel::Process& process) {
    std::vector<u8> page(kPageSize);
    u32 swept = 0;

    while (swept < kScanBytesPerTick) {
        if (scan_cursor + kPageSize > kHeapHi) {
            scan_cursor = kHeapLo;
            LogScanPass();
            return false;
        }
        const u32 base = scan_cursor;
        scan_cursor += kPageSize;
        swept += kPageSize;

        if (!mem.IsValidVirtualAddress(process, base)) {
            continue;
        }
        mem.ReadBlock(process, base, page.data(), kPageSize);

        // Only starts whose whole slot fits inside this page are considered. A party is
        // six contiguous slots spanning more than a page, so at least one of them always
        // starts in the scannable part of some page; the walk-back below finds the rest.
        for (u32 off = 0; off + kSlotSize <= kPageSize; off += 4) {
            const u16 species =
                static_cast<u16>(page[off + OFF_SPECIES] | (page[off + OFF_SPECIES + 1] << 8));
            if (species == 0 || species > kMaxSpecies) {
                continue;
            }
            const u8 level = page[off + OFF_LEVEL];
            if (level < 1 || level > 100) {
                continue;
            }
            const u32 candidate = base + off;
            // Past the species and level gates this is worth describing, whatever happens
            // next: it is either the party or the closest thing in the heap to it.
            Reject why = Reject::None;
            if (!ValidateSlot(mem, process, candidate, &why)) {
                ++scan_plausible;
                scan_reject[static_cast<std::size_t>(why)]++;
                if (scan_near_logs < kNearMissLogLimit) {
                    ++scan_near_logs;
                    u8 rate = 0;
                    const bool have_rate = ResolveGrowth(mem, process, candidate, rate);
                    LOG_INFO(Core_Cheats,
                             "Hoenn level cap: near miss @ {:#010x} rejected at {} — species {} "
                             "lv {} exp {} hp {}/{} stats {}/{}/{}/{}/{} sanity {:#06x} "
                             "chk {:#06x} curve {}",
                             candidate, static_cast<int>(why),
                             mem.Read16(process, candidate + OFF_SPECIES),
                             mem.Read8(process, candidate + OFF_LEVEL),
                             mem.Read32(process, candidate + OFF_EXP),
                             mem.Read16(process, candidate + OFF_HP_CUR),
                             mem.Read16(process, candidate + OFF_HP_MAX),
                             mem.Read16(process, candidate + kStatOffsets[0]),
                             mem.Read16(process, candidate + kStatOffsets[1]),
                             mem.Read16(process, candidate + kStatOffsets[2]),
                             mem.Read16(process, candidate + kStatOffsets[3]),
                             mem.Read16(process, candidate + kStatOffsets[4]),
                             mem.Read16(process, candidate + OFF_SANITY),
                             mem.Read16(process, candidate + OFF_CHECKSUM),
                             have_rate ? static_cast<int>(rate) : -1);
                }
                continue;
            }
            // Walk back to slot 0 — we may have landed on any member of the party.
            u32 first = candidate;
            while (first >= kHeapLo + kSlotSize &&
                   ValidateSlot(mem, process, first - kSlotSize)) {
                first -= kSlotSize;
            }
            const int count = CountParty(mem, process, first);
            if (count < 1) {
                continue;
            }
            party_base = first;
            party_count = count;
            LOG_INFO(Core_Cheats, "Hoenn level cap: party found at {:#010x}, {} member(s)",
                     party_base, party_count);
            LogParty(mem, process);
            return true;
        }
    }
    return false;
}

void LevelCap::LogParty(Memory::MemorySystem& mem, Kernel::Process& process) const {
    for (int i = 0; i < party_count; ++i) {
        const u32 slot = party_base + static_cast<u32>(i) * kSlotSize;
        u8 rate = 0;
        const bool have_rate = ResolveGrowth(mem, process, slot, rate);
        LOG_INFO(Core_Cheats,
                 "Hoenn level cap: slot {} @ {:#010x} species {} lv {} exp {} hp {}/{} "
                 "curve {}",
                 i, slot, mem.Read16(process, slot + OFF_SPECIES),
                 mem.Read8(process, slot + OFF_LEVEL), mem.Read32(process, slot + OFF_EXP),
                 mem.Read16(process, slot + OFF_HP_CUR), mem.Read16(process, slot + OFF_HP_MAX),
                 have_rate ? static_cast<int>(rate) : -1);
    }
}

void LevelCap::RewriteChecksum(Memory::MemorySystem& mem, Kernel::Process& process,
                               u32 slot) {
    std::array<u8, kStoredSize - OFF_BLOCKS> blocks{};
    mem.ReadBlock(process, slot + OFF_BLOCKS, blocks.data(), blocks.size());
    u16 sum = 0;
    for (std::size_t i = 0; i + 1 < blocks.size(); i += 2) {
        sum = static_cast<u16>(sum + (blocks[i] | (blocks[i + 1] << 8)));
    }
    mem.Write16(process, slot + OFF_CHECKSUM, sum);
}

void LevelCap::ApplyToSlot(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot,
                           int cap, bool armed) {
    u8 rate = 0;
    if (!ResolveGrowth(mem, process, slot, rate)) {
        return;
    }
    const u8 level = mem.Read8(process, slot + OFF_LEVEL);
    const u32 exp = mem.Read32(process, slot + OFF_EXP);
    const u32 cap_exp = ExpForLevel(rate, cap);
    if (level <= cap && exp <= cap_exp) {
        return;
    }

    if (!armed) {
        if (observe_logs < kObserveLogLimit) {
            ++observe_logs;
            LOG_INFO(Core_Cheats,
                     "Hoenn level cap (observe): slot {:#010x} lv{} exp {} would clamp to "
                     "lv{} exp {}",
                     slot, level, exp, cap, cap_exp);
        }
        return;
    }

    // Only maintain the checksum if the copy in RAM is keeping one. If it is stale while
    // playing, the game refreshes it on save and our write would be the wrong value.
    std::array<u8, kStoredSize - OFF_BLOCKS> blocks{};
    mem.ReadBlock(process, slot + OFF_BLOCKS, blocks.data(), blocks.size());
    u16 sum = 0;
    for (std::size_t i = 0; i + 1 < blocks.size(); i += 2) {
        sum = static_cast<u16>(sum + (blocks[i] | (blocks[i + 1] << 8)));
    }
    const bool checksum_live = sum == mem.Read16(process, slot + OFF_CHECKSUM);

    mem.Write32(process, slot + OFF_EXP, cap_exp);
    if (level > cap) {
        // Rare Candy, a trade, or a catch above the cap. Stats stay high until the game
        // next recalculates them, which is cosmetic and self-correcting; rebuilding them
        // here would mean reading IVs, EVs and nature and trusting all three.
        mem.Write8(process, slot + OFF_LEVEL, static_cast<u8>(cap));
    }
    if (checksum_live) {
        RewriteChecksum(mem, process, slot);
    }
    ++clamps;
    LOG_INFO(Core_Cheats, "Hoenn level cap: slot {:#010x} clamped lv{} exp {} -> lv{} exp {}",
             slot, level, exp, cap, cap_exp);
}

void LevelCap::Tick(Core::System& system, u32 process_id) {
    if (!enabled) {
        return;
    }
    auto process = system.Kernel().GetProcessById(process_id);
    if (!process) {
        process = system.Kernel().GetCurrentProcess();
    }
    if (!process) {
        return;
    }
    if (last_process_id != process_id) {
        last_process_id = process_id;
        ResetLocation();
    }

    const u64 now = system.CoreTiming().GetTicks();
    if (now < next_tick) {
        return;
    }
    next_tick = now + kTickInterval;

    auto& mem = system.Memory();

    if (party_base == 0) {
        if (!ScanForParty(mem, *process)) {
            return;
        }
    }

    // Re-prove the cached location every pass. Cheap, and it means a heap reshuffle
    // demotes us to a fresh sweep instead of writing to whatever moved in.
    if (!ValidateSlot(mem, *process, party_base)) {
        LOG_INFO(Core_Cheats, "Hoenn level cap: party at {:#010x} no longer validates, rescanning",
                 party_base);
        ResetLocation();
        return;
    }
    party_count = CountParty(mem, *process, party_base);

    const int cap = GetActiveCap();
    if (cap <= 0) {
        return;
    }
    // Enforcement needs the real growth table. Inferring a curve from one level/exp pair
    // is good enough to recognise a Pokemon but not to decide what to write to it.
    const bool armed = enforce && growth_loaded;
    for (int i = 0; i < party_count; ++i) {
        ApplyToSlot(mem, *process, party_base + static_cast<u32>(i) * kSlotSize, cap, armed);
    }
}

} // namespace Hoenn
