// Copyright Hoenn Forge — GPU-path free look for ORAS (research proposal P1)
//
// Rotates the view transform inside the PICA200 vertex-shader float uniforms instead of
// writing into the game's camera object in guest memory. ORAS picks a camera *controller
// type* at construction: interiors get a type that reads the euler fields the memory
// driver writes, towns/routes get a type that derives eye and look-at from map geometry
// and never reads them. Everything downstream of that choice — including this hook — is
// identical for both, which is why this works outdoors where memory writes cannot.
//
// This writes zero bytes of guest memory, so it cannot desync a script, poison a map
// transition, or softlock. Enabling and disabling it is a pure toggle.
//
// Precedent: magcius/citra branch wip/jstpierre/camhax, commit 77c3eeb, which overwrites
// three uniform rows with a user view matrix for OoT3D. We left-multiply instead of
// replacing, so we do not need to know whether the uniform holds a pure view matrix or a
// per-object world-view matrix.

#pragma once

#include <array>
#include "common/common_types.h"
#include "common/vector_math.h"

namespace Hoenn::GpuCam {

/// Parameter ids for the generic JNI get/set pair. Kept as plain ints so the Android
/// bridge needs exactly two native entry points instead of one per knob.
/// Mirrored by HoennGpuCam.kt — keep the two in sync.
enum Param : int {
    ParamProbe = 0,        ///< rw  0/1 — discriminating experiment: fixed 12 deg yaw
    ParamRowMode = 1,      ///< rw  -1 auto, -2 all qualifying triples, >=0 fixed row
    ParamTranspose = 2,    ///< rw  0/1 — read the triple as columns rather than rows
    ParamRadius = 3,       ///< rw  orbit radius in world units
    ParamDetectedRow = 4,  ///< r   row the detector last selected, -1 if none
    ParamQualifyCount = 5, ///< r   how many orthonormal triples the last full scan found
    ParamActive = 6,       ///< r   0/1 — is the GPU camera driving the view right now
    ParamYaw = 7,          ///< r   current yaw in degrees
    ParamPitch = 8,        ///< r   current pitch in degrees
    ParamInvert = 9,       ///< rw  bit 0 inverts yaw, bit 1 inverts pitch
};

constexpr int kInvertYaw = 1;
constexpr int kInvertPitch = 2;

constexpr int kRowModeAuto = -1;
constexpr int kRowModeAll = -2;

/// The game culls and submits geometry for its own frustum, so rotating far reveals
/// unrendered space and backfaces (the same limitation Dolphin's Free Look has).
/// These clamps keep the look inside what the game actually drew.
constexpr float kYawClampDeg = 40.0f;
constexpr float kPitchClampDeg = 25.0f;

/// Fixed angle used by the discriminating experiment.
constexpr float kProbeYawDeg = 12.0f;

/// Observed camera-to-player distance in the ORAS camera object (+0xA8): ~2000 in a
/// house, ~2300 in a town. Tunable at runtime through ParamRadius.
constexpr float kDefaultRadius = 2300.0f;

// --- Driver side (core, emulation thread) ------------------------------------------

/// Enable the GPU camera and publish the current orbit angles, in degrees.
void SetActive(bool active, float yaw_deg, float pitch_deg);

/// Disable the GPU camera and zero the angles. Safe to call every tick.
void Disable();

/// True when the discriminating experiment is armed, so the driver knows to keep the
/// GPU path live even where the memory camera already works.
bool IsProbeEnabled();

// --- UI side (JNI thread) -----------------------------------------------------------

void SetParam(int param, float value);
float GetParam(int param);

// --- GPU side (called from VSPicaUniformData::SetFromRegs) --------------------------

/// Left-multiplies the detected view transform by an eye-space orbit about the player.
/// No-op (one relaxed atomic load) when the camera is off.
void ApplyToUniforms(std::array<Common::Vec4f, 96>& f);

} // namespace Hoenn::GpuCam
