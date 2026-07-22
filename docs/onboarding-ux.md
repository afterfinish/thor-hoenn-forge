# Onboarding UX wireflow

End-to-end user experience for Hoenn Forge.  
Implements product requirements **#6** (dump), **#5** (randomize), and entry into play (**#2/#3/#1**).

Milestone A ships a **subset** (welcome → dump → play).  
This doc is the **full target flow** so UI does not need a redesign later.

---

## Design principles

1. **Zero emulator literacy** — no “add games directory,” no resolution jargon on first run.  
2. **Honest legality** — dump ownership is front and center.  
3. **Progress is visible** — prepare can take minutes; always show stage + cancel when safe.  
4. **Never destroy the user’s dump** — only read; write only to app storage.  
5. **Reversible** — “New run” and “Change dump” always available from Home.  
6. **Thor-first defaults** — settings exist, but first path needs none.

---

## Screen map

```text
[S0] Splash
  → [S1] Welcome / Legal
  → [S2] Storage access (if needed)
  → [S3] Choose dump
  → [S4] Dump validation
  → [S5] Play mode          (Vanilla | Randomized)     [B+]
  → [S6] Randomizer         (presets + seed)           [B+]
  → [S7] Preparing…         (pipeline progress)        [B+/C]
  → [S8] Ready / First tip
  → [S9] Emulation (game)

Anytime after first success:
  [H0] Home
  [H1] Settings
  [H2] New run
  [H3] Change dump
```

---

## Screen-by-screen

### S0 — Splash

- App icon + **Hoenn Forge**  
- Short load; no gameplay yet  
- If already onboarded → **H0 Home** (skip to continue)

---

### S1 — Welcome / Legal

**Copy (required meaning, polish later):**

- Title: Welcome to Hoenn Forge  
- Body:  
  - Unofficial fan project; not affiliated with Nintendo / TPC  
  - **Does not include** Omega Ruby or Alpha Sapphire  
  - You must select a dump of a game **you own**  
  - Emulator-enhanced experience (upscale, optional randomizer, free camera)  
- Checkbox: “I own this game and will provide my own dump.”  
- Primary: **Continue** (disabled until checkbox)  
- Secondary: **How do I dump?** → opens in-app doc / link to `docs` dumping guide (no ROM sources)

**Exit:** S2 or S3

---

### S2 — Permissions / storage

- Explain: “We need access to read your dump file. Prepared games and saves stay in app storage.”  
- Button: **Choose access** (SAF)  
- Fail path: clear error + retry  

**Exit:** S3

---

### S3 — Choose dump

- Primary: **Select dump file**  
- Helper text: Preferred **decrypted `.cci`** of Omega Ruby or Alpha Sapphire  
- Show last error if returning from S4  

**Exit:** S4

---

### S4 — Dump validation

**UI states:**

| State | UI |
|-------|-----|
| Checking | Spinner: “Checking dump…” |
| Success | Card: game name (ΩR/αS), region if known, size |
| Fail | Icon + plain-language reason + **Pick another file** |

**Success actions:**

- Milestone A: **Play** → S7 (copy only) or straight S9 if no prepare  
- Full product: **Continue** → S5  

**Validation rules (logic):**

- Readable file  
- Recognized container  
- Title ID ∈ allowed OR/AS set  
- Optional: warn if update revision unknown for freecam/randomizer  

**Exit:** S5 (full) / prepare or play (A)

---

### S5 — Play mode

- **Vanilla** — no randomizer; still may apply freecam + upscale  
- **Randomized** — go to S6  
- Fine print: “Randomizing creates a new prepared copy. Your original dump is not modified.”

**Exit:** S6 or S7

---

### S6 — Randomizer

See also [milestone-b-randomizer.md](milestone-b-randomizer.md).

**Layout:**

1. **Presets** (large cards)  
   - Light  
   - Standard  
   - Chaos  
2. **Seed**  
   - Show current seed  
   - **Re-roll**  
   - **Edit** (advanced)  
