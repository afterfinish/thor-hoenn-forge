# Azahar integration plan (Milestone A — next slice)

## Goal

Boot the user’s validated OR/AS dump inside Hoenn Forge with **Thor defaults** from `profiles/thor.json`, without a separate Azahar install.

## Approach

1. Add **azahar-emu/azahar** as a git submodule under `emulator/azahar` (GPL-3).  
2. Build the Android target from that tree (or extract `src/android` + native core).  
3. Replace Azahar’s multi-game launcher entry with our onboarding → Home → **Play**.  
4. On Play: pass dump URI/path into the emulator frontend and apply Thor config once.  
5. Keep `applicationId` = `dev.tzigdon.hoennforge` so it coexists with stock Azahar.

## Why not in the first shell commit

- Full Azahar clone is large (native + submodules).  
- First achievement: **onboarding + legal dump validation + buildable APK**.  
- Emulator embed is the next discrete achievement (commit → push → MEMORY).

## Local dump (this machine)

| Field | Value |
|-------|--------|
| File | `local/dumps/Pokemon Alpha Sapphire - dump.3ds` (gitignored) |
| Header | NCSD |
| Title ID | `000400000011C500` |
| Game | Alpha Sapphire (USA) |
| Size | 2.00 GB |

## Play button today

Shows a toast: dump registered; core integration pending.
