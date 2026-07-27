# Hardcore-nuzlocke level cap

Holds every party Pokémon at or below the cap for the boss you are about to fight. At the
cap a Pokémon keeps battling and keeps winning — it just never banks the experience.

Status: **built, not yet proved on hardware.** Enforcement is compiled in but disabled;
see [Arming it](#arming-it).

## The ladder

Thirteen stages, from [Nuzlocke University's ORAS
table](https://nuzlockeuniversity.ca/2022/01/18/hardcore-nuzlocke-level-caps-by-generation/#omega-ruby/alpha-sapphire).
Stage *n* is the cap in force *before* beating boss *n*, so a fresh save starts at stage 0
capped at 14.

| Stage | Boss | Cap | | Stage | Boss | Cap |
|---|---|---|---|---|---|---|
| 0 | Roxanne | 14 | | 7 | Wallace | 46 |
| 1 | Brawly | 16 | | 8 | Sidney | 52 |
| 2 | Wattson | 21 | | 9 | Phoebe | 53 |
| 3 | Flannery | 28 | | 10 | Glacia | 54 |
| 4 | Norman | 30 | | 11 | Drake | 55 |
| 5 | Winona | 35 | | 12 | Steven | 59 |
| 6 | Tate & Liza | 45 | | | | |

Post-game rematch caps (72/73/74/75/79) are not implemented; they are four more rows in
`kCaps` and `LevelCapLadder.STAGES` whenever someone wants them.

## How it works

No code patch. `Hoenn::LevelCap` (`overlay/azahar/core/hoenn_levelcap.{cpp,h}`) is ticked
from the cheat-engine event that already drives `Hoenn::FreeCam`
(`overlay/azahar/core/cheats.cpp`), throttled to its own ~5 Hz so free look pulling that
event to 60 Hz does not drag a heap sweep along with it.

Each pass:

1. Locate the party, if not already cached.
2. Re-validate the cached location; a failure demotes to a fresh sweep.
3. For each slot, if experience has run past the cap, write back the first experience
   value of the cap level.

Holding a Pokémon at level *N* means writing `ExpForLevel(growthRate, N)`, so the module
needs each species' growth curve. That comes from the personal table (`a/1/9/5`, offset
`0x15` of each `0x50`-byte entry), extracted during prepare by
`randomizer/GrowthTable.kt` and written to `filesDir/prepared/growth_rates.bin`. Reading
it from the player's own dump rather than hardcoding it means it cannot drift from the
game actually running.

### Finding the party

By signature sweep, not a hardcoded address. `CAMERA_SLOT` can afford to be a build
constant because a wrong guess there draws a bad frame; a wrong guess here writes to a
save file.

The sweep walks `0x08000000`–`0x0A000000` a page at a time (skipping unmapped pages,
2 MB per tick, so a cold start finds the party within a couple of seconds) looking for a
260-byte PK6 party slot. A candidate must pass all of:

- sanity word at `+0x04` is zero
- species at `+0x08` in 1..721
- level at `+0xEC` in 1..100
- max HP in 1..1000, current HP ≤ max HP
- all five stats at `+0xF4`..`+0xFC` in 1..1000
- **experience, level and the species' growth curve agree** — `ExpForLevel(rate, level) ≤
  exp < ExpForLevel(rate, level+1)`

That last one is the real discriminator: three independent facts that only line up on an
actual Pokémon. On a hit the sweep walks backwards in `0x104` strides to slot 0 and counts
the party.

### Two safety properties

**Fail open.** Party not found, growth curve unknown, stage unknown — any uncertainty and
the module does nothing. A cap that silently stops applying is a disappointing run; a cap
that writes to the wrong address is a destroyed save.

**Observe before enforce.** `SetEnforce(false)` runs the entire pipeline and logs what it
would have written without touching guest memory.

The checksum is only maintained if the copy in RAM is already maintaining one — we sum
`0x08`..`0xE7` before writing and compare against the stored value. If the game lets it go
stale during play and refreshes on save, writing our own would be the wrong value.

## Open assumption

**PK6 is assumed to be plaintext and unshuffled in RAM.** That is the form the game needs
to work in while playing, but it is an assumption, not a measured fact — the save file
stores the four 56-byte blocks shuffled and XOR'd against a PID-keyed PRNG. If ORAS keeps
*that* form live, every field offset above is wrong, validation never passes, and the
module stays inert rather than writing nonsense. That is the intended failure mode, but it
means "the cap did nothing" has two possible causes and the log distinguishes them.

## Arming it

`HoennLevelCap.OBSERVE_ONLY` is `true`. Flip it to `false` once the checklist below passes
on hardware.

### Device checklist

Needs the Thor connected and a real save with a party.

1. Onboard with the cap on, or turn it on from the START quick menu.
2. `adb logcat -c`, play for ~30 s, then `adb logcat -d | grep "Hoenn level cap"`.
3. **Party located:** expect `party found at 0x08xxxxxx, N member(s)` with N matching your
   actual party size. If it never appears, the plaintext assumption above is wrong — dump
   the heap and check whether the blocks are shuffled.
4. **Addresses correct:** set the stage below your party's level so clamping would trigger,
   and check the observe lines report levels that match the party screen. Wrong levels mean
   a false-positive match, not a layout problem.
5. **Stability:** confirm the sweep does not cost visible frames on a cold start, and that
   `party at … no longer validates, rescanning` does not spam on map transitions.
6. Only then set `OBSERVE_ONLY = false`, rebuild, and verify on a **throwaway save** that
   experience actually stops at the cap and that saving and reloading keeps the party
   intact — that is the checksum path.

### While you are in there

Locate the badge bitfield so stages 0–8 can advance themselves. With the party validated
you have an anchor: snapshot the heap either side of earning a badge and diff. Badges
cannot express the Elite Four (stages 8–12 all sit past the eighth badge), so manual
advance stays as the mechanism for those and as an override throughout.

## UI

- **Onboarding** — `LevelCapActivity`, between play mode and prepare/randomizer, so it
  applies to a vanilla run and a randomized one alike. On or off; a run always starts at
  stage 0.
- **START quick menu** — one row that cycles `Off → Roxanne → … → Steven → Off`. A closed
  cycle, so no state is unreachable and nothing there is a one-way door. The chip shows the
  live cap; the row says `Cap on, party not found yet` while the sweep is still looking.
