// Copyright Hoenn Forge — experimental overworld follower probe
//
// Party lead species comes from Kotlin (save PK6). Native does budgeted
// pad-correlated player lock + path-lag thrash of nearby world objects.
// Sprite ghost is drawn in Java on the bottom screen.

#include "core/hoenn_follower.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"

namespace Hoenn {

namespace {
constexpr VAddr CAMERA_SLOT = 0x085F67DC;
constexpr float PAD_DZ = 0.18f;
constexpr int kCandSeedPerTick = 8;
constexpr int kTrailBasesPerTick = 32;
constexpr u32 kTrailStep = 0x80;
constexpr u32 kTrailWindow = 0x6000;

bool HeapPtr(u32 p) {
    return p >= 0x08000000 && p < 0x0C000000;
}

bool OkF(float f) {
    return std::isfinite(f) && std::fabs(f) < 1.0e7f;
}

float BFloat(u32 b) {
    float f = 0.f;
    std::memcpy(&f, &b, 4);
    return f;
}

u32 FBits(float f) {
    u32 b = 0;
    std::memcpy(&b, &f, 4);
    return b;
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
} // namespace

FollowerProbe& FollowerProbe::GetInstance() {
    static FollowerProbe i;
    return i;
}

void FollowerProbe::EnsurePad() {
    if (circle_pad) {
        return;
    }
    std::string param =
        Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CirclePad];
    if (param.empty() || param.find("null") != std::string::npos) {
        param = "engine:gamepad,code:0";
    }
    circle_pad = Input::CreateDevice<Input::AnalogDevice>(param);
}

void FollowerProbe::OnCoreReconnect() {
    dllfield_base = 0;
    have_player = false;
    player_obj = 0;
    trail_n = 0;
    hist_n = 0;
    hist_i = 0;
    cand_n = 0;
    corr_ticks = 0;
    scan_cursor = 0;
    fail_cooldown = 0;
    LOG_INFO(Core, "Hoenn follower: core reconnect");
}

void FollowerProbe::OnModuleLoaded(std::string_view name, u32 load_address) {
    if (name == "DllBattle") {
        in_battle = true;
        return;
    }
    if (name == "DllField") {
        in_battle = false;
        if (load_address != 0) {
            dllfield_base = load_address;
        }
        LOG_WARNING(Core, "Hoenn follower: DllField @{:08X} party_sp={}", dllfield_base,
                    party_lead_species);
    }
}

void FollowerProbe::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        dllfield_base = 0;
        have_player = false;
        trail_n = 0;
    } else if (name == "DllBattle") {
        in_battle = false;
    }
}

void FollowerProbe::SetEnabled(bool e) {
    if (enabled == e) {
        return;
    }
    enabled = e;
    have_player = false;
    player_obj = 0;
    trail_n = 0;
    hist_n = hist_i = 0;
    cand_n = 0;
    corr_ticks = 0;
    scan_cursor = 0;
    fail_cooldown = 0;
    LOG_WARNING(Core, "Hoenn follower probe {} party_sp={}", enabled ? "ON" : "OFF",
                party_lead_species);
}

bool FollowerProbe::IsEnabled() const {
    return enabled;
}

void FollowerProbe::SetPartyLeadSpecies(int species) {
    party_lead_species = (species >= 1 && species <= 721) ? species : 0;
    LOG_WARNING(Core, "Hoenn follower: party lead species={}", party_lead_species);
}

int FollowerProbe::GetPartyLeadSpecies() const {
    return party_lead_species;
}

std::string FollowerProbe::StatusLine() const {
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "fol=%d sp=%d pl=%08X trails=%d hist=%d pad=%.2f,%.2f", enabled ? 1 : 0,
                  party_lead_species, player_obj, trail_n, hist_n, last_pad_x, last_pad_y);
    return std::string(buf);
}

bool FollowerProbe::LooksPos(float x, float y, float z) const {
    if (!OkF(x) || !OkF(y) || !OkF(z)) {
        return false;
    }
    const float ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
    const float m = std::max({ax, ay, az});
    if (m < 5.f || m > 5.0e5f) {
        return false;
    }
    if (std::fabs(ay - 1.f) < 0.01f && m < 5.f) {
        return false;
    }
    if (std::fabs(ax - 32.f) < 0.1f && std::fabs(az - 30.f) < 0.1f) {
        return false;
    }
    return true;
}

bool FollowerProbe::ReadPos(Memory::MemorySystem& mem, Kernel::Process& process, u32 base, u32 off,
                            float& x, float& y, float& z) const {
    if (!HeapPtr(base) || base + off + 12 >= 0x0C000000) {
        return false;
    }
    x = BFloat(mem.Read32(process, base + off));
    y = BFloat(mem.Read32(process, base + off + 4));
    z = BFloat(mem.Read32(process, base + off + 8));
    return LooksPos(x, y, z);
}

