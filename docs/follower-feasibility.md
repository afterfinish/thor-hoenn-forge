# ORAS overworld follower feasibility

Research only (2026-07-25). Title: Alpha Sapphire USA `000400000011C500`.

## Verdict

| Horizon | Call |
|---------|------|
| **v1.0** | **No** — not a pillar; freecam unfinished |
| **Long-term** | **Maybe** — engine can draw mons; no public ORAS follower mod |
| **One flag flip** | **No** |

**Possible** under Hoenn Forge (CRO hooks / DllField), **impractical for v1**.

---

## Best new evidence

Stock `DllField.cro` already has:

```text
GetPlayerFollowerAcmd
GetPlayerFollowerGridX
GetPlayerFollowerGridZ
```

Likely path-history / escort / multiplayer helper — **not** proven as a party-mon actor. Phase-0 RE: Ghidra xrefs + live log while walking.

Also present: full `field::mmodel` actor system, `MdlAdd`/`MdlSetPos`, PokeRide 3D mon, Amie, status models.

---

## Approaches (ranked)

| Rank | Approach | Feasibility | Notes |
|------|----------|-------------|-------|
| 1 | DllField code + PlayerFollower / Mdl APIs | Med | Best product path |
| 2 | NPC clone + party model | Med | Softlock risk high |
| 3 | Study PokeRide for model load only | Support RE | Not “walk behind” |
| 4 | Pure memory trail | Low | Freecam thrash lesson |
| 5 | Emulator client-side draw | Reject | Mini-engine |
| 6 | Full decomp | Out of scope | SOUL |

---

## Why harder than HG/SS

- 3D models + walk anims (no OW sprite sheet)
- Analog movement, scale/doors, cutscene cams
- Bike / ride / surf / dive / sky each need hide rules
- No public prior art to fork

---

## MVP (if ever)

Outdoor free-roam only; party slot 1; one step behind; auto-hide on battle/interior/bike/ride/surf/cutscene; no talk-to system.

---

## Order of work

1. Finish freecam DllField ownership RE (pillar #1).  
2. Phase-0: prove what `GetPlayerFollower*` actually is.  
3. Only then spawn/bind party lead model.

Full write-up from research session is in conversation history (agent 2026-07-25).
