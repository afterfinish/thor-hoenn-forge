# Outdoor free look via the GPU path

Status: implemented, builds, **not yet visually confirmed on device**.

## Why the memory camera cannot work outdoors

ORAS chooses a camera *controller type* when it constructs the field camera. Interiors
get a type that reads the euler fields at `+0x98` / `+0x9C` — the ones
`hoenn_freecam.cpp` writes — so free look works there. Towns, routes, caves and gyms get
a type that derives eye and look-at from map geometry and the player transform, and never
reads those fields at all.

That is why the driver's own logs report `p == rb` and `ow = 0` in a town: our writes
persist because nothing owns those bytes, not because we won a fight for them. They are
dead storage. Every approach in the table in `freecam-orbit-attempts.md` /
`freecam-deep-re.md` failed for that one reason, and no write into that object can
succeed.

## What this does instead

`overlay/azahar/video_core/hoenn_gpu_cam.{h,cpp}` operates *downstream* of every game
camera controller, so type-2 and type-13 maps behave identically.

The PICA200 has no fixed-function T&L, so the world-to-eye transform must be somewhere in
the 96 vertex-shader float uniform rows. A 3x4 row-major transform occupies three
consecutive rows, each `(a, b, c | t)`, and its 3x3 part is orthonormal exactly when the
transform is rigid — true of a view or world-view matrix, false of a projection or MVP.
That is the detector.

Given such a matrix `M`, we do **not** replace it (that is what magcius/citra's `camhax`
does for OoT3D, and it needs a per-title magic row index). We left-multiply:

```
M' = O * M        O = T(p) * R * T(-p),   p = (0, 0, -d)
```

`O` acts purely in eye space, so it orbits the camera around the point `d` units in front
of it — the player — and it is correct whether `M` is a pure view matrix or a per-object
world-view matrix, because the view part is a left factor of both. The yaw axis is
world-up expressed in eye space, recovered from `M` itself (column 1 of its 3x3);
yawing about raw eye-space Y would roll the horizon, because the game camera is pitched
down.

The hook is a single call at the end of `VSPicaUniformData::SetFromRegs`, which both
`RasterizerOpenGL::UploadUniforms` and `RasterizerVulkan::UploadUniforms` call — so one
overlay file covers the Thor's Vulkan path.

**Zero bytes of guest memory are written.** This cannot desync a script, poison a map
transition, or softlock. The auto-disable requirements in `freecam-research.md` collapse
to a plain toggle.

## Shipping shape: hybrid

- **Live GOLD target** (interiors): the memory camera keeps ownership. It moves the
  engine's real camera, so culling is correct and there are no artefacts.
- **No live target** (towns, routes, caves, gyms): the GPU camera takes over.
- Transitions between the two reset the GPU deltas to zero.
- The probe forces the GPU path on everywhere, so the experiment can be run in a house.

## Running the discriminating experiment

1. START -> **Outdoor look (advanced)** -> tap **Probe (fixed 12° yaw): Off** so it reads
   **On**. Leave Row on **Auto**.
2. Back out, START -> **Free look** -> On.
3. Stand still in a **house** and look at the top screen, then walk to **Route 101 or
   Oldale** and look again. Do not touch the right stick — the probe is a fixed offset.

Outcomes:

| What you see | Meaning | Next |
|---|---|---|
| World visibly tilts ~12° in **both** places | Confirmed. Everything after this is tuning. | Probe off, play with the stick. |
| Only characters/NPCs tilt, map does not | Detector latched a per-object matrix | Row -> **All qualifying triples**, retest same session |
| Nothing tilts, chip shows a row number | Wrong triple | Row -> **pick a number**, sweep 0..93 |
| Nothing tilts, chip shows no row | No orthonormal triple exists — the engine passes a combined MVP | Needs the `P * O * P^-1` fallback, not yet built |
| World rotates but around a distant point, not the player | Uniforms are column-major, or the radius is far off | Toggle **Matrix layout**, then retune **Orbit radius** |

`adb logcat | grep "Hoenn GPU cam"` prints the chosen row, how many triples qualified, the
mode and the angles once per second.

## Honest limitations

- The game culls and submits geometry for **its** frustum. Rotating far reveals
  unrendered space and backfaces — the same limitation Dolphin's Free Look has. Hence the
  clamps: ±40° yaw, ±25° pitch.
- The stick is sampled from the freecam tick at ~16 Hz, so motion is stepped rather than
  smooth. Raising the tick rate would also speed up zoom assist, which is why it was left
  alone.
- Orbit radius is a constant (2300, the observed town value; a house is ~2000). If it is
  wrong the player drifts off-centre as you yaw. It is tunable in the menu.
- The detector rejects axis-aligned matrices (identity, mirrors, 90° screen rotations) so
  it stays off 2D layout and texture transforms. There is no render-target check, because
  `SetFromRegs` has no access to the framebuffer registers and threading one in would mean
  overlaying both rasterizers whole. If the bottom screen or HUD turns out to rotate, that
  is the fix to make.

## Files

| File | Role |
|---|---|
| `overlay/azahar/video_core/hoenn_gpu_cam.{h,cpp}` | detector, orbit maths, runtime knobs |
| `overlay/azahar/video_core/shader/generator/shader_uniforms.cpp` | upstream overlay, one added call (base `c711b0ab`) |
| `overlay/azahar/core/hoenn_freecam.{h,cpp}` | hybrid split, stick integration, safety parking |
| `overlay/azahar/java/.../hoennforge/HoennGpuCam.kt` | JNI wrapper, param ids |
| `overlay/azahar/java/.../fragments/EmulationFragment.kt` | quick-menu row + options dialog |
| `scripts/build-android.ps1` | `video_core/` overlay path, CMake and JNI patches |
