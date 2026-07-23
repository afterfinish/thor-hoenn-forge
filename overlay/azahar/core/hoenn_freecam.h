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
 * Overworld freelook + zoom.
 *
 * Pitch: stick Y → cam+0x98 (community third-person field).
 * Orbit yaw: stick X rotates the horizontal XZ offset pair at +0x90/+0x94
 *   (mirrored to +0x4C/+0x50). Writing only X was a slide; rotating the pair
 *   orbits the camera around the player.
 * FOV: L or R when zoom assist on.
 *
 * Never rewrite CAMERA_SLOT (post-battle rebind hard-crashed the guest).
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
    void OnModuleLoaded(std::string_view module_name);
    void OnModuleUnloaded(std::string_view module_name);

private:
    FreeCam() = default;

    bool freelook = false;
    bool zoom_assist = false;
    float sensitivity = 3.0f;
    // Default true: raw stick X felt inverted for users
    bool invert_x = true;
    bool invert_y = false;

    float user_fov = 480.f;
    float pitch = -12.74f;

    // Horizontal orbit around player (radians), radius from live cam XZ pair
    float orbit_angle = 0.f;
    float orbit_radius = 43.f;
    bool orbit_seeded = false;

    bool in_battle = false;
    u64 quiet_until = 0;
    u32 zero_fov_streak = 0;
    u32 diag = 0;

    std::unique_ptr<Input::AnalogDevice> c_stick;
    std::unique_ptr<Input::ButtonDevice> btn_l;
    std::unique_ptr<Input::ButtonDevice> btn_r;

    void EnsureDevices();
};

} // namespace Hoenn
