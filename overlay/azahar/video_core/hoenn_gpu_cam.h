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
    ParamProbe = 0,        ///< rw  0/1 — substitute a fixed 12 deg yaw for the stick
    ParamRowMode = 1,      ///< rw  -1 auto, -2 all qualifying triples, >=0 fixed row
    ParamTranspose = 2,    ///< rw  0/1 — read the triple as columns rather than rows
    ParamRadius = 3,       ///< rw  orbit radius in world units
    ParamDetectedRow = 4,  ///< r   row the detector last selected, -1 if none
    ParamQualifyCount = 5, ///< r   how many orthonormal triples the last full scan found
    ParamActive = 6,       ///< r   0/1 — is the GPU camera driving the view right now
    ParamYaw = 7,          ///< r   current yaw in degrees
    ParamPitch = 8,        ///< r   current pitch in degrees
    ParamInvert = 9,       ///< rw  bit 0 inverts yaw, bit 1 inverts pitch
    ParamPivotMode = 10,   ///< rw  where the orbit centre sits — see kPivot* below
    ParamRange = 11,       ///< rw  multiplier on the yaw/pitch clamps
};

/// Default look range. Wider than the original clamps, which device testing found too
/// restrictive once the camera was actually working.
constexpr float kDefaultRange = 2.0f;

/// Where to put the point the camera orbits around.
///
/// This is a convention that cannot be read off the uniforms, so it is a knob rather than
/// a constant. Both axis modes were tried on device and both threw the scene off screen,
/// because a distance is not a point: ORAS's tilted overhead camera puts the drawn objects
/// forward *and below* the eye, so a pivot on the Z axis at the measured distance sits
/// thousands of units above the player.
enum PivotMode : int {
    /// Learned from the interior camera — see [Observe]. The default, and the only mode
    /// that involves no guessing at all.
    kPivotCalibrated = 0,
    kPivotMeasured = 1,   ///< mean eye-space translation of the per-object matrices
    kPivotForwardPos = 2, ///< (0, 0, +radius)
    kPivotForwardNeg = 3, ///< (0, 0, -radius)
    kPivotNone = 4,       ///< swivel about the eye; cannot displace geometry
    kPivotModeCount = 5,
};

constexpr int kInvertYaw = 1;
constexpr int kInvertPitch = 2;

constexpr int kRowModeAuto = -1;
constexpr int kRowModeAll = -2;

/// The game culls and submits geometry for its own frustum, so rotating far reveals
/// unrendered space and backfaces (the same limitation Dolphin's Free Look has). Pitch is
/// therefore clamped, scaled by the user's chosen range — see [PitchClampDeg].
///
/// Yaw is **not** clamped. It turns about the vertical, so it has no degenerate pose to
/// protect against and no reason to stop: it wraps, giving a full 360 degrees around the
/// player. The original limit was a guess made before the camera worked well enough to
/// judge it, and on device it simply read as the camera running out of travel halfway.
constexpr float kPitchClampBaseDeg = 25.0f;

/// Fold an angle into [-180, 180]. Yaw accumulates without bound otherwise.
float WrapDeg(float deg);

/// Hard ceiling on pitch regardless of range: at 90 degrees the up vector and the view
/// direction line up and the yaw axis stops being well defined.
constexpr float kPitchClampCeilDeg = 85.0f;

/// Effective pitch clamp, base times the selected range.
float PitchClampDeg();

/// Fixed angle for A/B testing: substitutes a constant yaw for the stick so a change
/// can be judged without also moving. It no longer forces the GPU path on where the
/// memory camera owns the map — doing that made the camera flicker indoors.
constexpr float kProbeYawDeg = 12.0f;

/// Orbit radius override, in *eye-space* units. Zero means use [kDefaultOrbitDistance].
///
/// Two other sources for this were tried and both were wrong. The game camera object's own
/// distance field (+0xA8, ~2000 in a house, ~2300 in a town) is in some unrelated unit and
/// shoved Route 104 about three screens off centre. The eye-space depth of the per-object
/// matrices reads ~4950, roughly thirty times too large, and did the same. Only the
/// interior camera's own motion gave a figure on the right scale.
constexpr float kDefaultRadius = 0.0f;

/// Default orbit distance in eye-space units, used when the radius knob is left at zero.
///
/// The interior calibration can measure this, but it should not: the distance is a
/// property of the *map*, not of the engine, and interiors are the only place it can be
/// measured. A house taught 225, which read as almost right outdoors; a Pokemon Center
/// then taught 514 and the orbit centre moved somewhere above and behind the player.
/// Learning the row and the axis from interiors is sound because those are engine
/// properties that carry over. Learning the distance is not, so it is a tuned default
/// with a knob, and the calibration's own figure is kept only for the log.
constexpr float kDefaultOrbitDistance = 225.0f;

// --- Driver side (core, emulation thread) ------------------------------------------

/// Enable the GPU camera and publish the current orbit angles, in degrees.
void SetActive(bool active, float yaw_deg, float pitch_deg);

/// Disable the GPU camera and zero the angles. Safe to call every tick.
void Disable();

/// Dolly the camera along its own view axis, in eye-space units. Positive pulls back.
///
/// This is what L/R zoom has to become outdoors. Indoors zoom writes an FOV into the
/// game's camera object, but the town controller re-derives FOV every frame and ignores
/// the write, which is the same reason free look needed this path at all. Moving the
/// camera is not the same as widening the lens — there is no perspective change — but it
/// is the honest equivalent here, and it costs no guest memory.
void SetDolly(float units);
float GetDolly();

/// Turn a circle-pad vector into the frame the player is actually looking at.
///
/// ORAS maps walking directions relative to *its* camera, and this path never moves that
/// camera -- it rotates what is drawn. So after swinging the view, pushing up still walks
/// in the old direction, which on device reads as the character moving "as if the camera
/// were still where it started". Indoors the problem does not arise, because the memory
/// path moves the engine's own camera and the mapping follows it.
///
/// Rotating the stick by the same yaw before the game reads it puts the two back in
/// agreement. No-op unless the GPU camera is driving.
void RotateStick(float& x, float& y);

/// Clamp on the dolly, as a multiple of the orbit distance.
constexpr float kDollyRangeMul = 3.0f;

/// Watch the interior camera instead of driving anything.
///
/// This is the calibration path, and the reason the outdoor camera does not have to guess.
/// Indoors the memory driver moves the game's *own* camera, correctly, and the result lands
/// in the same uniform row the GPU path wants to drive. Sampling that row at two different
/// angles gives D = M1 * M0^-1, the exact transform the engine applies for a known change
/// in yaw. A rigid transform has a fixed point, found by solving (I - R)p = t, and that
/// point is the pivot the game orbits about — measured in eye space rather than assumed.
/// The rotation axis falls out of the same decomposition.
///
/// Every convention that was previously a coin flip — handedness, which way is forward,
/// where the orbit centre sits, row versus column major — is therefore observed instead of
/// argued about. Call this from the driver each tick while the memory path owns the camera.
void Observe(float yaw_deg, float pitch_deg);

/// True once a usable interior calibration has been captured.
bool IsCalibrated();

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
