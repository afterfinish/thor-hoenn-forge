// Copyright Hoenn Forge — ORAS free look (houses) + L/R zoom assist
#pragma once

#include <array>
#include <memory>
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
 * v1 ship camera tools:
 * - Free look (right stick): hybrid.
 *     * Indoors, where the memory camera has a live GOLD target, we keep writing the
 *       dual eulers. That is a real in-engine camera, so culling stays correct.
 *     * Everywhere else — towns, routes, caves, gyms — the game's camera controller
 *       never reads those fields, so the stick instead drives Hoenn::GpuCam, which
 *       rotates the view transform in the vertex-shader uniforms. Zero guest writes.
 * - Zoom assist (L/R): FOV on primary GOLD
 *
 * No experiment menu, CRO patches, shadows, or RE thrash.
 */
class FreeCam {
public:
    static constexpr int kMaxLiveTargets = 4;

    static FreeCam& GetInstance();

    void SetFreelookEnabled(bool enabled);
    bool IsFreelookEnabled() const;
    void SetZoomAssistEnabled(bool enabled);
    bool IsZoomAssistEnabled() const;

    void SetSensitivity(float s);
    float GetSensitivity() const;
    void SetInvertX(bool invert);
    void SetInvertY(bool invert);

    /** Fixed ~15 Hz when tools on. */
    u64 GetScheduleInterval() const;

    void Tick(Core::System& system, u32 process_id);
    void OnModuleLoaded(std::string_view module_name, u32 load_address = 0);
    void OnModuleUnloaded(std::string_view module_name);
    void OnCoreReconnect();

private:
    FreeCam() = default;

    bool freelook = false;
    bool zoom_assist = false;
    float sensitivity = 3.0f;
    bool invert_x = false;
    bool invert_y = true;

    float user_fov = 280.f;
    bool fov_seeded = false;
    float pitch = -12.74f;
    float yaw = 0.f;
    bool yaw_seeded = false;

    // GPU-path free look (Hoenn::GpuCam). These are *deltas* applied on top of whatever
    // camera the game itself computed, not absolute angles like the memory path above.
    float gpu_yaw = 0.f;
    float gpu_pitch = 0.f;
    bool gpu_active = false;

    bool in_battle = false;
    u64 quiet_until = 0;
    u32 last_process_id = 0;
    u32 last_slot_cam = 0;
    u32 last_good_cam = 0;
    u64 last_collect_tick = 0;
    u32 diag = 0;

    std::array<u32, kMaxLiveTargets> live_targets{};
    int live_count = 0;
    u32 primary_cam = 0;

    std::unique_ptr<Input::AnalogDevice> c_stick;
    std::unique_ptr<Input::ButtonDevice> btn_l;
    std::unique_ptr<Input::ButtonDevice> btn_r;

    void EnsureDevices();
    void ResetYaw();
    void StopGpuCam();
    void SeedAnglesFromCam(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam);
    bool IsGoldLive(Memory::MemorySystem& mem, Kernel::Process& process, u32 base) const;
    void CollectLiveTargets(Memory::MemorySystem& mem, Kernel::Process& process, u32 slot_cam);
    void WriteFreelook(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam);
    void WriteZoomFov(Memory::MemorySystem& mem, Kernel::Process& process, u32 cam);
};

} // namespace Hoenn
