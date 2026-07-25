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

    // Path history
    static constexpr int kHist = 16;
    std::array<float, kHist> hx{}, hy{}, hz{};
    int hist_n = 0;
    int hist_i = 0;

    u32 player_obj = 0;
    u32 player_pos_off = 0;
    u32 follow_obj = 0;
    u32 follow_pos_off = 0;
    float last_px = 0.f, last_py = 0.f, last_pz = 0.f;
    bool have_player = false;

    // Budgeted discovery state (never full-process scan in one tick)
    u32 scan_cursor = 0;
    u32 api_scan_cursor = 0;
    bool api_done = false;
    u32 api_name_addr = 0;
    int fail_cooldown = 0;

    std::unique_ptr<Input::AnalogDevice> circle_pad;

    void EnsurePad();
    void MaybeScanApiName(Memory::MemorySystem& mem, Kernel::Process& process);
    void TryBootstrapPlayer(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam_slot);
    void TryFindFollowerBudgeted(Memory::MemorySystem& mem, Kernel::Process& process);
    void PushHist(float x, float y, float z);
    bool ReadPos(Memory::MemorySystem& mem, Kernel::Process& process, u32 base, u32 off, float& x,
                 float& y, float& z) const;
    void WritePos(Memory::MemorySystem& mem, Kernel::Process& process, u32 base, u32 off, float x,
                  float y, float z);
    bool LooksPos(float x, float y, float z) const;
};

} // namespace Hoenn
