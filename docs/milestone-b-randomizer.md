# Milestone B — On-device randomizer (pk3DS-class)

**Delivers:** requirement **#5**  
**Depends on:** Milestone A (dump in app, boot path, storage strategy)  
**Status:** UI + config + prepare shell in progress (engine modules pending)  

---

## Goal

In onboarding, after dump validation, the user can:

1. Choose **Vanilla** or **Randomized**  
2. If randomized: pick **preset** + **seed** (advanced toggles optional)  
3. Wait for prepare  
4. Play a **stable** randomized OR/AS build  

Original dump file is **never** modified.

---

## What “using pk3DS” means

Desktop [pk3DS](https://github.com/kwsch/pk3DS) = `pk3DS.WinForms` + **`pk3DS.Core`** (C#, GPL-3).

| Approach | Use |
|----------|-----|
| **A. Prefer** | Headless **`pk3DS.Core`** (or extracted algorithms) driven from app | Closest parity |
| **B** | Port ORAS randomizer modules to Kotlin | More control, more rewrite |
| **C** | “Randomize on PC, import” | **Rejected** for product goal |

**Not acceptable:** shipping `pk3DS.exe` + Wine as the UX.

---

## Pipeline

```text
User dump (read-only)
  → copy into app-private work dir
  → decrypt if required (or require decrypted input)
  → extract RomFS + ExeFS (pk3DS expects folder layout)
  → run randomizer modules with seed
  → (Milestone C) apply freecam code patch
  → package for Azahar load (rebuilt image and/or layered content)
  → index as “active prepared game”
  → boot
```

### Storage

- Check free space before copy/extract (multi-GB).  
- Keep: `original_ref`, `prepared/`, `saves/`, `last_seed.json`.  
- Cancel cleans partial `prepared/`.  

---

## v1 randomizer scope

### Presets

| Preset | Intent |
|--------|--------|
| **Light** | Wilds + trainers mild; starters random; playable for newcomers |
| **Standard** | Classic pk3DS-style full wilds/trainers |
| **Chaos** | Aggressive options; warn “may be unfair / softlock risk” |

### Always

- Seed display + re-roll + manual entry  
- Reproducibility: same app version + same dump revision + same seed + same preset ⇒ same result (document any caveat)

### v1 modules (minimum)

- [ ] Wild encounters (species, levels; legends toggle)  
- [ ] Trainer parties (and basic difficulty)  
- [ ] Starters  

### Explicitly later

- Full personal/stats/abilities/TM soup  
- Move randomizers  
- Mart inventories  
- Every desktop checkbox  

---

## UX

See [onboarding-ux.md](onboarding-ux.md) screens **S5–S7**, **New run**.

---

## Engineering checklist

- [ ] Extract tooling on Android (or NDK port of known 3DS extractors)  
- [ ] Map pk3DS folder contract (`romfs` / `exefs`)  
- [ ] Seed plumbing into Core  
- [ ] OR + AS paths  
- [ ] Progress callbacks to UI  
- [ ] Failure recovery  
- [ ] Boot prepared game via Azahar core  
- [ ] GPL notices for pk3DS  

### Testing

- [ ] Light preset completes early game  
- [ ] Fixed seed reproduces twice  
- [ ] Vanilla path still works  
- [ ] Low-storage failure is clean  

---

## Exit criteria

1. On-device randomize from onboarding without PC.  
2. Original dump untouched on disk.  
3. At least Light + Standard presets reliable on US OR smoke path.  
4. Seed shown on Home.  
5. Documented limitations vs desktop pk3DS.  

---

## Risks

| Risk | Mitigation |
|------|------------|
| Core assumes Windows paths / WinForms bits | Isolate pure logic; fix path abstraction |
| Extract too slow / heavy | Progress UI; maybe keep extracted cache |
| Chaos softlocks | Warnings; prefer Light default |
| Update revision mismatch | Detect revision; refuse or warn |
