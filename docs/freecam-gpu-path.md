# Outdoor free look via the GPU path

Status: **mechanism confirmed on device.** A fixed 12° yaw visibly transforms real Route
104 geometry, so "the view transform is reachable in `f[0..95]`" is settled fact, not a
hypothesis. What remains is picking the right matrix and applying it coherently.

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

## Shipping shape: hybrid, on a latched decision

- **Live GOLD target** (interiors): the memory camera keeps ownership. It moves the
  engine's real camera, so culling is correct and there are no artefacts.
- **No live target** (towns, routes, caves, gyms): the GPU camera takes over.
- Ownership is **latched**, never derived per tick. `live_count` is a heuristic sampled
  on a timer and zeroed on module load and slot-pointer change, so a single transient
  zero used to hand the camera over for a frame or two and back — which flickered
  indoors, and kept the outdoor row detector re-acquiring from scratch. Handover now
  needs 4 consecutive empty collect cycles (12 on a map where the memory camera has
  already worked), and the memory path reclaims after 2.
- The probe does **not** force the GPU path on. It only substitutes a fixed 12° yaw for
  the stick while the GPU path is already active.

## Coherence: the thing that actually broke

The first device run rendered Route 104 with its terrain plane flung off screen while
trees, bridge, berry bushes and shadows stayed put and unrotated. Three causes, all now
fixed:

1. **`O` was rebuilt per upload** from whichever row qualified at that moment, so draws
   in the same frame received different rotations. `O` is now derived once from a cached
   up-axis belonging to a single locked reference row, held even across uploads where
   that row does not qualify.
2. **The candidate list was capped at 12** while 21 triples qualified, so a shifting
   subset of the scene was transformed and the rest was not. All 94 triples are now swept
   on every upload; there is no cap and no staleness.
3. **Selection ran per upload.** It now runs once per window (512 uploads or 200 ms),
   ranking rows by how many draws in the window carried them *and* whether the value ever
   changed. The lock is sticky: acquiring needs ≥50% of draws, displacing needs the
   locked row to be under 12.5% for 8 consecutive windows.

The default row mode is **All qualifying triples**, because a scene's rigid transforms
live at several rows at once (terrain shader, prop shader, bone palettes) and every one
of them is a world→eye transform that must receive the same eye-space `O`.

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
| Right stick swings the wrong way | Engine handedness guessed wrong | Cycle **Invert** (none / yaw / pitch / both) |

## The right stick is fully wired

The probe is not the feature — it is a mute switch. The C-Stick (`ANDROID_STICK_C = 718`)
is read every freecam tick and integrates `gpu_yaw` / `gpu_pitch`, clamped to ±40° / ±25°,
which are pushed straight to the GPU camera. With the probe **on**, those accumulated
angles are computed and logged but a constant 12° yaw is substituted before the transform
is built, so an A/B comparison is not confounded by hand movement. Turn the probe **off**
and the stick drives the camera with no further work.

## Reading the census

`adb logcat | grep "Hoenn GPU cam"` prints two lines a second:

```
Hoenn GPU cam: locked=28 cold=0 steady=3 live=3 mode=-2 probe=true yaw=12.0 ...
Hoenn GPU cam census over 512 uploads, row:hits/changes = 28:512/0 3:498/497 6:210/209
```

`row:hits/changes` is how many uniform uploads in the window carried a rigid transform at
that row, and how many of those differed from the previous one.

- **many hits, zero changes, trailing `T`** — a view matrix. This is what the lock should
  hold. The `T` means the row carries a real translation.
- **many hits, zero changes, no `T`** — the normal matrix derived from that view matrix.
  Identical 3x3, zero translation. Ranking prefers the `T` row over this one.
- **many hits, almost as many changes** — a per-object world-view matrix. Correct to
  rotate (that is why All mode exists), but a bad lock target. Rows at a regular 3-row
  stride are a bone palette.
- **few hits** — a transient prop matrix.

The same line reports `eye depth mean/min/max`, sampled from the per-object matrices'
translation columns. That is the scale the orbit radius has to be on. The game's own
camera-object distance (~2300) is on a different scale entirely and throws the scene
several screens off centre, which is why the radius now defaults to zero.

If two rows both show high hits and zero changes, ORAS is running more than one shader
family with the view at different rows; All mode covers that and Auto does not.

`Hoenn freelook path -> GPU/memory` in the same log shows ownership changing hands.

## Honest limitations

- The game culls and submits geometry for **its** frustum. Rotating far reveals
  unrendered space and backfaces — the same limitation Dolphin's Free Look has. Hence the
  clamps: ±40° yaw, ±25° pitch.
- Orbit radius 0 is a safe fallback: it swivels about the eye instead of orbiting the
  player, so no amount of transforming the wrong matrix can fling geometry off screen.
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
