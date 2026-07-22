# Hoenn Forge

**Modern Pokémon Omega Ruby / Alpha Sapphire on Android** — especially the [AYN Thor](https://www.ayntec.com/) — as a single APK: your dump, upscaled presentation, optional randomizer, free camera when the world allows.

> **Unofficial fan project.** Not affiliated with Nintendo or The Pokémon Company.  
> **This repository and the app do not include any game ROMs.** You must provide a dump of a game you own.

---

## Product goals

| # | Goal | v1 |
|---|------|----|
| 1 | Free camera (right stick, as free as ORAS allows) | Required |
| 2 | Upscaled resolution (native FPS OK) | Required |
| 3 | Emulator baked in — Thor settings OOTB | Required |
| 4 | Single-screen toggle (other handhelds) | Optional, last |
| 5 | Randomizer in onboarding (pk3DS-class) | Required |
| 6 | Pick legal dump — no ROMs in project | Required |

Inspiration: Dusklight-style “install → point at dump → play modern,” plus OoT3D-class free-camera **patches** (not a full ORAS decomp port).

---

## Status

**Milestone A prototype ready for install.** See [docs/install-prototype.md](docs/install-prototype.md).

| Milestone | Doc | Status |
|-----------|-----|--------|
| Soul / working contract | [SOUL.md](SOUL.md) | Done |
| Master plan | [docs/PLAN.md](docs/PLAN.md) | Done |
| **A** Shell (emu + dump + upscale) | [docs/milestone-a-shell.md](docs/milestone-a-shell.md) | **Prototype APK built** — install & dogfood |
| Onboarding UX | [docs/onboarding-ux.md](docs/onboarding-ux.md) | Spec done; A subset implemented |
| **B** Randomizer | [docs/milestone-b-randomizer.md](docs/milestone-b-randomizer.md) | Not started |
| **C** Freecam | [docs/freecam-research.md](docs/freecam-research.md) | Not started |
| **D** Single-screen | [docs/milestone-d-single-screen.md](docs/milestone-d-single-screen.md) | Not started |

Build: see [docs/building.md](docs/building.md).

---

## Architecture (target)

```text
APK (GPL-3, no game files)
  ├── Kotlin onboarding (dump → randomize → prepare)
  ├── Pipeline (extract → pk3DS-class randomize → freecam patch)
  └── Azahar-based emulator core (Thor profile baked in)
```

---

## Legal

- Provide your **own** Omega Ruby / Alpha Sapphire dump.  
- Do not open issues asking for ROMs or download links (they will be closed).  
- Upstream components (e.g. Azahar/Citra lineage, pk3DS) are **GPL-3**; this project will be distributed under compatible terms when code lands.  
- See [SOUL.md](SOUL.md) for non-negotiables.

---

## Contributing / AI assistants

Read **[SOUL.md](SOUL.md)** and **[MEMORY.md](MEMORY.md)** first, then **[docs/PLAN.md](docs/PLAN.md)** and the milestone you are working on.  
Milestone **A** before B/D product work; freecam research may proceed in parallel after A boots a dump.

**After every small achievement:** commit → push → update `MEMORY.md`.

---

## Name

**Hoenn Forge** — forge your own modern Hoenn run from a dump you own.