void FollowerProbe::WritePos(Memory::MemorySystem& mem, Kernel::Process& process, u32 base, u32 off,
                             float x, float y, float z) {
    if (!HeapPtr(base) || base + off + 12 >= 0x0C000000) {
        return;
    }
    mem.Write32(process, base + off, FBits(x));
    mem.Write32(process, base + off + 4, FBits(y));
    mem.Write32(process, base + off + 8, FBits(z));
}

void FollowerProbe::PushHist(float x, float y, float z) {
    hx[static_cast<size_t>(hist_i)] = x;
    hy[static_cast<size_t>(hist_i)] = y;
    hz[static_cast<size_t>(hist_i)] = z;
    hist_i = (hist_i + 1) % kHist;
    if (hist_n < kHist) {
        hist_n++;
    }
}

void FollowerProbe::SeedCandidates(Memory::MemorySystem& mem, Kernel::Process& process,
                                   u32 cam_slot) {
    if (cand_n >= kMaxCand) {
        return;
    }
    static constexpr u32 kOffs[] = {0x00, 0x0C, 0x10, 0x20, 0x30, 0xC0, 0xE0, 0x100, 0x120};
    auto try_add = [&](u32 base, u32 off) {
        if (cand_n >= kMaxCand) {
            return;
        }
        float x, y, z;
        if (!ReadPos(mem, process, base, off, x, y, z)) {
            return;
        }
        for (int i = 0; i < cand_n; ++i) {
            if (cands[static_cast<size_t>(i)].base == base &&
                cands[static_cast<size_t>(i)].off == off) {
                return;
            }
        }
        auto& c = cands[static_cast<size_t>(cand_n++)];
        c = {base, off, 0.f, x, z, true};
    };

    // Small samples near camera + gold band
    static u32 seed_i = 0;
    const u32 bases[] = {cam_slot,
                         cam_slot > 0x80 ? cam_slot - 0x80 : cam_slot,
                         cam_slot + 0x80,
                         cam_slot + 0x100,
                         0x082D3000u + (seed_i % 16) * 0x80u,
                         0x082D4000u + (seed_i % 8) * 0x100u};
    seed_i++;
    int added = 0;
    for (u32 base : bases) {
        if (!HeapPtr(base) || added >= kCandSeedPerTick) {
            continue;
        }
        for (u32 off : kOffs) {
            if (cand_n >= kMaxCand || added >= kCandSeedPerTick) {
                break;
            }
            const int before = cand_n;
            try_add(base, off);
            if (cand_n > before) {
                added++;
            }
        }
    }
}

void FollowerProbe::ScoreCandidates(Memory::MemorySystem& mem, Kernel::Process& process,
                                    float pad_x, float pad_y) {
    const bool pad_active = std::fabs(pad_x) >= PAD_DZ || std::fabs(pad_y) >= PAD_DZ;
    float best_score = -1.f;
    int best_i = -1;

    for (int i = 0; i < cand_n; ++i) {
        auto& c = cands[static_cast<size_t>(i)];
        float x, y, z;
        if (!ReadPos(mem, process, c.base, c.off, x, y, z)) {
            c.live = false;
            continue;
        }
        c.live = true;
        const float dx = x - c.px;
        const float dz = z - c.pz;
        c.px = x;
        c.pz = z;
        if (!pad_active) {
            continue;
        }
        // Circle pad Y is typically forward; world XZ move should correlate roughly
        const float move = std::sqrt(dx * dx + dz * dz);
        if (move < 0.3f) {
            c.score *= 0.98f;
            continue;
        }
        // Reward movement while stick is held (don't need exact axis match)
        c.score += move;
        if (c.score > best_score) {
            best_score = c.score;
            best_i = i;
        }
    }

    corr_ticks++;
    // Lock after enough stick-walk samples
    if (!have_player && best_i >= 0 && best_score > 8.f && corr_ticks > 15) {
        const auto& c = cands[static_cast<size_t>(best_i)];
        player_obj = c.base;
        player_pos_off = c.off;
        last_px = c.px;
        last_pz = c.pz;
        float y = 0.f;
        float x = 0.f, z = 0.f;
        if (ReadPos(mem, process, player_obj, player_pos_off, x, y, z)) {
            last_py = y;
            last_px = x;
            last_pz = z;
        }
        have_player = true;
        LOG_WARNING(Core,
                    "Hoenn follower: pad-locked player @{:08X}+{:03X} score={:.1f} "
                    "({:.1f},{:.1f},{:.1f}) sp={}",
                    player_obj, player_pos_off, best_score, last_px, last_py, last_pz,
                    party_lead_species);
    }
}

