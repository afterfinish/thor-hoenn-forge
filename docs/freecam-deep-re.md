# Freecam deep RE — 2026-07-24

**Title:** Alpha Sapphire USA `000400000011C500`  
**Device dogfood:** AYN Thor · package `dev.tzigdon.hoennforge.debug`  
**Pointer:** `*(u32*)0x085F67DC` → heap cam object  
**Builds tested:** mode-unlock-v3 · echo-v4 · **shadow-v5** (pending install)

This document consolidates live RE dumps, failed approaches, DllField evidence, and the only paths still worth taking. **Do not re-try items in the ban list.**

---

## 1. Ban list (proven dead / harmful)

| ID | Approach | Result | Source |
|----|----------|--------|--------|
| F1–F3 | sin/cos rotate `+0x90`/`+0x94` | Planar spin, not freelook | orbit-attempts |
| F4 | Multi-write ~12 FOV “silvers” | Camera chaos / black screen | crazy-2f |
| F5 | Rewrite `*CAMERA_SLOT` | Hard crash | orbit-attempts |
| v3 | Force mode **only** at `+0x8C` → `0x00020001` | Town still dead; pitch sticky FREE | four-v3 |
| v4 | Distance-scored FOV=200 “echoes” | **List-node junk**, not cameras | four-echo-v4 |
| Pitch-gate LIVE | Require pitch range for LIVE | False dead (fov=225 fl=F) | session |
| FOV-only LIVE | Accept FOV without flag `0x0F` | Zoom-to-nothing (798/110) | session |

---

## 2. Four-dump protocol (user order)

Always: **town → 1F working → 2F working → 2F not working**.

### echo-v4 session (canonical)

| # | Place | Slot | Primary GOLD | mode `@+48` / `@+8C` | Freelook | Key observation |
|---|-------|------|--------------|----------------------|----------|-----------------|
| 1 | Town | `082D4898` GOLD | same | **both `000D0001`** | **NO** | FOV 254; `+6C`=270.4 ≠ `+B0`=254.45 |
| 2 | 1F yes | `082D3458` GOLD | same | **both `00020001`** | **YES** | Dual FOV match 225.17 |
| 3 | 2F yes | slot DEAD | `082D3920` GOLD FREE | both FREE | **YES** | Shadow `08285648` pitch **matches** gold (−25) |
| 4 | 2F no | slot DEAD | `082D3920` GOLD FREE | both FREE | **NO** | Shadow `08285648` pitch **stale** (−25) while gold −23.36 |

### Cross-session GOLD addresses

| Base | Role |
|------|------|
| `082D3458` / `082D4898` / `082D3920` | Slot / primary field cams (heap band `082Dxxxx`) |
| `08188228` | **Always present** second GOLD, FOV≈251, mode often 0 — likely stereo / secondary eye (`FieldStereoCamera`) |
| `08285648` | **flag=0 FOV≈primary** shadow; tracks pitch when freelook works; **stale when dead** |

---

## 3. Camera object map (locked from PRI dumps)

Working and town GOLDs share the **same dual layout**. Full dual block:

```text
offset   type     meaning                         notes
------   ----     -------                         -----
+0x40    u32      0
+0x44    f32      1.0                             constant
+0x48    u32      MODE (mirror of +0x8C)          FREE=0x00020001  TOWN=0x000D0001
+0x4C    u32      0
+0x50    f32      angle-ish                       mirrors +0x94
+0x54    f32      PITCH                           mirrors +0x98   ← we write
+0x58    f32      YAW                             mirrors +0x9C   ← we write
+0x5C    u32/f32  0
+0x60    f32      32.0                            constant-ish
+0x64    f32      distance / zoom                 town 2300 · house 2000
+0x68    f32      30.0
+0x6C    f32      FOV mirror                      == +0xB0 when LIVE-feeling
+0x70    u32      0
+0x74    f32      5.1                             constant
+0x78-7C 0
+0x80    u32      FLAG                            LIVE = 0x0000000F
+0x84-88 0
+0x8C    u32      MODE                            same enum as +0x48
+0x90    0
+0x94    f32      angle-ish                       (failed yaw probe band)
+0x98    f32      PITCH                           community third-person
+0x9C    f32      YAW                             probe #14 locked (when live)
+0xA0    0
+0xA4    f32      32.0
+0xA8    f32      distance                        mirrors +0x64
+0xAC    f32      30.0
+0xB0    f32      FOV                             L/R zoom
+0xB2    u16      zoom (community free-zoom)      inside FOV word region
+0xB4    0
+0xB8    f32      5.1
```

