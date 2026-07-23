// Copyright Hoenn Forge — ORAS free look + zoom assist
#pragma once

#include <memory>
#include <string_view>
#include "common/common_types.h"
#include "core/frontend/input.h"

namespace Core {
class System;
}

namespace Hoenn {

/**
 * Pitch: stick Y → cam+0x98 (proven).
 * Yaw:   stick X → cam+0x9C (RE probe #14, dogfood 2026-07-23).
 * Zoom:  L/R → cam+0xB0.
 *
 * Never rewrite CAMERA_SLOT.
 */
class FreeCam {
public:
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

    void Tick(Core::System& system, u32 process_id);
    void OnModuleLoaded(std::string_view module_name, u32 load_address = 0);
    void OnModuleUnloaded(std::string_view module_name);

private:
    FreeCam() = default;

    bool freelook = false;
    bool zoom_assist = false;
    float sensitivity = 3.0f;
    bool invert_x = true;
    bool invert_y = false;

    float user_fov = 480.f;
    float pitch = -12.74f;
    float yaw = 0.f;
    bool yaw_seeded = false;

    bool in_battle = false;
    u64 quiet_until = 0;
    u32 zero_fov_streak = 0;
    u32 diag = 0;
    u32 last_cam = 0;

    std::unique_ptr<Input::AnalogDevice> c_stick;
    std::unique_ptr<Input::ButtonDevice> btn_l;
    std::unique_ptr<Input::ButtonDevice> btn_r;

    void EnsureDevices();
    void ResetYaw();
};

} // namespace Hoenn
