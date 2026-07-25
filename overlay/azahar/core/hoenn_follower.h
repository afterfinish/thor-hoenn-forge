// Copyright Hoenn Forge — experimental overworld follower probe
#pragma once

#include <array>
#include <string>
#include <string_view>
#include "common/common_types.h"
#include "core/frontend/input.h"
#include <memory>

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
 * Uses DllField evidence: GetPlayerFollowerGridX/Z/Acmd, ActionCmdBehindWalk,
 * AddCommMultiTrainerObjOnGrid. Goal: make *something* trail the player so we
 * can see what the field system does. Collision/softlocks ignored for now.
 */
class FollowerProbe {
public:
    static FollowerProbe& GetInstance();

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

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
    u32 diag = 0;

    // Path history (world-ish floats)
    static constexpr int kHist = 12;
    std::array<float, kHist> hx{}, hy{}, hz{};
    int hist_n = 0;
    int hist_i = 0;

    // Tracked objects
    u32 player_obj = 0;
    u32 player_pos_off = 0;
    u32 follow_obj = 0;
    u32 follow_pos_off = 0;
    float last_px = 0.f, last_py = 0.f, last_pz = 0.f;
    bool have_player = false;

    // API probe
    bool api_logged = false;
    u32 api_name_addr = 0;

    std::unique_ptr<Input::AnalogDevice> circle_pad;

    void EnsurePad();
    void RecoverDllField(Memory::MemorySystem& mem, Kernel::Process& process);
    void DiscoverPlayerAndFollower(Memory::MemorySystem& mem, Kernel::Process& process,
                                   float pad_x, float pad_y);
    void PushHist(float x, float y, float z);
    bool ReadPos(Memory::MemorySystem& mem, Kernel::Process& process, u32 base, u32 off, float& x,
                 float& y, float& z) const;
    void WritePos(Memory::MemorySystem& mem, Kernel::Process& process, u32 base, u32 off, float x,
                  float y, float z);
    bool LooksPos(float x, float y, float z) const;
};

} // namespace Hoenn
