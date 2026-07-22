# How to provide your legal dump for development

Project root on this machine: **`C:\hoenn-forge`**  
Dumps live only under **`C:\hoenn-forge\local\`** (gitignored).

---

## Critical rules

| Do | Don’t |
|----|--------|
| Keep dumps under `local/` | Commit dumps, keys, or `code.bin` |
| Tell the agent the **path** once files are local | Upload ROMs to GitHub / chat |
| Use a dump of a game **you own** | Ask for or download pirated ROMs |
| Prefer decrypted **`.cci`** | Ship `aes_keys.txt` in the repo |

The agent can read files under `C:\hoenn-forge\local\` on your PC for research and testing. That is **not** the same as putting them in git.

---

## What we need (and when)

### Right now (Milestone A — shell)

For **writing code**, no dump is required in the project.

For **you testing** Azahar / later the APK:

1. A working **Omega Ruby** and/or **Alpha Sapphire** dump that already runs in Azahar PC.
2. Optionally a **copy** at:

```text
C:\hoenn-forge\local\dumps\
  omega-ruby.cci          (or .3ds renamed / as-is)
  alpha-sapphire.cci      (optional second title)
```

Also useful (text only, you can paste in chat or `local/notes.txt`):

- Exact **filename** and extension  
- **Game**: OR or AS  
- **Region** (e.g. USA)  
- Whether you have **update 1.4** installed in Azahar  
- That it **boots** in Azahar (yes/no)

### Later (Milestone B — randomizer)

Extracted game tree (or tools run by the agent against your dump):

```text
local/extracted/or-us/
  romfs/
  exefs/
```

### Later (Milestone C — freecam)

Same extract, especially **ExeFS `code.bin`**, for Ghidra/patch work. Still only under `local/`.

---

## Finding your dump if it already runs in Azahar PC

You already “have” the game if Azahar’s game list can launch it. Find the file:

### Method 1 — From Azahar (easiest)

1. Open **Azahar**.
2. In the game list, **right-click** Omega Ruby / Alpha Sapphire.
3. Look for **Open location** / **Show in folder** / **Properties** (wording varies by build).
4. Note the full path to the `.cci` / `.3ds` / `.cia`.
5. **Copy** (do not move) that file into:

```text
C:\hoenn-forge\local\dumps\
```

### Method 2 — Open Azahar data folder

1. In Azahar: **File → Open Azahar Folder** (or similar).
2. That opens the **user** data dir (saves, mods, config)—**not always** where ROMs live.
3. ROMs are usually wherever you put them when you used **Add Game Directory**.
4. Check Azahar settings / game directories list for that path.

### Method 3 — Search your PC

In PowerShell (example):

```powershell
Get-ChildItem -Path $env:USERPROFILE, D:\, E:\ -Recurse -Include *.cci,*.3ds,*.cia -ErrorAction SilentlyContinue |
  Where-Object { $_.Length -gt 500MB } |
  Select-Object FullName, Length, LastWriteTime
```

ORAS dumps are typically on the order of **~1–2 GB**. Identify by name or by launching candidates in Azahar.

---

## Preferred dump format

| Format | Notes |
|--------|--------|
| **`.cci`** (decrypted) | **Best** for Azahar + our pipeline |
| **`.3ds`** | Often same idea; Azahar may want rename to `.cci`—use what already boots for you |
| **`.cia`** | Installable; slightly more awkward for extract; OK if that’s what you have |

If Azahar boots it, we can work with it. If load fails with encryption errors, dump/decrypt again from your console tools (GodMode9 etc.)—we never commit keys.

---

## Optional: confirm title ID (nice for freecam later)

After the dump is in `local/dumps/`, tell the agent the path. We can later read headers and record title IDs in research notes (identifiers only).

Or in Azahar: game properties often show **Title ID** (e.g. `000400000011C400` style for OR/AS—confirm on your file).

---

## Checklist for you (do this now)

1. [ ] Create folders: `C:\hoenn-forge\local\dumps` (agent can create empty structure).
2. [ ] Copy your legal OR and/or AS dump into `local\dumps\` (copy, don’t only leave one fragile USB path).
3. [ ] Confirm the same file still boots in Azahar (or open the copy from Azahar once).
4. [ ] Reply with something like:

```text
Dump ready:
- C:\hoenn-forge\local\dumps\<filename>
- Game: Omega Ruby / Alpha Sapphire
- Region: USA (or …)
- Boots in Azahar: yes
- Update installed: 1.4 / none / unknown
```

5. [ ] **Do not** `git add` anything under `local/`.

That’s all we need to start Milestone A coding. Freecam/randomizer extract steps come later; we’ll walk you through those when we get there.

---

## What the agent will do with the dump

- **Read** from `local/` on this PC for validation, extract experiments, freecam research.  
- **Never** commit it.  
- **Never** need you to upload it to GitHub.  
- Build/test instructions will always assume **your** dump path stays private under `local/`.
