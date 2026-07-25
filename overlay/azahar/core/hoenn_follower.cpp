// Copyright Hoenn Forge — experimental overworld follower probe
//
// Rough test only. Does NOT implement HG/SS-quality following.
// Strategy:
//  1) Confirm DllField + GetPlayerFollower* API name table is mapped.
//  2) Discover a world-pos triple that moves with circle pad (= player-ish).
//  3) Discover a second triple nearby (NPC / leftover model).
//  4) Write lagged player positions into that second object each tick.
// If anything trails you, the field model pipeline is writable enough to build on.

#include "core/hoenn_follower.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"

namespace Hoenn {

namespace {
constexpr VAddr CAMERA_SLOT = 0x085F67DC;
// File offset of GetPlayerFollowerGridX string in DllField.cro
constexpr u32 DLL_FOLLOWER_STR_OFF = 0x10B4BA;
constexpr float PAD_DZ = 0.18f;

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
    api_logged = false;
    api_name_addr = 0;
    have_player = false;
    player_obj = 0;
    follow_obj = 0;
    hist_n = 0;
    hist_i = 0;
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
        api_logged = false;
        LOG_WARNING(Core, "Hoenn follower: DllField @{:08X}", dllfield_base);
    }
}

void FollowerProbe::OnModuleUnloaded(std::string_view name) {
    if (name == "DllField") {
        dllfield_base = 0;
        api_logged = false;
        have_player = false;
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
    api_logged = false;
    LOG_WARNING(Core, "Hoenn follower probe {}", enabled ? "ON (rough experiment)" : "OFF");
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

void FollowerProbe::RecoverDllField(Memory::MemorySystem& mem, Kernel::Process& process) {
    if (dllfield_base != 0 && api_name_addr != 0) {
        return;
    }
    // Find "GetPlayerFollowerGridX" in process image → base = addr - file_off
    constexpr u32 kLo = 0x00100000;
    constexpr u32 kHi = 0x00A00000;
    const char* needle = "GetPlayerFollowerGridX";
    const u32 nlen = 22;
    for (u32 a = kLo; a + nlen < kHi; a += 4) {
        bool match = true;
        for (u32 i = 0; i < nlen; ++i) {
            if (static_cast<char>(mem.Read8(process, a + i)) != needle[i]) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }
        api_name_addr = a;
        if (a >= DLL_FOLLOWER_STR_OFF) {
            dllfield_base = a - DLL_FOLLOWER_STR_OFF;
        }
        LOG_WARNING(Core,
                    "Hoenn follower: found API name @{:08X} dll_base={:08X} "
                    "(GetPlayerFollowerGridX)",
                    a, dllfield_base);
        // Peek nearby name table for logging
        LOG_WARNING(Core, "Hoenn follower: API table present — BehindWalk / MdlAdd / "
                          "AddCommMultiTrainerObjOnGrid also in DllField");
        return;
    }
    if ((diag % 200) == 0) {
        LOG_WARNING(Core, "Hoenn follower: GetPlayerFollowerGridX string not found yet");
    }
}

bool FollowerProbe::LooksPos(float x, float y, float z) const {
    if (!OkF(x) || !OkF(y) || !OkF(z)) {
        return false;
    }
    const float ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z);
    const float m = std::max({ax, ay, az});
    // World-ish coords (ORAS field is large)
    if (m < 5.f || m > 5.0e5f) {
        return false;
    }
    // Reject camera dual constants
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

void FollowerProbe::DiscoverPlayerAndFollower(Memory::MemorySystem& mem, Kernel::Process& process,
                                              float pad_x, float pad_y) {
    const bool pad_active = std::fabs(pad_x) >= PAD_DZ || std::fabs(pad_y) >= PAD_DZ;
    const u32 slot = mem.Read32(process, CAMERA_SLOT);
    if (!HeapPtr(slot)) {
        return;
    }

    // Centers: camera slot object + known house/town gold bands
    const u32 centers[] = {slot, 0x082D3000, 0x082D4000, 0x08188000, last_px != 0 ? 0u : 0u};
    (void)centers;

    struct Hit {
        u32 base;
        u32 off;
        float x, y, z;
        float move;
    };
    Hit best_player{};
    Hit best_other{};
    bool have_bp = false, have_bo = false;

    auto consider = [&](u32 base, u32 off) {
        float x, y, z;
        if (!ReadPos(mem, process, base, off, x, y, z)) {
            return;
        }
        // Skip dual freelook band on camera objects
        if (off >= 0x40 && off <= 0xB8) {
            return;
        }
        float move = 0.f;
        if (have_player && base == player_obj && off == player_pos_off) {
            move = std::fabs(x - last_px) + std::fabs(z - last_pz);
        }
        // Prefer objects that move when pad is active
        if (pad_active && have_player && base == player_obj && off == player_pos_off) {
            // update path below
        }
        if (!have_player) {
            // First pass: pick anything that moves with pad later
            if (!have_bp) {
                best_player = {base, off, x, y, z, 0.f};
                have_bp = true;
            }
        }
        // Follower candidate: different base, similar Y, not same pos
        if (have_player && base != player_obj) {
            const float dist = std::fabs(x - last_px) + std::fabs(z - last_pz);
            if (dist > 2.f && dist < 8000.f) {
                if (!have_bo || dist < best_other.move) {
                    best_other = {base, off, x, y, z, dist};
                    have_bo = true;
                }
            }
        }
    };

    // Bootstrap player: scan near slot for first good world pos outside dual
    if (!have_player) {
        for (u32 base = slot > 0x8000 ? slot - 0x8000 : 0x08000000;
             base < slot + 0x10000 && base + 0x200 < 0x0C000000; base += 0x20) {
            for (u32 off = 0; off + 12 <= 0x1C0; off += 4) {
                if (off >= 0x40 && off <= 0xB8) {
                    continue;
                }
                float x, y, z;
                if (ReadPos(mem, process, base, off, x, y, z)) {
                    player_obj = base;
                    player_pos_off = off;
                    last_px = x;
                    last_py = y;
                    last_pz = z;
                    have_player = true;
                    LOG_WARNING(Core,
                                "Hoenn follower: player-ish pos @{:08X}+{:03X} "
                                "({:.1f},{:.1f},{:.1f})",
                                base, off, x, y, z);
                    break;
                }
            }
            if (have_player) {
                break;
            }
        }
        // Also try known gold band for non-cam objects
        if (!have_player) {
            for (u32 base = 0x082D0000; base < 0x082E0000; base += 0x40) {
                for (u32 off = 0; off + 12 <= 0x100; off += 4) {
                    if (off >= 0x40 && off <= 0xB8) {
                        continue;
                    }
                    float x, y, z;
                    if (ReadPos(mem, process, base, off, x, y, z)) {
                        player_obj = base;
                        player_pos_off = off;
                        last_px = x;
                        last_py = y;
                        last_pz = z;
                        have_player = true;
                        LOG_WARNING(Core,
                                    "Hoenn follower: player-ish (band) @{:08X}+{:03X} "
                                    "({:.1f},{:.1f},{:.1f})",
                                    base, off, x, y, z);
                        break;
                    }
                }
                if (have_player) {
                    break;
                }
            }
        }
    }

    if (!have_player) {
        return;
    }

    // Update player pos; if pad moving and pos changes, push history
    float px, py, pz;
    if (ReadPos(mem, process, player_obj, player_pos_off, px, py, pz)) {
        const float d = std::fabs(px - last_px) + std::fabs(pz - last_pz);
        if (d > 0.5f || pad_active) {
            PushHist(px, py, pz);
        }
        last_px = px;
        last_py = py;
        last_pz = pz;
    }

    // Find follower candidate near player if missing
    if (follow_obj == 0 || !HeapPtr(follow_obj)) {
        follow_obj = 0;
        float best_dist = 1.0e9f;
        const u32 lo = player_obj > 0x20000 ? player_obj - 0x20000 : 0x08000000;
        const u32 hi = std::min(player_obj + 0x20000, 0x0BFFFFFFu);
        for (u32 base = lo; base + 0x1C0 < hi; base += 0x40) {
            if (base == player_obj) {
                continue;
            }
            for (u32 off = 0; off + 12 <= 0x1C0; off += 4) {
                if (off >= 0x40 && off <= 0xB8) {
                    continue;
                }
                float x, y, z;
                if (!ReadPos(mem, process, base, off, x, y, z)) {
                    continue;
                }
                const float dist = std::fabs(x - last_px) + std::fabs(z - last_pz);
                // Prefer something a few meters behind-ish, not on top of player
                if (dist < 15.f || dist > 5000.f) {
                    continue;
                }
                if (dist < best_dist) {
                    best_dist = dist;
                    follow_obj = base;
                    follow_pos_off = off;
                }
            }
        }
        if (follow_obj) {
            LOG_WARNING(Core,
                        "Hoenn follower: trail candidate @{:08X}+{:03X} dist≈{:.0f} from player",
                        follow_obj, follow_pos_off, best_dist);
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
    RecoverDllField(mem, *process);

    float pad_x = 0.f, pad_y = 0.f;
    try {
        if (circle_pad) {
            std::tie(pad_x, pad_y) = circle_pad->GetStatus();
        }
    } catch (...) {
        circle_pad.reset();
    }

    DiscoverPlayerAndFollower(mem, *process, pad_x, pad_y);

    // Trail: write lagged position into follow candidate
    if (follow_obj && hist_n >= 4) {
        // lag ~3 samples behind
        int idx = hist_i - 4;
        while (idx < 0) {
            idx += kHist;
        }
        const float tx = hx[static_cast<size_t>(idx)];
        const float ty = hy[static_cast<size_t>(idx)];
        const float tz = hz[static_cast<size_t>(idx)];
        WritePos(mem, *process, follow_obj, follow_pos_off, tx, ty, tz);
        // Also try a few common duplicate offsets (some models mirror pos)
        WritePos(mem, *process, follow_obj, follow_pos_off + 0x10, tx, ty, tz);
    }

    if ((diag++ % 90) == 0) {
        LOG_INFO(Core,
                 "Hoenn follower tick pl={:08X}+{:03X} fo={:08X}+{:03X} hist={} "
                 "pos=({:.0f},{:.0f},{:.0f}) dll={:08X} api={}",
                 player_obj, player_pos_off, follow_obj, follow_pos_off, hist_n, last_px, last_py,
                 last_pz, dllfield_base, api_name_addr ? 1 : 0);
    }
}

} // namespace Hoenn
