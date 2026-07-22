# Randomizer options (pk3DS-class) — product + data model

Options chosen during onboarding when the user selects **Randomized**.  
Original dump is never modified; work happens on a copy under app storage.

## Global

| Field | Type | Notes |
|-------|------|--------|
| seed | UInt64 / string | Shown, re-roll, manual entry |
| preset | none / light / standard / chaos | Optional starting point |

## Modules (UI exposes all; engine ships by phase)

### Wild encounters
- randomizeSpecies
- randomizeLevels
- allowLegendaries
- typeTheme (none / mono / dual)

### Trainers
- randomizeParties
- randomizeItems
- randomizeMoves
- randomizeAbilities
- difficulty (weaker / similar / stronger / rivalPlus)

### Starters
- mode (vanilla / fullRandom / genLimited / typeBalanced)

### Personal (advanced)
- randomizeTypes
- randomizeBaseStats
- randomizeAbilities
- randomizeTmCompat

### Moves & TMs (advanced)
- randomizeMoveTypes
- randomizeMoveCategories
- randomizeLevelUpLearnsets
- randomizeEggMoves
- randomizeTmList

### Evolutions (advanced)
- randomizeEvolutions (warn softlock)

### Misc (advanced)
- randomizeSpecialMarts
- randomizeStaticGifts

## Presets

| Preset | Enables |
|--------|---------|
| Light | wilds species+levels (no legends), trainers parties similar, starters fullRandom |
| Standard | Light + trainer items/moves/abilities similar, legends off |
| Chaos | most modules on, legends on, evolutions on, stronger trainers |

## Engine phases

1. **UI + config + prepare shell** (this sprint)
2. **Extract RomFS/ExeFS on device**
3. **Wild + trainer + starter modules**
4. **Advanced modules**
5. **Repack / layered load into Azahar**
