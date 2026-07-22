# Freecam research brief (Milestone C)

**Delivers:** requirement **#1** — free camera, as free as ORAS allows  
**Integration:** after Milestone A can boot OR/AS; research may start earlier on PC  
**Status:** Interim assist shipped (START menu toggle → ORAS camera cheats). Full stick freecam still research.  

---

## Goal

Right-stick free look in Pokémon Omega Ruby / Alpha Sapphire:

- **Primary:** free-roam overworld — yaw free; pitch clamped; modern feel  
- **Secondary:** useful in caves/routes/cities without softlocking story  
- **Non-goals for v1:** full freecam in battles, every cutscene, or cinematic directors’ mode  

Inspiration: OoT3D community free-camera **code patches** (right stick), not emulator-only hacks.

---

## Why this is hard (ORAS-specific)

| Factor | Implication |
|--------|-------------|
| Locked / cinematic follow cam | Not designed like Zelda freelook |
| Per-area camera scripts | Mauville-like / interiors may hard-set camera |
| Separate battle camera | Different system; defer |
| Cutscenes | Must not desync scripts |
| No stock C-Stick freecam in ORAS | Emulator R-stick → C-Stick alone is insufficient |

Existing **camera cheats** (FOV, zoom L/R, third-person, overhead) prove camera state is in memory and mutable—but also fail in some maps. Use them as a **research lab**, not the final product.

---

## Success criteria

### Must

- [ ] Right stick rotates camera yaw in overworld free roam  
- [ ] Pitch control present with clamps (no infinite flip)  
- [ ] Sensitivity + invert X/Y in app settings  
- [ ] Auto-disable or safe fallback in cutscenes (no softlock on smoke route)  
- [ ] Works with prepared randomized games (patch after randomize, or independent of RomFS randomize)  
- [ ] Documented title IDs + game revisions supported  

### Should

- [ ] Smooth analog response (not 4-way only)  
- [ ] Restore vanilla camera when stick neutral after transition (optional design choice—document)  
- [ ] Blacklist problem maps rather than crash  

### Could (later)

- [ ] Battle freecam  
- [ ] FOV slider tied to freecam  
- [ ] Gyro assist on Thor  

### Smoke-test path (human QA)

1. New game → Route 101–103  
2. Petalburg Woods  
3. Enter/exit Pokémon Center  
4. Dewford / cave segment  
5. Mauville (known dynamic-camera risk)  
6. Gym interior  
7. One cutscene (e.g. early rival / story beat)  
8. Save/reload with freecam settings  

Pass = completable without stuck camera or softlock.

---

## Supported versions (fill as researched)

Track explicitly; freecam patches are **revision-sensitive**.

| Game | Region | Title ID | Update | Patch status |
|------|--------|----------|--------|--------------|
| Omega Ruby | US | _(fill)_ | 1.4? | TODO |
| Alpha Sapphire | US | _(fill)_ | 1.4? | TODO |
| … | … | … | … | … |

v1 recommendation: **US 1.4** (or whatever matches common dump + cheat docs), then expand.

---

## Research method

### Phase R0 — Environment

- [ ] Legal dumps of OR + AS (US first)  
- [ ] Azahar/Citra **desktop** for cheats + RAM  
- [ ] Ghidra/IDA on decompressed `code.bin`  
- [ ] Note tools: 3dsdump / GodMode9 extract notes in dumping guide (no keys in repo)

### Phase R1 — Cheat reconnaissance

Using known ORAS camera cheats (FOV / free zoom / third-person / overhead):

- [ ] Confirm which codes work on our revision  
- [ ] List maps where cheats **fail** (dynamic / locked)  
- [ ] Identify addresses that change when zooming / third-person  
- [ ] Differentiate camera position vs target vs FOV vs mode flags  

**Deliverable:** `research/camera-addresses-or-us.md` (addresses + notes only)

### Phase R2 — Code tracing

- [ ] Find writers to camera position/orientation each frame  
- [ ] Find input readers (Circle Pad, C-Stick / Circle Pad Pro, touch eye icons if any)  
- [ ] Locate mode enum / “scripted camera” flags  
- [ ] Design hook: add C-Stick deltas into yaw/pitch when mode == free-roam  

