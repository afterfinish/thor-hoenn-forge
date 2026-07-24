# Interior / floor freecam death — diagnosis (2026-07-24)

## Symptom class

Same family as post-battle freelook death and community “need map refresh”:

- Official pointer `*0x085F67DC` may still look like a heap cam.
- Stick Y → `+0x98` / X → `+0x9C` works in overworld and often house **1F after recovery**.
- Dies on **house enter**, **2F**, and stays dead on return to 1F until **leave map** (and often until an explicit re-acquire).

## User-proven recovery

Opening **START → Cam address probe** (which runs `ScanCamCandidates` / recover + sticky hunt) after a **fresh** house enter restores freelook. User does not need to pick a candidate number.

After visiting **2F**, the same menu path **fails** until leave house + re-enter + probe again.

## Failed approaches this session

| Approach | Result |
|----------|--------|
| Numbered heap candidates (FOV-scored) | False hits; FOV-only visual junk |
| Pitch/yaw-only probe addresses | None worked on 2F |
| Sticky write test (first sticky wins) | ~all floats “sticky”; live cam often unsticky |
| Instant auto-recover on FOV snap | Did not replace manual cam-probe open |

## Working theory

```text
[overworld cam live]
      │ enter door
      ▼
[interior cam constructing / slot stale / our drive_override wrong]
      │ delay + re-acquire (cam probe open) ──► 1F freelook OK
      ▼ go upstairs
[2F: different cam mode OR our writes poison OR +0x98 unused]
      │ rescan fails
      ▼ back 1F
[still dead — need full map unload]
      │ leave house
      ▼
[field cam rebuilt]
      │ cam probe re-acquire
      ▼
[OK again]
```

## Experiments for human dogfood

See **MEMORY.md** experiments A–D (poison, pause-only, hex note, logcat).

## Engineering direction

1. Treat recovery as **re-acquire drive base** (clear override, reseed slot, optional hunt), not new FOV addresses.  
2. **Delay and multi-shot** re-acquire after transitions.  
3. **Stop writing** while dead if poison test (A) confirms.  
4. Use **game-driven pitch variance** (freelook off, player turns) to find live objects — inverse of sticky-write.  
5. Never rewrite `CAMERA_SLOT`.
