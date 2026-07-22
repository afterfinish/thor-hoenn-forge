// Copyright Hoenn Forge — ORAS right-stick free look (experimental)
#include "core/hoenn_freecam.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/input.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"

namespace Hoenn {

namespace {
// Gateway-style camera object pointer used by community ORAS camera cheats (v1.x)
// 6XXXXXXX / B8XXXXXX codes use address 0x05F67DC (masked).
constexpr VAddr CAMERA_PTR = 0x005F67DC;

// Field offsets inside the camera object (from FOV / third-person / zoom cheats + probing)
constexpr u32 OFF_PITCH = 0x98;     // written by third-person / overhead modes
constexpr u32 OFF_FOV = 0xB0;       // wide-angle float
constexpr u32 OFF_ZOOM_U16 = 0xB2;  // free-zoom halfword
// Candidate yaw-related floats near pitch (game-revision sensitive)
constexpr u32 OFF_YAW_CANDIDATES[] = {0x90, 0x94, 0x9C, 0xA0, 0xA4, 0xA8, 0xAC};

// Wide FOV constant used by community wide-angle cheat (float bits 0x44551CCD)
constexpr u32 WIDE_FOV_BITS = 0x44551CCD;

constexpr float PITCH_MIN = -0.85f;
constexpr float PITCH_MAX = 0.75f;
constexpr float STICK_DEADZONE = 0.12f;
constexpr float PI = 3.14159265358979323846f;

u32 FloatBits(float f) {
    u32 bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

float BitsFloat(u32 bits) {
    float f = 0.f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

bool LooksLikePointer(u32 p) {
    // 3DS userland process pointers commonly land in these ranges
    return (p >= 0x08000000 && p < 0x10000000) || (p >= 0x14000000 && p < 0x1C000000) ||
           (p >= 0x00100000 && p < 0x08000000);
}
} // namespace

FreeCam& FreeCam::GetInstance() {
    static FreeCam inst;
    return inst;
}

void FreeCam::SetEnabled(bool e) {
    if (enabled == e) {
        return;
    }
    enabled = e;
    if (enabled) {
        // Start from a neutral look; stick will drive from here
        yaw = 0.f;
        pitch = 0.15f;
        last_cam = 0;
        c_stick.reset(); // reload stick after re-enable / profile changes
        LOG_WARNING(Core, "Hoenn free-look ON (right stick). Experimental — may fail on dynamic cameras.");
    } else {
        c_stick.reset();
        LOG_INFO(Core, "Hoenn free-look OFF");
    }
}

bool FreeCam::IsEnabled() const {
    return enabled;
}

void FreeCam::SetSensitivity(float s) {
    sensitivity = std::clamp(s, 0.25f, 8.f);
}

float FreeCam::GetSensitivity() const {
    return sensitivity;
}

void FreeCam::SetInvertX(bool invert) {
    invert_x = invert;
}

void FreeCam::SetInvertY(bool invert) {
    invert_y = invert;
}

void FreeCam::Tick(Core::System& system, u32 process_id) {
    if (!enabled) {
        return;
    }
    if (!system.IsPoweredOn()) {
        return;
    }

    auto process = system.Kernel().GetProcessById(process_id);
    if (!process) {
        return;
    }

    Memory::MemorySystem& memory = system.Memory();

    // Read right stick (C-Stick mapping) — cache device across ticks
    float sx = 0.f;
    float sy = 0.f;
    try {
        if (!c_stick) {
            c_stick = Input::CreateDevice<Input::AnalogDevice>(
                Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CStick]);
        }
        if (c_stick) {
            std::tie(sx, sy) = c_stick->GetStatus();
        }
    } catch (...) {
        c_stick.reset();
        return;
    }

    // Deadzone
    if (std::fabs(sx) < STICK_DEADZONE) {
        sx = 0.f;
    }
    if (std::fabs(sy) < STICK_DEADZONE) {
        sy = 0.f;
    }
    if (invert_x) {
        sx = -sx;
    }
    if (invert_y) {
        sy = -sy;
    }

    // Scale: cheat tick is ~5Hz; boost so stick still feels responsive
    // If called more often later, sensitivity still works.
    constexpr float dt = 1.f / 5.f;
    yaw += sx * sensitivity * dt;
    pitch += sy * sensitivity * dt;
    // Wrap yaw
    while (yaw > PI) {
        yaw -= 2.f * PI;
    }
    while (yaw < -PI) {
        yaw += 2.f * PI;
    }
    pitch = std::clamp(pitch, PITCH_MIN, PITCH_MAX);

    // Resolve camera object
    const u32 cam_ptr = memory.Read32(*process, CAMERA_PTR);
    if (!LooksLikePointer(cam_ptr)) {
        return;
    }
    if (cam_ptr != last_cam) {
        last_cam = cam_ptr;
        // Soft reset angles on camera object change (map transition)
        // keep yaw/pitch so stick feel is continuous across small swaps
    }

    // Pitch (known field from third-person cheat)
    memory.Write32(*process, cam_ptr + OFF_PITCH, FloatBits(pitch));
    system.InvalidateCacheRange(cam_ptr + OFF_PITCH, 4);

    // Yaw candidates — write same accumulated yaw to nearby floats that look like angles.
    // Only overwrite values that already look like reasonable angle-ish floats or zeros.
    for (u32 off : OFF_YAW_CANDIDATES) {
        const u32 bits = memory.Read32(*process, cam_ptr + off);
        const float cur = BitsFloat(bits);
        const bool plausible = std::isfinite(cur) && std::fabs(cur) < 50.f;
        if (plausible || bits == 0) {
            memory.Write32(*process, cam_ptr + off, FloatBits(yaw));
            system.InvalidateCacheRange(cam_ptr + off, 4);
        }
    }

    // Keep a usable FOV while free-looking
    memory.Write32(*process, cam_ptr + OFF_FOV, WIDE_FOV_BITS);
    system.InvalidateCacheRange(cam_ptr + OFF_FOV, 4);

    // Slightly pull camera back (u16 zoom field) for stick freelook readability
    const u16 zoom = memory.Read16(*process, cam_ptr + OFF_ZOOM_U16);
    if (zoom < 0x200 || zoom > 0x8000) {
        memory.Write16(*process, cam_ptr + OFF_ZOOM_U16, 0x400);
        system.InvalidateCacheRange(cam_ptr + OFF_ZOOM_U16, 2);
    }
}

} // namespace Hoenn
