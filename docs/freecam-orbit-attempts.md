# Freecam orbit attempts — do not repeat failures

**Last updated:** 2026-07-23  
**Pitch (stick Y → `cam+0x98`):** WORKS. Do not thrash.

## Failed approaches (never ship again)

| # | What we did | User result |
|---|-------------|-------------|
| F1 | Stick X → write single float `+0x90` (as “angle” or raw) | Free-cam **X slide** only (left/right on one axis) |
| F2 | Stick X → `atan2` + `sin/cos` rotate **pair `+0x90` / `+0x94`** (and alts `+0x4C`/`+0x50`), fixed radius | **Rotates on XY plane** — not orbit around vertical / character |
| F3 | Same as F2, rebranded “character-relative orbit” | **Same XY-plane spin** (user, 2026-07-23) |
| F4 | Multi-write many heap FOV candidates | Black screen |
| F5 | Rewrite `*CAMERA_SLOT` rebind | Hard crash after battle |

## Why F2/F3 feel like “XY plane”

Live dumps show `+0x90` / `+0x94` sit immediately before pitch:

```text
+0x90  ~ -30 … +30   (large)
+0x94  ~ -30 … +30   (large)  OR sometimes ~1
+0x98  pitch angle   (community third-person)
+0xB0  FOV
```

Rotating that pair with `sin/cos` only reparameterizes two floats the game already treats as a **2D quantity** (offset or dual euler). That does **not**:

1. Locate the **character / look-at**, or  
2. Move the **eye** on a circle around that point in 3D, or  
3. Keep the view aimed at the character.

So it feels like free-cam / planar spin, not third-person orbit.

## Locked product path (2026-07-23)

User dogfood of numbered RE probes: **#14 `cam+0x9C`** = correct horizontal freelook.

| Input | Memory | Status |
|-------|--------|--------|
| Stick **Y** | `cam+0x98` pitch | Proven |
| Stick **X** | **`cam+0x9C` yaw** | **Locked** (probe #14) |
| L / R | `cam+0xB0` FOV | Proven |

Multi-probe START menu removed after confirmation.

## Earlier RE / failed side paths

| ID | Approach | Status |
|----|----------|--------|
| +0x94 alone | vertical / wrong | Confirmed bad |
| +0x90 / sin-cos pair | F1–F3 slide/spin | Failed earlier |
| N2 eye/target float3 | False pairs | No visual |
| Pad-train | Locked pitch alt | Removed |
| N5 code.bin / CRO patch | Full freecam | Still open later |

## Success criteria

- Stick X: camera **revolves around the player**; player stays roughly centered  
- Stick Y: pitch unchanged  
- Not pure world-X translation; not planar spin of the 0x90/0x94 pair  

## Evidence sources

- logcat freecam dumps (cam=`082D4898` / `082D2F48`)  
- DllField.cro RTTI camera class names  
- Community cheats: only pitch `+0x98` and FOV `+0xB0` (no public yaw)  