**Implication:** writes must hit **both halves** of the dual block for mode and FOV, not only the community “primary” offsets.

---

## 4. What “pitch sticky” actually proves

Logs (`ok v3` / `ok v4`): `p == rb`, `ow=0` in town **and** 2F-bad.

| Interpretation | Supported? |
|----------------|------------|
| Wrong heap address entirely | **No** for house 1F (works). **Maybe** for 2F-bad if render uses shadow |
| Game overwrites our pitch every frame | **No** (`ow=0`) |
| Memory holds pitch but **render path ignores eulers** | **Yes** for town lock + 2F-bad ghost |
| Mode word alone unlocks freelook | **No** (v3 forced FREE, still dead; 2F-bad already FREE) |

So freelook death is **not** “find another GOLD and write pitch.” It is either:

1. **Write the object the renderer actually samples** (shadow hypothesis), or  
2. **Change the game’s update/render path** (DllField / mode consumer), or  
3. **Accept mode-locked follow-cam** in some maps (community cheat limit).

---

## 5. echo-v4 failure analysis (do not repeat)

Selected echoes:

```text
082D4AD8 / 082D3698 / 082D3B60  fov=200  fl=0
layout: pointers at +40..+60, +A4=-100, +B0=200 fixed, +B8=120
```

These are **allocator / list nodes**, not camera objects. Distance scoring preferred them over `08285648` (FOV 225.2, farther from slot).

**shadow-v5 rule:** shadow only if `flag==0` and `|fov − primary_fov| ≤ 2` and layout gate (reject FOV=200 when primary ≠ 200).

---

## 6. 2F working vs 2F dead — critical

Dump 3 and 4:

- Same primary `082D3920`
- Same FREE dual mode
- Same dual FOV match
- **Only** euler values differ on gold
- WIDE: `08285648` pitch **matches gold when working**, **stale when dead**

**Conclusion for 2F-bad:** dual-mode unlock cannot help (already FREE). Must drive **FOV-matched flag=0 shadow(s)** (and/or find another render-owned object via variance/FOV-pulse).

Town is a **different** class: mode lock `0x000D0001` + FOV dual desync.

---

## 7. DllField.cro evidence

Path: `local/dumps/romfs/000400000011C500/DllField.cro` (~1.3 MB)

### RTTI / symbols (Itanium mangled)

| Symbol | Role |
|--------|------|
| `N5field17FieldStereoCameraE` | Stereo field camera (explains dual GOLDs / `08188228`) |
| `N5field18FieldCameraSettingE` | Settings object |
| `N5field17CCameraULCDTargetE` | ULCD / target cam |
| `N5field26CCameraGameTargetInterfaceE` | Game target interface |
| `EVCameraBegin/End/Bind/Set*Param` | **Event / cinematic camera API** (separate from free roam) |
| `ShakeCamera*`, `PlayCameraAnimation` | Scripted motion |
| `Call3DCamera`, `IsPlayingCameraAnimation` | Animation gate |

### Immediate constants in CRO

| Constant | Count in DllField | Meaning |
|----------|-------------------|---------|
| `0x00020001` | 8 | FREE mode (matches working dumps) |
| `0x000D0001` | 1 @ `0x1195A0` | TOWN/lock mode (matches town dump) |
| FOV 480.0 float | 35 | Default wide FOV constant |

**Next code RE (when Ghidra time):** xrefs to `0x000D0001` and `0x00020001` → functions that **branch camera update**. Patch candidate: force free-roam branch or inject stick into orbit angles.

---

## 8. Community cheats (ceiling of float-only)

Inazuma / Gateway lineage (AS/OR 1.4):

| Cheat | Offset | Notes |
|-------|--------|-------|
| Wide FOV | `+0xB0` | Works same places as our zoom |
| Free zoom | `+0xB2` u16 | Distance |
| Third-person / overhead | freeze `+0x98` | **Pitch only** — no public yaw |
| Failures | dynamic cams, Mauville, “need map refresh” | **Same class as our dead zones** |

There is **no public free-yaw cheat**. Pitch freelook is already at community ceiling for pure float poke; dead zones need shadow discovery or code path.

---

## 9. Savestate / raw heap note

`local/re-cam/as.00.raw` is a **boost serialization archive** (`serialization::archive`), **not** a flat FCRAM image. Automated GOLD signature scan of raw yields false positives (ASCII “reCo” etc.).  

