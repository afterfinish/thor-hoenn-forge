# Hoenn Forge — Master Plan

Living index for product planning. Detailed docs:

| Doc | Purpose |
|-----|---------|
| [SOUL.md](../SOUL.md) | Project identity, non-negotiables, how we work |
| [MEMORY.md](../MEMORY.md) | Session handoff — update after every small achievement |
| [dump-workflow.md](dump-workflow.md) | How to place legal dumps under `local/` (never git) |
| [product-scope.md](product-scope.md) | Requirements 1–6, v1 vs later |
| [milestone-a-shell.md](milestone-a-shell.md) | **A** — baked emulator, dump pick, Thor upscale |
| [onboarding-ux.md](onboarding-ux.md) | Full onboarding wireflow (dump → randomize → play) |
| [freecam-research.md](freecam-research.md) | **C** — free camera research & ship criteria |
| [milestone-b-randomizer.md](milestone-b-randomizer.md) | **B** — pk3DS-class pipeline on device |
| [milestone-d-single-screen.md](milestone-d-single-screen.md) | **D** — optional single-screen (last) |

---

## Vision

Android APK (GPL-3) that:

1. Asks for a **legal** Omega Ruby / Alpha Sapphire dump (no ROMs in project).
2. Optionally **randomizes** via pk3DS-class logic in onboarding.
3. Applies **free camera** patch (as free as ORAS allows).
4. Runs an **embedded Azahar-class** core with **Thor-perfect** defaults (upscale; native FPS OK).
5. Later: **single-screen** toggle for other handhelds.

---

## Architecture (target)

```text
┌──────────────────────────────────────────────────────────┐
│  Hoenn Forge APK (no game files)                         │
│                                                          │
│  Onboarding (Kotlin)                                     │
│    → legal notice → pick dump → randomize → prepare      │
│                                                          │
│  Pipeline (app private storage)                          │
│    dump copy → extract → randomize → freecam patch       │
│                                                          │
│  Emulator core (Azahar fork)                             │
│    baked Thor profile, dual layout, auto-boot            │
│                                                          │
│  Assets we ship: patches, layouts, presets — never ROMs  │
└──────────────────────────────────────────────────────────┘
```

---

## Milestone order

| ID | Name | Delivers | Depends on |
|----|------|----------|------------|
| **A** | Shell | #6 dump pick, #3 baked emu, #2 upscale | — |
| **B** | Randomizer | #5 onboarding randomize | A |
| **C** | Freecam | #1 free camera (research can start after A boots) | A for integration |
| **D** | Single-screen | #4 optional toggle | A (layouts) |

**Rule:** Do not block A on B/C/D. Freecam research may run in parallel on PC after A can boot ORAS.

---

## Definition of done — v1.0

- [ ] APK installs on AYN Thor (and generic Android as best-effort)
- [ ] No ROM/dump in repository or release APK
- [ ] Onboarding: legal notice → file pick → validate OR/AS
- [ ] Optional randomize (presets + seed) → prepared game in app storage
- [ ] Freecam overworld on right stick with sensitivity/invert
- [ ] Upscaled default profile; user never opens raw emulator settings for first run
- [ ] GPL-3 source published with APK
- [ ] Single-screen: optional / may slip to v1.1

---

## Repo layout (target as code lands)

```text
Pokemon-hoenn-forge/
  SOUL.md
  README.md
  LICENSE                 # GPL-3
  docs/                   # plans (this folder)
  app/                    # Kotlin onboarding + settings (later)
  emulator/               # Azahar submodule/fork (later)
  randomizer/             # pk3DS.Core binding or port (later)
  patches/freecam/        # IPS per title + revision (later)
  profiles/               # thor.json etc. (later)
  research/               # freecam notes, never binaries (later)
  .gitignore              # dumps, keys, large artifacts
```

---

## External references

- Azahar: https://github.com/azahar-emu/azahar  
- pk3DS: https://github.com/kwsch/pk3DS  
- OoT3D single-screen / freecam class mods (methodology only): GameBanana / community patches  
- Dusklight: inspiration for UX (dump-in, modern feel), not for ORAS decomp scope  

---

## Status

| Milestone | Status |
|-----------|--------|
| Planning docs | **In progress / seed** |
| A Shell | Not started |
| B Randomizer | Not started |
| C Freecam | Not started (research brief ready) |
| D Single-screen | Not started |

Update this table when milestones start or ship.
