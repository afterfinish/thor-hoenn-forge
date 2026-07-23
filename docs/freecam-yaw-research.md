# Freecam yaw (left/right) research

**Date:** 2026-07-23  
**Scope:** horizontal freelook only. **Do not change working stick-Y → pitch** (`cam+0x98`).  
**Status:** Y0 dump + Y1 single-offset probe **implemented** in `hoenn_freecam.cpp` (2026-07-23). Pitch path unchanged. APK built; install when you ask.  
**Title under test:** Alpha Sapphire USA `000400000011C500` (v1.x; pointer matches community 1.4 camera cheats).

---

## In-game probe (how to dogfood)

1. Free look ON (START menu). Pitch Y should still work as before.
2. Right stick **X** drives the **current** yaw candidate only.
3. Press **L+R together** (edge) → next candidate (`+0x90`, `+0x94`, … ~47 offsets). Toast is log-only — watch logcat.
4. logcat filter: `Hoenn yaw` / `yawProbe` / `yawdump` / `yawΔ`
   - `yawdump[idle]` / `yawΔ[pad]` — observation when circle pad turns
   - `yawProbe → [n/N] offset +0xXX` — which field stick X is writing
5. If camera orbits left/right on a candidate → **note the offset**; we lock it in code and drop the probe list.
6. If glitch: L+R to leave that offset; stop freelook.

---

## Current product (what works)

| Input | Effect | Memory |
|-------|--------|--------|
| Right stick **Y** | Pitch up/down (feels great) | `*0x085F67DC + 0x98` float |
| L / R | Zoom FOV | `+0xB0` float |
| — | — | Zoom u16 (community free-zoom) at `+0xB2` |

Gateway pattern (community):

```text
685F67DC 00000000   ; if pointer non-null
B85F67DC 00000000   ; load pointer as offset base
00000098 C14BD70A   ; pitch = -12.74f (third-person)
000000B0 44551CCD   ; FOV wide
; free zoom L/R mutates halfword at +0xB2
```

Slot address in process: **`0x085F67DC`** (cheat line nibble `6` is type; address is `0x085F…`).

Driver: `overlay/azahar/core/hoenn_freecam.cpp` — stick **X is intentionally ignored** (`(void)sx`).

---

## What community cheats prove (and don’t)

Public ORAS camera cheats (Inazuma Life / same lineage as Gateway codes):

| Cheat | Offset | Meaning |
|-------|--------|---------|
| Wide FOV | `+0xB0` | Field of view float |
| Free zoom L/R | `+0xB2` | Distance / zoom u16 |
| Third-person | `+0x98` = `C14BD70A` | **Pitch only** |
| Overhead | `+0x98` = `DDDDDDDD` | Still **pitch only** |

**There is no public “free yaw / orbit left-right” cheat.**  
If a simple float near pitch were enough, someone would have shipped it next to third-person. That is strong negative evidence against “just write yaw next to pitch.”

Same cheats also note: need map refresh (Poké Center) sometimes; **fail on dynamic cameras** (Mauville, many gyms). Same failure class as our post-battle dead slot.

---

## What we already tried (failed)

Early freecam (`62bf8b6` era) accumulated stick X into `yaw` and **blast-wrote** the same value to:

```text
OFF_YAW_CANDIDATES = { 0x90, 0x94, 0x9C, 0xA0, 0xA4, 0xA8, 0xAC }
```

…whenever the float looked “angle-ish” (`|v| < 50` or zero).

**Result:** no reliable visual left/right freelook. Pitch later isolated at `0x98` and became the shipping path; X was killed on purpose.

Do **not** re-enable multi-candidate blast writes without a proven single offset — risk of corrupting FOV/zoom neighbors and black screens (same class of bug as heap multi-write).

---

## Why pitch can work while yaw does not

Hypothesis (ordered by likelihood):

1. **Camera is a follow-cam, not a free-look rig**  
   Horizontal orientation is derived each frame from **player facing** + orbit/lag, not a user-owned yaw euler sitting next to pitch. Pitch is a separate “elevation” knob the follow system accepts; yaw is recomputed.

2. **True horizontal control is orbit / look-at geometry**  
   Eye position and look-at (or a single orbit angle around the player up-axis) live **elsewhere** in the camera object (or a sibling system). Changing only an unused float near `0x98` does nothing; changing pitch does.

3. **Writes are clobbered**  
   Even if we find a yaw field, field camera update may overwrite it every frame unless we write every tick **after** the game’s update, or we **patch** the updater.

