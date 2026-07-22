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
 * Overworld freelook (stick Y → pitch) + zoom (L/R → FOV).
 *
 * Writes only the official camera object at *0x085F67DC.
 * After battle the slot often points at a dead object (FOV=0). We then
 * *rebind* the slot (one pointer write) to a nearby live camera — same
 * effect as entering a building, without touching random heap FOV fields.
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
    bool invert_x = false;
    bool invert_y = false;

    float user_fov = 480.f;
    float pitch = -12.74f;

    bool in_battle = false;
    u64 quiet_until = 0;
    u64 next_rebind_attempt = 0;
    u32 zero_fov_streak = 0;
    u32 diag = 0;

    std::unique_ptr<Input::AnalogDevice> c_stick;
    std::unique_ptr<Input::ButtonDevice> btn_l;
    std::unique_ptr<Input::ButtonDevice> btn_r;

    void EnsureDevices();
    bool TryRebindLiveCamera(Core::System& system, u32 process_id);
};

} // namespace Hoenn