3. **Advanced** (collapsed)  
   - Wilds: species, levels, legends toggle  
   - Trainers: parties, items, difficulty  
   - Starters  
   - (Later: personal/stats, moves — not required for v1)  
4. Primary: **Prepare randomized game**  
5. Secondary: **Back**

**Copy:** “Powered by pk3DS-class randomization. Not all desktop options are in v1.”

**Exit:** S7

---

### S7 — Preparing

**Stages (progress bar + label):**

1. Copying dump into app storage  
2. Extracting game data  
3. Applying randomizer (if any)  
4. Applying free camera patch (if any)  
5. Finalizing  

**Rules:**

- Estimate or indeterminate progress if needed  
- **Cancel** returns to S5/S6; delete partial work  
- On failure: keep original dump reference; show log snippet + “Try vanilla” / “Retry”  
- Disk space check **before** stage 1; block with “Need ~X GB free”

**Exit:** S8 or error

---

### S8 — Ready / First tip

- “You’re ready.”  
- Tips (one screen, dismissible):  
  - Right stick: look around (when freecam available)  
  - Bottom screen: bag / map (Thor dual)  
  - Home button / overlay: pause & settings  
- Primary: **Start adventure**  

**Exit:** S9

---

### S9 — Emulation (game)

- Fullscreen dual layout (Thor default)  
- In-game overlay (tap or guide button):  
  - Pause  
  - Settings (cam sensitivity, single-screen later)  
  - Exit to Home  
- No game browser  

---

## Home (H0) — after first successful prepare

```text
┌─────────────────────────────────────┐
│  Hoenn Forge                        │
│  Omega Ruby · Randomized · seed … │
│                                     │
│  [ Continue ]                       │
│  [ New run ]                        │
│  [ Change dump ]                    │
│  [ Settings ]                       │
└─────────────────────────────────────┘
```

| Action | Behavior |
|--------|----------|
| Continue | Boot last prepared game + saves |
| New run | S5 → optional re-randomize; warn saves are per-run or prompt |
| Change dump | Confirm → S3; does not delete saves until confirmed |
| Settings | H1 |

**Save policy (decide in B, document in UI):**

- Recommendation: **one active run** with named slots later  
- v1: one prepared game + its save; New run archives or overwrites with confirm  

---

## Settings (H1)

### Player-facing (always)

- Free camera: sensitivity, invert X/Y, enable/disable  
- Display: reset Thor defaults; (later) single-screen toggle  
- Audio volume  
- About / licenses / source link  

### Advanced (collapsed)

- Graphics scale override  
- Graphics API Vulkan/GLES  
- Open raw emulator debug (optional, late)  
- Clear prepared data  
- Export save  

---

## Error copy guidelines

| Situation | Tone |
|-----------|------|
| Encrypted dump | “This dump looks encrypted. Export a decrypted `.cci` from your console dump tools, then try again.” |
| Wrong game | “This isn’t Omega Ruby or Alpha Sapphire.” |
| Low space | “Need about X GB free to prepare the game.” |
| Randomizer fail | “Randomizing failed. Your original dump is fine. Try Light preset or Vanilla.” |
| Freecam patch mismatch | “Free camera isn’t available for this game version yet. You can still play upscaled.” |

Never imply the user should download a ROM.

---

## Milestone mapping

| Screens | Milestone |
|---------|-----------|
| S0–S4, S9, minimal H0/H1 | **A** |
| S5–S7 randomize stages, New run | **B** |
| S7 freecam stage, cam settings | **C** |
| Single-screen in H1 | **D** |

---

## Accessibility & UX polish (later, track)

- Large touch targets for handhelds  
- Don’t require touch for core overworld once in-game (3DS bottom still needs touch for some menus—document)  
- Landscape lock for Thor  
- Avoid tiny Citra-style multi-panel setup wizards  

---

## Wireflow summary (happy path v1.0)

```text
Splash → Legal ✓ → Pick dump → Valid OR
  → Randomized → Preset Standard + seed
  → Preparing (copy → extract → rando → freecam)
  → Tip → Game (upscaled, freecam, Thor dual)
  → next launch: Home → Continue
```