4. **Units / layout mismatch**  
   Values may be degrees, fixed-point, or sin/cos pair — not the same radians range as pitch (`~-25…0`).

Gen IV dynamic-camera research (different engine generation, but useful model) treats horizontal rotation as **camera/target XZ orbit**, not a single “yaw next to pitch” float. ORAS Gen 6 field camera is more complex, but the mental model “orbit + elevation + FOV” still fits what cheats expose (elevation + FOV only).

---

## Camera object map (known so far)

```text
cam = *(u32*)0x085F67DC     // heap object, often ~0x082Dxxxx overworld

+0x90 … +0xAC   UNKNOWN / failed yaw candidates (do not thrash)
+0x98           PITCH float  ← WORKING (stick Y)  base ~-12.74
+0x9C … +0xAC   unknown
+0xB0           FOV float    ← WORKING (L/R zoom assist)
+0xB2           ZOOM u16     ← community free-zoom
+0xB4+          unknown (possible matrices / positions further out)
```

**Gap:** no confirmed horizontal orbit / yaw field. Layout before `0x90` and after `0xB4` not systematically mapped on our dump.

---

## Recommended discovery plan (when we implement)

### Phase Y0 — Observe only (safe)

While freelook pitch is ON and working (pre-battle overworld):

1. Resolve `cam = *0x085F67DC`.
2. Dump floats `cam+0x00 … cam+0x200` (or u32 words) for several states:
   - Stand still facing North
   - Stand still facing East / South / West (turn with circle pad / D-pad)
   - Walk forward
   - Stick Y pitch up vs down (known good)
   - Stick X held left vs right (**expect no visual change today**)
3. **Diff** which words change with **player turn** but **not** with pitch. Those are yaw/orbit/facing-linked candidates.
4. Diff which words change with stick X even if screen doesn’t (dead fields we write but game ignores).

Deliverable: table `offset → Δ on turn / Δ on pitch / Δ on stick X`.

### Phase Y1 — Single-offset probe (still careful)

For each strong candidate from Y0:

- Drive **only that offset** from stick X (leave pitch path untouched).
- Write every freecam tick (same cadence as pitch).
- Try both: absolute set, and read-modify-add.
- Try angle units: small step (`0.02`), degrees, and full turn range.
- Log: offset, before, after, cam ptr. User confirms visual.

Stop criteria per candidate:

- Visual orbit → **keep, clamp, ship**
- No visual after full range poke → discard
- Glitch/black screen → discard and never multi-write

### Phase Y2 — If no float works: geometry / patch

| Approach | Idea | Effort |
|----------|------|--------|
| **Orbit via eye/target** | If positions exist, rotate eye XZ around player by stick X each frame | Medium |
| **Player-facing decoupling** | Patch field camera so orbit angle is sticky free-look yaw instead of player dir | High (DllField RE) |
| **View matrix write** | Last resort; fragile, stereo/upscale sensitive | High risk |

`DllField.cro` is available locally (`local/dumps/romfs/000400000011C500/DllField.cro`, ~1.3 MB) for Ghidra when float hunt fails.

Prefer **Y0→Y1** first: pitch proved cheat-style floats can work without a full code patch.

---

## Implementation constraints (when coding)

1. **Do not modify** the working pitch path (`OFF_PITCH`, clamps, stick Y sign).
2. Stick X only → new yaw/orbit path; optional invert_x (already stubbed).
3. No multi-write of many heap FOV candidates (black screens — see MEMORY).
4. Same battle pause / quiet / dead-FOV rules as pitch (post-battle bug is separate track).
5. Ship only after user dogfood on Thor; **no install until asked**.

---

## Success criteria (yaw feature)

- Right stick X smoothly orbits/yaws camera in free-roam overworld.
- Pitch Y behavior unchanged.
- No black screen / softlock on Route 101–103 smoke path.
- Document offset + units + title revision in this file.

---

## References

- Community camera codes: pointer `685F67DC` / `B85F67DC`, pitch `+0x98`, FOV `+0xB0`, zoom `+0xB2` (Inazuma Life ORAS cheats; same family as Gateway).
- `MEMORY.md` — pitch works; yaw not found; post-battle dead object.
- `docs/freecam-research.md` — milestone C brief.
- Prior failed multi-candidate yaw write: git era around `62bf8b6` freecam.
- Gen IV dynamic cameras (methodology only): pokehacking “dynamic cameras” tutorial (orbit via camera/target XZ).
- Local: `DllField.cro` under `local/dumps/romfs/000400000011C500/` for code RE.
