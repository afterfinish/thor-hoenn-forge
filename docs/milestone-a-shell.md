# Milestone A — Shell (baked emulator + dump + upscale)

**Delivers:** requirements **#6**, **#3**, **#2**  
**Blocks:** playable foundation for B (randomizer), C (freecam integration), D (layouts)  
**Status:** Not started  

---

## Goal

Install one APK → pick legal ΩR/αS dump → play **upscaled** with **Thor-tuned** controls and dual-screen layout.  
No separate Azahar install. No settings rabbit hole. **No ROMs in the project.**

---

## Technical approach

1. Fork / vendor **Azahar** Android (Citra lineage) under GPL-3.  
2. Replace multi-game launcher with Hoenn Forge onboarding + home.  
3. Bake a **Thor profile** as default config.  
4. Auto-load the user-selected dump after onboarding.  
5. Strip or hide advanced emu UI for first-run path (power-user settings can exist later behind “Advanced”).

### Suggested package identity

- Application name: **Hoenn Forge**  
- `applicationId`: e.g. `dev.tzigdon.hoennforge` (finalize at first Android module)  
- Do **not** reuse Azahar’s applicationId if we want side-by-side install with stock Azahar.

---

## Onboarding slice for A only

For Milestone A, onboarding is **minimal**:

```text
Welcome + legal notice
  → Grant storage / SAF access
  → Pick dump file
  → Validate title (OR or AS)
  → Copy or register path into app storage index
  → Apply Thor profile
  → Boot game
```

Randomizer (#5) and freecam patch (#1) are **stubs/skipped** until B/C.  
UI may show disabled “Coming soon” for randomize if we want the full wireflow shell early—prefer **hide** until B to avoid false expectations.

Full UX (including B/C): see [onboarding-ux.md](onboarding-ux.md).

---

## Dump validation

Accept (document exact list during implementation):

| Format | Notes |
|--------|--------|
| `.cci` | Preferred (decrypted cart image) |
| `.3ds` | Often rename-equivalent; verify Azahar load path |
| `.cia` | May need install-into-nand flow; decide A vs later |
| `.cxi` | Optional |

**Reject with clear errors:**

- Wrong title (not OR/AS)
- Unreadable / encrypted without keys (message: use decrypted dump)
- Truncated file

Store metadata only:

```text
title_id, game (OR|AS), region, file_name, content_uri or private copy path, added_at
```

Never commit dumps. Prefer **copy into app-private storage** OR durable SAF URI with persistable permission—pick one strategy in implementation and document it.

**Recommendation:** copy into app-private storage for pipeline simplicity in B (extract/randomize). Warn about free space.

---

## Thor default profile (starting point — tune on device)

Record final values in `profiles/thor.json` when code exists.

| Setting | Starting recommendation | Tune notes |
|---------|-------------------------|------------|
| Internal resolution | **3x** or **4x** | Raise if stable & cool enough |
| Graphics API | **Vulkan** first | GLES fallback if black screen / glitches |
| Accurate mul / shaders | Balanced quality | Prefer stability over max shaders |
| Layout | **Dual screen** | Top = 3DS top, bottom = 3DS bottom |
| Top screen stretch | Smart fit / integer if available | Avoid ugly stretch on 16:9 |
| New 3DS mode | **ON** | Needed for C-Stick path later |
| Circle Pad | Left stick | Full analog |
| C-Stick | Right stick | Freecam will use this later |
| Touch | Bottom screen touch + optional pointer | Map bag/DexNav |
| Frame limit | Game default | Do not chase 120 FPS in A |
| Audio | Default | |
| Save paths | App-scoped | Document for user backups |

### Dual-screen layout (Thor)

- Physical: top ~6" 120 Hz, bottom ~3.92"  
- Default: world on top, touch UI / map on bottom  
- Optional later: bottom map emphasis for DexNav randomizers  

Device detection: model strings / screen count if available; else default dual-friendly layout that still works single-screen (stacked or large top).

---

## Azahar fork checklist

### Project integration

- [ ] Add Azahar as `git submodule` or subtree under `emulator/` (document choice)
- [ ] Build Android target from our repo root / Gradle wrapper
- [ ] New app name, icon, package id
- [ ] LICENSE + NOTICE for third-party (Azahar, dependencies)
- [ ] CI: assemble debug APK **without** any dump fixtures

### UX replacement

- [ ] Custom `Splash` / `WelcomeActivity`
- [ ] Legal notice screen (must accept to continue)
- [ ] Dump picker (SAF)
- [ ] Validation + error screens
- [ ] Home: **Continue** / **Change dump** / **Settings** (minimal)
- [ ] Launch path: `LoadGame(preparedOrOriginalPath)` with no game list browser required

### Config baking

- [ ] On first run, write Thor defaults into Azahar config store
- [ ] “Reset graphics to Thor defaults” in Settings
- [ ] Hide or nest raw Citra/Azahar settings under Advanced

### Controls

- [ ] Default bindings for Thor hall sticks + buttons
- [ ] Document button map in README (A/B/X/Y Nintendo layout)
- [ ] Deadzones sensible for hall sticks

### Performance smoke (Thor)

- [ ] Littleroot → Route 101: stable, no audio crackle
- [ ] Mauville / contested areas: note issues
- [ ] Thermal: 30+ min session acceptable
- [ ] Battery note in docs (optional)

### Safety / hygiene

- [ ] `.gitignore`: `*.cci`, `*.3ds`, `*.cia`, `*.cxi`, `*.app`, `nand/`, `sdmc/`, keys
- [ ] No sample ROMs in `androidTest`
- [ ] README dumping guide points to **user’s hardware**, not downloads

---

## Exit criteria (Milestone A done)

1. Clean clone → build APK → install on Thor.  
2. Onboarding completes with user dump only.  
3. Game boots to title / overworld with **clearly upscaled** presentation.  
4. Dual-screen usable; sticks/buttons correct.  
5. Save/load works across app restart.  
6. Zero game data in git history.  
7. Short `docs/milestone-a-results.md` notes final resolution scale and any Thor-specific workarounds.

---

## Explicitly not in A

- Randomizer pipeline  
- Freecam patch  
- Single-screen polish  
- Full settings suite  
- Multi-region QA  

---

## Suggested first implementation tasks (when coding begins)

1. Repo skeleton: Gradle app module + empty onboarding screens.  
2. Vendor Azahar Android; prove stock build.  
3. Wire dump pick → load game.  
4. Inject Thor defaults.  
5. Replace branding; legal screens.  
6. Dogfood on Thor; lock profile numbers.

---

## Open decisions (resolve during A)

| Decision | Options | Lean |
|----------|---------|------|
| Copy dump vs SAF-only | Copy / URI | **Copy** for B pipeline |
| CIA support in A | Yes / Later | **Later** if painful |
| Advanced settings | Hidden / Visible | **Hidden** behind Advanced |
| Upstream pin | Commit hash | Pin + document upgrade process |