**Do not** base layout RE on raw savestate scans. Use live `DumpREState` / logcat only.

User report: savestate load → fatal toast → Continue works. `CheatEngine::Connect` already re-registers freecam after load (`OnCoreReconnect`). Treat fatal as Azahar recover path unless it regresses with freecam-only builds.

---

## 10. Driver architecture facts

| Item | Value |
|------|-------|
| Tick host | `Cheats::RunCallback` |
| Freecam interval | `16_000_000` ticks (~15 Hz) — coarse but OK when path is live |
| Writes | GOLD multi + dual pitch/yaw; shadow-v5 adds FOV-matched shadows + dual mode/FOV |
| Pause | `DllBattle` / quiet after `DllField` |

Increasing tick rate alone **cannot** fix ghost GOLD (game is not overwriting; render ignores).

---

## 11. Ranked research / product paths

### A. Ship / dogfood (memory, low risk) — **shadow-v5**

1. GOLD dual pitch/yaw (`+54/+58` + `+98/+9C`)  
2. Dual-mode unlock **only** when `+48`/`+8C` show town lock or mismatch → write FREE to **both**  
3. Shadows: max 2, `flag==0`, `|fov−pri|≤2`, layout gate  
4. Dual FOV on L/R (`+6C` + `+B0`)  

**Expected:** help 2F-bad if `08285648` is render-owned; help town if dual-mode is sufficient.  
**Not expected:** Mauville/cutscene/EVCamera script locks.

### B. FOV ownership pulse (RE tool, next dogfood)

On RE dump or debug combo:

1. Snapshot FOV of primary + top 5 FOV-matched candidates  
2. Nudge each candidate FOV by +40 for ~200 ms (one at a time)  
3. User reports which nudge **visually** zoomed  
4. Lock that base as `render_cam`  

Proves render ownership without thrashing pitch.

### C. Freelook-OFF variance scan (RE tool)

With freelook **off**, player turns with circle pad:

1. Sample pitch/yaw/FOV on candidates every 50 ms for 1 s  
2. Promote objects whose eulers **change with player camera**  
3. Inverse of sticky-write (docs already recommend this)

### D. DllField code path (product long-term)

1. Ghidra on `DllField.cro` at `0x1195A0` (`0x000D0001`) and eight `0x00020001` sites  
2. Find update that builds view from player facing vs free eulers  
3. Patch: stick → free angles when not `IsPlayingCameraAnimation` / EVCamera active  
4. Matches OoT3D freecam philosophy (SOUL) better than permanent cheat thrash  

### E. Explicit non-goals near-term

- Full decomp of ORAS field camera  
- Battle freecam  
- View-matrix spray without ownership proof  

---

## 12. Dogfood checklist (shadow-v5)

```text
adb -s 45e67a5d install -r dist\HoennForge-vanilla-relWithDebInfo.apk
```

1. Force-stop / relaunch  
2. Freelook ON → logcat `BUILD=shadow-v5`  
3. Town: any freelook? dual-mode unlock lines?  
4. House 1F: still good (regression gate)  
5. 2F good + 2F bad  
6. If dead: 4 RE dumps — toast `sh=` should list FOV-matched shadows (e.g. `08285648`), **not** FOV 200  
7. Compare WIDE: does shadow pitch now track gold when stick moves?

---

## 13. File index

| Path | Use |
|------|-----|
| `overlay/azahar/core/hoenn_freecam.cpp` | Driver |
| `local/re-cam/four-echo-v4.txt` | Canonical 4-dump log |
| `local/re-cam/deep_scan2.py` | Offline float/search helpers |
| `docs/freecam-orbit-attempts.md` | Failed orbit ban list |
| `docs/freecam-yaw-research.md` | Yaw discovery plan |
| `docs/freelook-interior-transition.md` | Interior death class |
| `local/dumps/.../DllField.cro` | Code RE target |

---

## 14. Bottom line

1. **Euler sticky on GOLD ≠ freelook.**  
2. **Mode is dual** (`+0x48` and `+0x8C`); town natural lock is `0x000D0001`.  
3. **2F-bad is FREE already** — needs **FOV-matched shadow drive**, not more mode thrash.  
4. **echo-v4 wrote junk** — fixed in shadow-v5 selection rules.  
5. **Durable product path** is still DllField update patch after ownership RE; memory drive remains the interim that already wins house 1F / good 2F.

*Research session complete for offline work. Next human step: install shadow-v5 + dogfood list above.*
