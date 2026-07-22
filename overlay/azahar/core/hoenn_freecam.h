// Copyright Hoenn Forge — ORAS right-stick free look (experimental)
#pragma once

#include <memory>
#include "common/common_types.h"
#include "core/frontend/input.h"

namespace Core {
class System;
}

namespace Hoenn {

/**
 * Experimental free-look for ORAS using the overworld camera object.
 * Driven by the physical right stick (C-Stick mapping).
 *
 * Not a full code patch: rewrites camera fields each tick. Works best in
 * free-roam overworld; dynamic cameras (Mauville, gyms, cutscenes) may ignore it.
 */
class FreeCam {
public:
    static FreeCam& GetInstance();

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void SetSensitivity(float sensitivity);
    float GetSensitivity() const;

    void SetInvertX(bool invert);
    void SetInvertY(bool invert);

    /// Call periodically while the game is running (e.g. from cheat engine tick).
    void Tick(Core::System& system, u32 process_id);

private:
    FreeCam() = default;

    bool enabled = false;
    float sensitivity = 2.5f;
    bool invert_x = false;
    bool invert_y = false;

    // Accumulated look angles (radians)
    float yaw = 0.f;
    float pitch = 0.15f; // slight downward look

    // Last applied camera base (for reset detection)
    u32 last_cam = 0;

    std::unique_ptr<Input::AnalogDevice> c_stick;
};

} // namespace Hoenn
