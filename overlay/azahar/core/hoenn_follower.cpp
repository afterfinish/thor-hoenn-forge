// Copyright Hoenn Forge — experimental overworld follower probe
//
// Rough test only. Must stay cheap: never full-process scan in one tick
// (that froze the game on first enable).

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
constexpr u32 DLL_FOLLOWER_STR_OFF = 0x10B4BA;
constexpr float PAD_DZ = 0.18f;

// Hard budgets per tick — keep emu responsive
constexpr int kApiBytesPerTick = 0x2000;     // 8 KiB string hunt
constexpr int kFollowerBasesPerTick = 48;    // small chunk of heap
constexpr u32 kFollowerStep = 0x80;
constexpr u32 kFollowerWindow = 0x8000;      // ±32 KiB around player only

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
    api_done = false;
    api_name_addr = 0;
    api_scan_cursor = 0;
    have_player = false;
    player_obj = 0;
    follow_obj = 0;
    hist_n = 0;
    hist_i = 0;
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
        LOG_WARNING(Core, "Hoenn follower: DllField @{:08X}", dllfield_base);
    }
}

void FollowerProbe::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        dllfield_base = 0;
        have_player = false;
        follow_obj = 0;
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
    player_obj = follow_obj = 0;
    hist_n = hist_i = 0;
    scan_cursor = 0;
    fail_cooldown = 0;
    // Do NOT reset api_done — keep any prior string hit; never re-blast full scan
    LOG_WARNING(Core, "Hoenn follower probe {} (budgeted, no full-scan)",
                enabled ? "ON" : "OFF");
}

bool FollowerProbe::IsEnabled() const {
    return enabled;
}

std::string FollowerProbe::StatusLine() const {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "fol=%d pl=%08X fo=%08X hist=%d dll=%08X", enabled ? 1 : 0,
                  player_obj, follow_obj, hist_n, dllfield_base);
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

