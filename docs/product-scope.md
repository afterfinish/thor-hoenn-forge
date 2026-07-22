# Product scope — requirements 1–6

## Goals (user)

| # | Requirement | v1 | Notes |
|---|-------------|----|-------|
| **1** | Free camera controls — as free as it can get | **Required** | Overworld priority; clamp/disable where scripts break |
| **2** | Upscaled resolution | **Required** | FPS may stay native; not a success metric |
| **3** | Emulator baked into APK | **Required** | Perfect Thor settings out of the box; no tinkering |
| **4** | Single-screen toggle | **Optional / last** | For other Android handhelds |
| **5** | Randomize before start (pk3DS-class) | **Required** | Baked into onboarding sequence |
| **6** | Choose legal dump on onboarding | **Required** | **No ROM files in the project** |

---

## v1.0 vs later

### Must ship in v1.0

- Dump picker + OR/AS validation (#6)
- Embedded emulator core with Thor defaults (#3, #2)
- Randomizer presets + seed (#5 subset)
- Freecam overworld + settings for sensitivity / invert (#1)

### May ship in v1.1+

- Full pk3DS option parity
- Single-screen toggle (#4)
- Battle freecam
- Multi-language dump first-class QA
- Texture pack hooks
- FPS unlock experiments

### Never (unless project mission changes)

- Full ORAS decompilation / native reimplementation as the product spine
- Shipping game data
- Closed-source binary-only release of GPL-combined work

---

## Supported games (v1)

| Game | Priority |
|------|----------|
| Pokémon Omega Ruby | Primary |
| Pokémon Alpha Sapphire | Primary |
| X / Y / SuMo / USUM | Out of v1 |

Target **one region first** (recommend **US English** dumps), then expand title IDs.

---

## Supported devices

| Device | Priority |
|--------|----------|
| AYN Thor (8 Gen 2 class) | Design target |
| Other dual-screen Android | Best-effort dual layout |
| Single-screen Android handhelds | After #4 |
| Phones / tablets | Best-effort |

---

## Legal product copy (use in app)

> Hoenn Forge does not include Pokémon Omega Ruby or Alpha Sapphire.  
> You must provide a dump of a game **you own**.  
> This project is unofficial and not affiliated with Nintendo or The Pokémon Company.

---

## Acceptance tests (product-level)

1. Fresh install on Thor → complete onboarding with a legal dump → reach overworld without opening any hidden emulator menu.
2. Graphics look clearly upscaled vs stock 3DS res.
3. New randomized run with a fixed seed is reproducible on the same app version + same dump revision.
4. Right stick moves camera in overworld; story can still be completed (no softlock on smoke-test path).
5. Uninstalling the app does not require the original dump file to have been modified on disk (we only copy).
