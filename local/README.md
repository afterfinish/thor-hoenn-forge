# Local-only files (gitignored)

This folder is for **your** machine. Dumps, extracts, and keys stay here.  
**Nothing under `local/` is committed** except this README.

## Layout (create as needed)

```text
local/
  dumps/           ← your .cci / .3ds / .cia of OR or AS (owned dump only)
  extracted/       ← romfs + exefs after unpack (for pk3DS / freecam later)
  keys/            ← aes_keys.txt etc. if you use them (never share)
  azahar-user/     ← optional copy/link notes of Azahar user dir
  notes.txt        ← optional private notes
```

## What the project needs from you (by phase)

| Phase | Put here | Do I need the full ROM in chat/git? |
|-------|----------|-------------------------------------|
| **A Shell** | Path to dump so *you* can test; optional copy in `local/dumps/` | **No** — coding does not need the ROM in the repo |
| **B Randomizer** | Extracted `romfs`/`exefs` or dump in `local/` for on-device pipeline tests | **No** in git; agent may read `local/` on this PC only |
| **C Freecam** | `code.bin` (or full extract) under `local/extracted/` | **No** in git; research reads local files only |

## Hard rules

1. Never copy dumps into the repo root or `docs/`.
2. Never paste ROM bytes, keys, or full `code.bin` into GitHub issues/chat uploads.
3. Prefer **decrypted `.cci`** for Azahar + our pipeline.
4. After you place files, tell the agent: “dump is at `local/dumps/...`” (filename only is fine).