### Phase R3 — Prototype patch

- [ ] IPS or layered `code.bin` patch for one title+revision  
- [ ] Right stick → freecam in Route 101  
- [ ] Pitch clamp constants  
- [ ] Disable path when scripted flag set (even if coarse)

### Phase R4 — Stabilize

- [ ] Smoke-test path above  
- [ ] Blacklist / auto-off list  
- [ ] Sensitivity scaling (match OoT3D-style hotkeys later if desired)  
- [ ] AS parity patch  

### Phase R5 — Ship integration

- [ ] `patches/freecam/` per titleID + revision  
- [ ] Pipeline stage in onboarding prepare (after randomize)  
- [ ] Settings UI: enable, sensitivity, invert  
- [ ] Graceful skip if revision unsupported  

---

## Design parameters (initial proposals)

| Parameter | Proposal | Notes |
|-----------|----------|-------|
| Yaw | Free, stick X | Continuous |
| Pitch | Stick Y, clamp ~[-35°, +55°] | Tune by feel |
| Roll | Locked | Avoid nausea / engine issues |
| Stick deadzone | Match Thor hall stick profile | |
| When stick zero | Hold last free orient **or** blend to follow cam | Prefer hold in open field; test follow-blend for towns |
| Cutscene | Force vanilla | |
| Battle | Vanilla | |
| Interior | Try free; blacklist if broken | |

---

## Relationship to emulator

| Layer | Role |
|-------|------|
| Azahar core | Maps physical right stick → **C-Stick** (New 3DS) |
| Freecam patch | Game code **reads** C-Stick and applies to camera |
| App settings | Sensitivity multipliers (patch constants or runtime cheat bridge) |

Without the game patch, stick mapping alone will **not** deliver freecam in ORAS.

---

## Interim product (if patch late)

Ship Milestone A/B with:

- Upscale + Thor dual  
- Optional **FOV / zoom cheats** as “Camera assist (beta)”  

Label honestly: not full freecam. Replace when R3+ lands.

### Shipped (2026-07-22) — two START menu toggles

1. **Free look (right stick)** — native driver `core/hoenn_freecam.cpp`
   - Reads C-Stick (right stick mapping)
   - Writes yaw/pitch into ORAS overworld camera object (`ptr @ 0x5F67DC`)
   - Pitch at `+0x98`; yaw candidates nearby; keeps wide FOV
   - **Experimental** — field layout is revision-sensitive; may do nothing or glitch on some maps
   - Toggle: `HoennPrefs.freelookEnabled` + `NativeLibrary.setHoennFreelook`

2. **Camera zoom assist** — Gateway cheats via `HoennFreecam.kt`
   - Wide FOV + **L/R** zoom
   - Not freelook; separate from right-stick toggle

- **Limits:** dynamic cameras (Mauville, gyms, cutscenes) often lock the camera
- **Title filter:** ORAS title IDs only (`OrasTitles`)
- **Next research:** exact yaw field / code.bin patch for reliable freelook

---

## Legal / hygiene

- Research notes and IPS diffs only in git  
- No full `code.bin` blobs if avoidable (prefer IPS)  
- No distribution of game files  
- Credit community prior art in README (camera cheat authors, freecam methodology from other 3DS games) without copying incompatible licenses blindly  

---

## Exit criteria (Milestone C done)

1. US OR (and ideally AS) freecam patch in `patches/freecam/`.  
2. Onboarding/prepare applies it automatically when revision matches.  
3. Settings control sensitivity/invert/enable.  
4. Smoke-test path passed on Thor via Hoenn Forge APK.  
5. Research notes checked in under `research/` for future regions.  

---

## References (methodology)

- OoT3D free camera / single-screen mods (GameBanana community) — **pattern**, not code drop-in  
- ORAS camera cheat collections (Citra/Azahar cheat format) — address discovery  
- 3DS code patching: Luma IPS / code.bin patterns — packaging analogue for emulator mod dirs  