// Optional API-name log only. 8 KiB/tick, never blocks.
void FollowerProbe::MaybeScanApiName(Memory::MemorySystem& mem, Kernel::Process& process) {
    if (api_done || dllfield_base == 0) {
        // If we already have dll base from CRO load, try fixed file offset once
        if (!api_done && dllfield_base != 0 && api_name_addr == 0) {
            const u32 a = dllfield_base + DLL_FOLLOWER_STR_OFF;
            const char* needle = "GetPlayerFollowerGridX";
            bool match = true;
            for (u32 i = 0; i < 22; ++i) {
                if (static_cast<char>(mem.Read8(process, a + i)) != needle[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                api_name_addr = a;
                LOG_WARNING(Core, "Hoenn follower: API name @{:08X} (via dll base)", a);
            }
            api_done = true; // one shot only
        }
        return;
    }
    // No dll base: tiny budgeted scan in code region (optional)
    constexpr u32 kLo = 0x00100000;
    constexpr u32 kHi = 0x00400000; // 3 MiB max, chunked
    if (api_scan_cursor < kLo) {
        api_scan_cursor = kLo;
    }
    if (api_scan_cursor >= kHi) {
        api_done = true;
        return;
    }
    const char* needle = "GetPlayerFollowerGridX";
    const u32 end = std::min(api_scan_cursor + static_cast<u32>(kApiBytesPerTick), kHi);
    for (u32 a = api_scan_cursor; a + 22 < end; a += 4) {
        if (static_cast<char>(mem.Read8(process, a)) != 'G') {
            continue;
        }
        bool match = true;
        for (u32 i = 1; i < 22; ++i) {
            if (static_cast<char>(mem.Read8(process, a + i)) != needle[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            api_name_addr = a;
            if (a >= DLL_FOLLOWER_STR_OFF) {
                dllfield_base = a - DLL_FOLLOWER_STR_OFF;
            }
            LOG_WARNING(Core, "Hoenn follower: API name @{:08X} dll={:08X}", a, dllfield_base);
            api_done = true;
            return;
        }
    }
    api_scan_cursor = end;
    if (api_scan_cursor >= kHi) {
        api_done = true;
    }
}

// Cheap player bootstrap: only around camera slot + tiny gold band
void FollowerProbe::TryBootstrapPlayer(Memory::MemorySystem& mem, Kernel::Process& process,
                                       u32 cam_slot) {
    auto try_base = [&](u32 base) -> bool {
        if (!HeapPtr(base)) {
            return false;
        }
        // Common world-pos offsets only (not full 0x1C0 grid)
        static constexpr u32 kOffs[] = {0x00, 0x0C, 0x10, 0x20, 0x30, 0xC0, 0xD0, 0xE0, 0xF0,
                                        0x100, 0x110, 0x120};
        for (u32 off : kOffs) {
            float x, y, z;
            if (ReadPos(mem, process, base, off, x, y, z)) {
                player_obj = base;
                player_pos_off = off;
                last_px = x;
                last_py = y;
                last_pz = z;
                have_player = true;
                LOG_WARNING(Core,
                            "Hoenn follower: player-ish @{:08X}+{:03X} ({:.1f},{:.1f},{:.1f})",
                            base, off, x, y, z);
                return true;
            }
        }
        return false;
    };

    // Cam object itself + neighbors (very small)
    if (try_base(cam_slot)) {
        return;
    }
    for (int d = -0x200; d <= 0x200; d += 0x40) {
        if (d == 0) {
            continue;
        }
        if (try_base(cam_slot + static_cast<u32>(d))) {
            return;
        }
    }
    // Tiny known gold band sample (not full 64 KiB)
    for (u32 base = 0x082D3000; base < 0x082D5000; base += 0x80) {
        if (try_base(base)) {
            return;
        }
    }
}

// Budgeted follower search: a few bases per tick around player
void FollowerProbe::TryFindFollowerBudgeted(Memory::MemorySystem& mem, Kernel::Process& process) {
    if (!have_player || follow_obj != 0) {
        return;
    }
    if (fail_cooldown > 0) {
        fail_cooldown--;
        return;
    }

    const u32 lo = player_obj > kFollowerWindow ? player_obj - kFollowerWindow : 0x08000000;
    const u32 hi = std::min(player_obj + kFollowerWindow, 0x0BFFFFFFu);
    if (scan_cursor < lo || scan_cursor >= hi) {
        scan_cursor = lo;
    }

    static constexpr u32 kOffs[] = {0x00, 0x0C, 0x10, 0x20, 0x30, 0xC0, 0xE0, 0x100, 0x120};
    float best_dist = 1.0e9f;
    u32 best_base = 0;
    u32 best_off = 0;

    for (int n = 0; n < kFollowerBasesPerTick; ++n) {
        const u32 base = scan_cursor;
        scan_cursor += kFollowerStep;
        if (scan_cursor >= hi) {
            scan_cursor = lo;
            // Full window pass done with no hit — cool down so we don't spin
            fail_cooldown = 30; // ~0.5s at 60Hz schedule
            break;
        }
        if (base == player_obj || !HeapPtr(base)) {
            continue;
        }
        for (u32 off : kOffs) {
            float x, y, z;
            if (!ReadPos(mem, process, base, off, x, y, z)) {
                continue;
            }
            const float dist = std::fabs(x - last_px) + std::fabs(z - last_pz);
            if (dist < 20.f || dist > 3000.f) {
                continue;
            }
            if (dist < best_dist) {
                best_dist = dist;
                best_base = base;
                best_off = off;
            }
        }
    }

    if (best_base != 0) {
        follow_obj = best_base;
        follow_pos_off = best_off;
        LOG_WARNING(Core, "Hoenn follower: trail candidate @{:08X}+{:03X} dist≈{:.0f}", follow_obj,
                    follow_pos_off, best_dist);
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

    // Optional, budgeted — never freezes
    if ((tick_n % 4) == 0) {
        MaybeScanApiName(mem, *process);
    }

    float pad_x = 0.f, pad_y = 0.f;
    try {
        if (circle_pad) {
            std::tie(pad_x, pad_y) = circle_pad->GetStatus();
        }
    } catch (...) {
        circle_pad.reset();
    }
    const bool pad_active = std::fabs(pad_x) >= PAD_DZ || std::fabs(pad_y) >= PAD_DZ;

    const u32 slot = mem.Read32(*process, CAMERA_SLOT);
    if (!HeapPtr(slot)) {
        return;
    }

    if (!have_player) {
        // Once per few ticks only
        if ((tick_n % 8) == 0) {
            TryBootstrapPlayer(mem, *process, slot);
        }
        tick_n++;
        return;
    }

    // Cheap path update
    float px, py, pz;
    if (ReadPos(mem, *process, player_obj, player_pos_off, px, py, pz)) {
        const float d = std::fabs(px - last_px) + std::fabs(pz - last_pz);
        if (d > 0.5f || pad_active) {
            PushHist(px, py, pz);
        }
        last_px = px;
        last_py = py;
        last_pz = pz;
    } else {
        // Lost player lock — re-bootstrap later
        have_player = false;
        player_obj = 0;
        follow_obj = 0;
        tick_n++;
        return;
    }

    if (follow_obj == 0) {
        TryFindFollowerBudgeted(mem, *process);
    }

    // Trail write only — cheap
    if (follow_obj && hist_n >= 4) {
        int idx = hist_i - 4;
        while (idx < 0) {
            idx += kHist;
        }
        const float tx = hx[static_cast<size_t>(idx)];
        const float ty = hy[static_cast<size_t>(idx)];
        const float tz = hz[static_cast<size_t>(idx)];
        WritePos(mem, *process, follow_obj, follow_pos_off, tx, ty, tz);
    }

    if ((tick_n++ % 120) == 0) {
        LOG_INFO(Core,
                 "Hoenn follower tick pl={:08X}+{:03X} fo={:08X}+{:03X} hist={} "
                 "pos=({:.0f},{:.0f},{:.0f})",
                 player_obj, player_pos_off, follow_obj, follow_pos_off, hist_n, last_px, last_py,
                 last_pz);
    }
}

} // namespace Hoenn