void FollowerProbe::TryFindTrailsBudgeted(Memory::MemorySystem& mem, Kernel::Process& process) {
    if (!have_player || trail_n >= kMaxTrail) {
        return;
    }
    if (fail_cooldown > 0) {
        fail_cooldown--;
        return;
    }

    const u32 lo = player_obj > kTrailWindow ? player_obj - kTrailWindow : 0x08000000;
    const u32 hi = std::min(player_obj + kTrailWindow, 0x0BFFFFFFu);
    if (scan_cursor < lo || scan_cursor >= hi) {
        scan_cursor = lo;
    }

    static constexpr u32 kOffs[] = {0x00, 0x0C, 0x10, 0x20, 0xC0, 0x100};
    for (int n = 0; n < kTrailBasesPerTick && trail_n < kMaxTrail; ++n) {
        const u32 base = scan_cursor;
        scan_cursor += kTrailStep;
        if (scan_cursor >= hi) {
            scan_cursor = lo;
            fail_cooldown = 20;
            break;
        }
        if (base == player_obj || !HeapPtr(base)) {
            continue;
        }
        bool already = false;
        for (int t = 0; t < trail_n; ++t) {
            if (trail_base[static_cast<size_t>(t)] == base) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        for (u32 off : kOffs) {
            float x, y, z;
            if (!ReadPos(mem, process, base, off, x, y, z)) {
                continue;
            }
            const float dist = std::fabs(x - last_px) + std::fabs(z - last_pz);
            if (dist < 25.f || dist > 2500.f) {
                continue;
            }
            trail_base[static_cast<size_t>(trail_n)] = base;
            trail_off[static_cast<size_t>(trail_n)] = off;
            trail_n++;
            LOG_WARNING(Core, "Hoenn follower: trail[{}] @{:08X}+{:03X} dist≈{:.0f} sp={}",
                        trail_n - 1, base, off, dist, party_lead_species);
            break;
        }
    }
}

void FollowerProbe::Tick(Core::System& system, u32 process_id) {
    if (!enabled || in_battle) {
        return;
    }
    if (!system.IsPoweredOn()) {
        return;
    }
    last_process_id = process_id;
    auto process = Resolve(system, process_id);
    if (!process) {
        return;
    }
    auto& mem = system.Memory();
    EnsurePad();

    float pad_x = 0.f, pad_y = 0.f;
    try {
        if (circle_pad) {
            std::tie(pad_x, pad_y) = circle_pad->GetStatus();
        }
    } catch (...) {
        circle_pad.reset();
    }
    last_pad_x = pad_x;
    last_pad_y = pad_y;
    const bool pad_active = std::fabs(pad_x) >= PAD_DZ || std::fabs(pad_y) >= PAD_DZ;

    const u32 slot = mem.Read32(*process, CAMERA_SLOT);
    if (!HeapPtr(slot)) {
        tick_n++;
        return;
    }

    if (!have_player) {
        if ((tick_n % 2) == 0) {
            SeedCandidates(mem, *process, slot);
        }
        ScoreCandidates(mem, *process, pad_x, pad_y);
        tick_n++;
        return;
    }

    float px, py, pz;
    if (ReadPos(mem, *process, player_obj, player_pos_off, px, py, pz)) {
        const float d = std::fabs(px - last_px) + std::fabs(pz - last_pz);
        if (d > 0.4f || pad_active) {
            PushHist(px, py, pz);
        }
        last_px = px;
        last_py = py;
        last_pz = pz;
    } else {
        have_player = false;
        player_obj = 0;
        trail_n = 0;
        cand_n = 0;
        corr_ticks = 0;
        tick_n++;
        return;
    }

    if (trail_n < kMaxTrail) {
        TryFindTrailsBudgeted(mem, *process);
    }

    if (trail_n > 0 && hist_n >= 4) {
        int idx = hist_i - 4;
        while (idx < 0) {
            idx += kHist;
        }
        const float tx = hx[static_cast<size_t>(idx)];
        const float ty = hy[static_cast<size_t>(idx)];
        const float tz = hz[static_cast<size_t>(idx)];
        for (int t = 0; t < trail_n; ++t) {
            WritePos(mem, *process, trail_base[static_cast<size_t>(t)],
                     trail_off[static_cast<size_t>(t)], tx, ty, tz);
        }
    }

    if ((tick_n++ % 120) == 0) {
        LOG_INFO(Core,
                 "Hoenn follower tick sp={} pl={:08X}+{:03X} trails={} hist={} "
                 "pos=({:.0f},{:.0f},{:.0f}) pad=({:.2f},{:.2f})",
                 party_lead_species, player_obj, player_pos_off, trail_n, hist_n, last_px, last_py,
                 last_pz, last_pad_x, last_pad_y);
    }
}

} // namespace Hoenn
