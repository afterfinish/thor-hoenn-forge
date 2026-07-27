# 60 FPS on the v1.0 cart build — research

Question: can we render ORAS at 60 FPS **at normal game speed** on the v1.0 build we already
support, without asking anyone to update to v1.4?

**Verdict: yes for v1.0, and the work is one word — but the feature may be cosmetic, and that is
the only thing left to find out.**

Four claims, separated because they have very different confidence levels:

| Claim | Status |
|---|---|
| The per-frame function exists in v1.0 and I have found it | **Proven.** `0x0010E354`, disassembled and annotated below |
| The Thor has the headroom | **Proven, already measured.** 300% / 90 FPS at 4× resolution — see §6 |
| A one-word patch makes ORAS present 60 frames/s at correct game speed | **Proven on paper**, untested. Exact byte given |
| Those 60 frames contain 60 *distinct* images | **Unproven, and the evidence leans no.** This is the whole feature |

The last row is the feature. Everything else is finished or already built: the delivery mechanism
exists and is verified, and `feature/60fps-toggle` already ships the UI, the IPS writer and the
randomizer interlock — it just encodes the wrong build's offsets. Do not start on delivery until
the last row is settled, and settling it costs one afternoon in desktop Azahar, not the week that
was budgeted, and not the device.

---

## 1. What was extracted, and how

The 2 GB cart dump is a plain decrypted NCSD. Nothing had to be decrypted or dumped from the
device.

| Item | Value |
|---|---|
| NCSD partition 0 | file `0x4000`, NCCH, flags byte 7 = `0x04` → **NoCrypto** |
| Title ID / product code | `000400000011C500` / `CTR-P-ECLA`, internal name **`sango-2`** |
| ExeFS | file `0x6E00`; `.code` at file `0x7000`, `0x2E5F70` bytes, **BLZ-compressed** |
| `.code` decompressed | `0x530000` bytes (5,439,488) |
| `.text` | VA `0x00100000`, file `0x000000`, `0x4795FC` used of `0x47A000` |
| `.rodata` | VA `0x0057A000`, file `0x47A000`, `0x71A14` used |
| `.data` | VA `0x005EC000`, file `0x4EC000`, `0x439F4`, then `.bss` `0x7EC2C` |

So **VA = file offset + `0x00100000`** inside `.text`. Zetta_D's `0x0010E34C` is `.text` + `0xE34C`.

The framework is GameFreak's **gflib** — `.rodata` still carries build paths like
`c:\home\gflib_cpp_final\gflib\prog\include\base/gfl_Singleton.h`, plus `nw::` (NintendoWare) and
`xy_system` RTTI. Same library as X/Y.

Reproduce with `scratchpad/extract_code.py` (BLZ = CUE's `blz.c` algorithm: reverse the compressed
region, LZ77-decode forward, reverse the output). Decompression is verified — the stream is
consumed exactly (3,039,007 / 3,039,007 bytes) and the result begins with a valid ARM `BL` table.

Disassembly used `arm-none-eabi-objdump` from the devkitPro install already on this machine, plus
`capstone` for the xref passes.

### Finding the function

Search key from MEMORY.md, and it worked first try. Across the entire 5.4 MB binary, word-aligned:

```
16666  (0x0000411A)   exactly 1 occurrence   -> 0x0010E69C
33333  (0x00008235)   exactly 1 occurrence   -> 0x0010E6A0
```

Two adjacent literal-pool words. Three `LDR Rd,[pc,…]` instructions reference them, all inside one
function whose prologue is at **`0x0010E354`**.

---

## 2. The v1.0 per-frame function — `0x0010E354`

`r4` = the frame manager (gflib singleton, pointer held at `0x0062F7C4`). Annotated:

```
0010E354  push {r4-r9,sl,fp,lr}            ; ---- function entry
0010E36C  vldr s16,[pc,#796]  -> 1.0f      ; stereo-3D depth factor, NOT a timestep
0010E374  ldrb r0,[r4,#13]                 ; ** mode: 0 = 60Hz, 1 = 30Hz **
0010E378  cmp  r0,#0
0010E37C  beq  0x0010E394                  ; ** mode==0 -> skip alternation entirely **
0010E380  ldrb r0,[r4,#14]                 ; alternation toggle
0010E384  eors r0,r0,#1
0010E388  moveq r6,#0                      ; toggle fell to 0 -> this is the "off" frame
0010E38C  strb r0,[r4,#14]
0010E390  beq  0x0010E3B8                  ; -> light path
0010E394  mov  r6,#1                       ; shouldUpdate = 1

0010E3D0  ---- update() ----
0010E3D0    ldr r0,[r4,#24]; ldr r0,[r0]; cmp r0,#0; bne +      ; queue non-empty -> skip submit
0010E40C    blx vtable[+0x20](proc, procMgr->[0x14])            ; submit previous frame
0010E410    bl 0x117ECC(mgr->[0x18])
0010E420    bl 0x122C6C(mgr->[0x2C])
0010E42C    bl 0x12DDF4(mgr->[0x30])
0010E48C    blx vtable[+0x18](proc)                             ; ** the game's UpdateFunc **
0010E4B0    vstr s16,[procMgr->[0x14] + 0x10]                   ; 3D depth = 1.0f
0010E4C8    b   0x0010E530

0010E4CC  ---- light path (r6 == 0) ----
0010E514    blx vtable[+0x1C](proc, procMgr)                    ; pre-draw / bookkeeping
0010E528    bl 0x11ECD4(procMgr, 3)

0010E530  ---- tail ----
0010E530    ldrb r0,[r4,#13]               ; mode
0010E534    rsb  r1,r6,#0                  ; -shouldUpdate
0010E538    tst  r0,r1                     ; ** draw() iff !(mode && shouldUpdate) **
0010E53C    bne  0x0010E554                ;    i.e. iff (!mode || !shouldUpdate)
0010E540    ldrb r1,[mgr->[0x1C] + 0x1FE]  ; one-shot frame-skip latch
0010E54C    strbne …                       ;   set -> consume it and return without drawing
0010E550    beq  0x0010E564

0010E554  ---- return ----

0010E564  ---- draw() ----
0010E568    svc 0x28                       ; svcGetSystemTick  (start)
0010E590    bl 0x1117A4(gfx, 7)
0010E59C    bl 0x11ECE4(procMgr)           ; -> proc vtable[+0x10] = the game's DrawFunc
0010E5AC    bl 0x11CB60 / 0x122BE0 / 0x12286C / 0x122BE0 / 0x122810
0010E5F4    bl 0x142350(gfx,1)             ; Is3DActive()? reads sharedpage 0x1FF81080/84
0010E5FC    vldreq s16,[pc,#148] -> 0.0f   ;   not active -> depth 0
0010E608    vmul.f32 s0, s16, [r4,#16]     ; depth * global depth scale
0010E60C    bl 0x11CE44(gfx, s0)           ; present with stereo depth
0010E618    bl 0x11153C(gfx)
0010E620    svc 0x28                       ; svcGetSystemTick  (end)
0010E62C    ldrb r1,[r4,#13]
0010E640    ldreq r7,[pc,#84]  -> 16666    ; ** mode==0 -> 60 Hz budget **
0010E644    ldrne r7,[pc,#84]  -> 33333    ; ** mode!=0 -> 30 Hz budget **
0010E654    bl 0x11D62C                    ; GPU processing time
0010E658    cmp r0,r7 ; movhi/strhi        ; overran -> arm the skip latch
0010E688    strb #1,[mgr->[0x1C] + 0x1FE]
```

Object layout, from the constructor at `0x00106ECC` and the init at `0x00106B10`:

| Offset | Meaning |
|---|---|
| `+0x0C` u8 | requested mode; `2` = no request pending |
| `+0x0D` u8 | **current mode — 0 = 60 Hz, 1 = 30 Hz** |
| `+0x0E` u8 | alternation toggle |
| `+0x10` f32 | global stereoscopic-3D depth scale (`1.0f`) |
| `+0x14` | gflib proc manager |
| `+0x18` | render submit queue |
| `+0x1C` | timing/config block: `+0x1FC` budget-check enable, `+0x1FD` profiling, `+0x1FE` skip latch |

Mode changes are applied at the top of the caller, `0x00108A48`: if `+0x0C != 2` it calls
`0x00110F14(display, mode)` — which writes the mode byte into every render target's `+0x14` across
five target arrays — then resets the toggle and stores the new mode into `+0x0D`. That call is why
flipping the mode really does change presentation cadence and not just the CPU budget.

### Independent confirmation that this is the right object

The published v1.0 AR code (Reshiban, Dec 2019) writes the **word** at `0x08C650C0`:

```
60 FPS   08C650C0 00000002
30 FPS   08C650C0 00000102     <- the game's runtime default
```

Byte-wise that is `+0x0C = 2`, `+0x0D = 0 or 1`, `+0x0E = 0`, `+0x0F = 0` — an exact match for the
field layout I derived statically from the constructor, with no knowledge of the cheat. So the v1.0
frame manager lives at heap **`0x08C650B4`**, and the byte the whole community has been poking for
six years is this function's `+0x0D`.

Two independent derivations landing on the same four bytes is as good as this gets without a device.

---

## 3. Mapping to the published v1.4 work

Zetta_D's thread is real and his decompilation is accurate. His pseudocode
(`if(mode){count^=true; shouldUpdate=count;}` … `if(!mode || !shouldUpdate){ … draw() … }`
… `limit = mode ? 33333 : 16666`) matches the v1.0 disassembly instruction for instruction,
including the unusual `tst(mode, -shouldUpdate)` encoding of `!mode || !shouldUpdate`.

**Every anchor in his patch maps to v1.0 with a constant `+8`, and each one lands on the
semantically correct instruction:**

| v1.4 | v1.0 | v1.0 word | What it is |
|---|---|---|---|
| `0x0010E34C` | `0x0010E354` | `E92D4FF0` | `push {r4-r9,sl,fp,lr}` — function entry |
| `0x0010E36C` | `0x0010E374` | `E5D4000D` | `ldrb r0,[r4,#13]` — the mode read he overwrites |
| `0x0010E38C` | `0x0010E394` | `E3A06001` | `mov r6,#1` — becomes his counter word |
| `0x0010E3C8` | `0x0010E3D0` | `E5940018` | first instruction of `update()` |
| `0x0010E528` | `0x0010E530` | `E5D4000D` | tail entry, his loop-back site |
| `0x0010E55C` | `0x0010E564` | `E320F000` | `nop` immediately before `svc 0x28` — `draw()` entry |

Six for six. Because source and target both shift by 8, **every relative branch encoding in his
listing ports unchanged**. The v1.0 port of his patch is a literal `+8` on the left column:

```
0010E374  E3A06001   mov  r6,#1
0010E378  E3A0C00X   mov  r12,#X
0010E37C  E58FC010   str  r12,[pc,#0x10]     -> 0x0010E394
0010E380  E59FC00C   ldr  r12,[pc,#0xC]      -> 0x0010E394
0010E384  E25CC001   subs r12,r12,#1
0010E388  0A000075   beq  0x0010E564          (draw)
0010E38C  E58FC000   str  r12,[pc,#0]        -> 0x0010E394
0010E390  EA00000E   b    0x0010E3D0          (update)
0010E394  FFFFFFFF   counter
0010E530  EAFFFF92   b    0x0010E380
```

I re-derived all six branch targets from the encodings; they resolve correctly at the v1.0
addresses.

**But do not ship that patch, because it is not a 60 FPS patch.** Read what it does: per entry it
runs `update()` exactly `X-1` times and then `draw()` once. Its floor is one update per draw. To get
60 FPS at correct speed you need *half* an update per draw, which that counter cannot express. His
thread title is honest about this — *"How to Change Game Speed Independently of FPS"*. It is a speed
slider, and his `X=2 → normal / X=3 → double` mapping only holds while the display is still in 30 Hz
mode. Set the FPS flag to 0 and `X=2` gives you 2× again.

The thing in his work that *can* do it is in the plugin, not the patch: `feature_engine.h` MITM-hooks
the update function and, for negative `game_speed`, calls the original only every Nth frame. That is
the right primitive. **The one-word patch in §4 is the same primitive, done statically, with no hook
and no address hunt.**

### This matters for `feature/60fps-toggle`

That branch (`43fa875`, unmerged, never device-tested) already hand-assembles Zetta_D's counted loop
in `HoennSixtyFps.kt:113-139` — and registers it at `HoennSixtyFps.kt:149` against title
`0x000400000011C500`, **the v1.0 base title, with v1.4 offsets**. `HoennSixtyFps.kt:41-46` says so
in a comment. As written it would write nine words of ARM into `0x0010E36C`, which in v1.0 is eight
bytes short of where that code lives — landing mid-prologue, in the middle of `vldr s16` and the
`sub sp`. That is a hard crash on boot, not a subtle misbehaviour. The offsets must be corrected
before that branch is enabled for anyone.

The branch author also flagged the deeper problem, at `HoennSixtyFps.kt:105-111`: the counted loop
keeps its counter in a data word **inside `.text`** and rewrites it every frame, and re-reads it
through a PC-relative literal load. That is self-modifying code, and the literal is exactly the kind
of thing dynarmic may fold at translation time. It was validated on physical hardware only. Their
own suggested fallback — *"force the update flag, call `update()` once, fall through to an
unconditional `draw()`"* — is close to the §4 patch, and §4 is better still: **one static byte, no
writes to `.text` at runtime, nothing for a JIT to fold.**

Also worth recording from the survey: **no v1.0 true-60 patch exists publicly**, no v1.1/1.2/1.3 work
exists at all, no version-diff table exists, and the only attempt to port this technique to another
build (Pokémon Sun) crashed on v1.2, did nothing on v1.0, and was abandoned unresolved. Nobody
distributes any of this as IPS or `code.bin`.

---

## 4. Why the AR code doubles speed, and the one-word fix

With `+0x0D = 0`:
- `0x0010E37C` branches straight past the alternation, so `shouldUpdate` is `1` **every** frame;
- the tail's `!mode` term is true, so `draw()` also runs **every** frame.

60 updates/s and 60 draws/s. That is the 2× speed everyone reports, and it is a single branch that
causes it.

The fix is to delete that branch. Replace `0x0010E37C` with a NOP and the alternation applies even in
mode 0, while the tail keeps drawing every frame because `mode` is 0:

| | update() | light path | draw() |
|---|---|---|---|
| stock, `+0x0D = 1` | 30 /s | 30 /s | 30 /s |
| flag flip, `+0x0D = 0` | **60 /s** | 0 | 60 /s |
| **patched, `+0x0D = 0`** | **30 /s** | **30 /s** | **60 /s** |

Every path in the function runs at exactly its stock rate. The only thing that changes is that
`draw()` now also runs on the update frame. That is the whole patch.

```
code.bin file offset 0x00E37C   (VA 0x0010E37C)
  from  0A 00 00 04     beq 0x0010E394
  to    00 F0 20 E3     nop {0}          (the binary uses this encoding throughout)
```

As an IPS against the decompressed `code.bin`:

```
50 41 54 43 48   00 E3 7C   00 04   00 F0 20 E3   45 4F 46
"PATCH"          offset      len     data          "EOF"
```

Two properties make this unusually safe:

- **It is inert at `+0x0D = 1`.** The branch it removes is only ever taken when mode is 0. With the
  game in its normal 30 Hz mode the patched binary is bit-for-bit equivalent in behaviour. So the
  patch can ship enabled and the feature is gated purely by the mode byte, which we can already set
  from the existing cheat path.
- **It exercises no new code.** The light path at `0x0010E4CC` is what the game already runs on every
  other frame at stock. Nothing untested gets reached.

---

## 5. The part that is not proven: are those 60 frames different?

This is the feature, and I cannot settle it statically.

`update()` is the only thing that advances game state, and after the patch it still runs 30 times a
second. The two `draw()` calls between consecutive updates therefore render the same scene **unless
the game's own `DrawFunc` (proc vtable `+0x10`, reached via `0x0011ECE4`) mutates something visible.**
If it does not, we have bought a 60 in the FPS counter and no visible change whatsoever.

The evidence available leans the wrong way:

- **The engine has no time delta.** The entire per-frame function contains exactly five floating-point
  instructions, and all five are the stereoscopic-3D depth factor — `1.0f` at `0x0010E690`, `0.0f` at
  `0x0010E698`, gated by `Is3DActive()` which reads the 3D slider from the shared page at
  `0x1FF81080`/`0x1FF81084`. Nothing resembling `dt` is passed to `update()`, which takes no arguments
  at all. This is a pure fixed-step design: **one call = one logical frame.** So the obvious
  alternative — run update at 60 Hz with a halved timestep — has nothing to halve. It is not a matter
  of finding the right constant; the constant does not exist.
- **Stock ORAS already alternates update and draw.** Frame A updates, frame B draws. GameFreak did not
  build a 30 Hz-logic/60 Hz-render mode; they built a 60 Hz mode in which *both* run every frame.
  The engine's own answer to "60 FPS" is 60 Hz logic. There is no interpolation path anywhere in the
  loop.
- The published 60 FPS work has **no independent confirmation** that it produces 60 Hz *motion* at
  normal speed. The one such claim is the author's own, on his own hardware, and it is exactly the
  claim someone would make after seeing an FPS readout change while the game felt normal.

Against that: `nw::anim::AnimFrameController` is in the RTTI, NintendoWare animation controllers use
float frame positions with an update rate, and studios do sometimes advance skeletal animation during
scene-graph traversal rather than in game logic. If ORAS's `DrawFunc` steps animations, the second
draw of each pair is genuinely new and the feature works for character and camera motion (with world
logic still at 30). That is a real possibility, not wishful thinking — but it is a coin toss, and it
is cheap to resolve.

**What would visibly break if render were unlocked naively (flag only, no patch):** everything runs at
2×. Walking, running, NPC and wild-encounter animation, all scripted cutscene timing, battle
animations and message advance, HP-bar drain, the Exp bar, day/night and weather stepping, and the DexNav
/ AreaNav timers. Music keeps real-time pacing while events fire at 2× — this is the "chopped audio"
in every community report; it is desync, not an audio bug. Anything driven by an integer frame counter
(most of GameFreak's own systems) doubles exactly. The patch in §4 removes all of this by keeping
`update()` at 30 Hz.

---

## 6. Headroom — already measured, and it passes

`MEMORY.md:158` says to run turbo at 200% first and abandon the feature if Speed % sags to ~130.
**That test has already been run, several times, and was not recognised as such.** The result is not
in any log — Azahar's Speed % is only ever formatted into an on-screen `TextView`
(`EmulationFragment.kt:2000`) and never reaches logcat — but it is burned into the dogfood
screenshots, which have the perf overlay enabled by `ThorProfile.kt:100-103`:

| Screenshot | Overlay reads | Scene |
|---|---|---|
| `screenshots/top-vote-b.png` | **FPS 90 · Speed 300%** | interior, flower shop |
| `screenshots/top-orbit-issue.png` | **FPS 90 · Speed 300%** | outdoor route, water + shoreline |
| `screenshots/top-probe-issue.png` | FPS 82 · Speed 274% | outdoor, water / berry patch / dock |
| `screenshots/top-orbit-live.png` | FPS 79 · Speed 265% | outdoor dock, three NPCs |
| `screenshots/top-vote-a.png` | FPS 76 · Speed 254% | outdoor forest, dense foliage |
| `screenshots/top-stick-issue.png` | FPS 30 · Speed 100% | turbo off, limiter holding |

Two of those sit *exactly* on 300%, which is the configured turbo ceiling
(`ThorProfile.kt:97` sets `TURBO_LIMIT = 300`; upstream default is 200) — meaning the limiter was
sleeping and the device had headroom it was not permitted to use. The worst outdoor case measured is
254%.

Every one of those captures was taken under `ThorProfile.applyCore()`: **Vulkan, `RESOLUTION_FACTOR`
= 4** (upstream default 1, `ThorProfile.kt:49`), accurate-mul on, HW shaders on, mono with the right
eye dropped. So 90 rendered frames per second at 4× internal resolution is a measured fact on this
device with our shipped settings.

The comparison that matters:

| | logic /s | draws /s |
|---|---|---|
| stock | 30 | 30 |
| **turbo 300%, already sustained** | **90** | **90** |
| 60 FPS patch | 30 | 60 |

The patch asks for **less total work than the device is already known to deliver** — two thirds the
draw rate and one third the logic rate of a sustained 300% turbo run. Headroom is not the blocker,
and there is margin left in `RESOLUTION_FACTOR` if it ever becomes tight.

Two caveats to carry forward rather than to worry about:

- The engine polices itself. The budget check at `0x0010E654` compares measured GPU time against
  `r7`, which in mode 0 is **16666 µs**. Overrun arms the skip latch at `mgr->[0x1C]+0x1FE`, and the
  next frame consumes it and returns without drawing. So if the Thor ever cannot hold 16.6 ms the
  game degrades to an irregular cadence on its own rather than tearing.
- `ThorProfile.applyIfNeeded()` calls `applyCore()` unconditionally *before* reading `thorApplied`
  (`ThorProfile.kt:33-38`), so the flag guards nothing and every Play re-stomps user graphics
  settings. If a tester lowers resolution to buy headroom it will be silently reverted next launch.
  Pre-existing defect, already logged; relevant here only because it will confuse a test session.

---

## 7. Delivery options, ranked

### (a) IPS to `code.bin` via `load/mods/<TID>/exefs/code.ips` — **recommended, and mostly built**

The delivery path is verified in our pinned Azahar (`emulator/azahar`, upstream `c711b0a`):

- `src/core/file_sys/ncch_container.cpp:518` `NCCHContainer::ApplyCodePatch`. Search order at
  `:541-549` puts `load/mods/<ModId>/exefs/code.ips` **first**; first match wins. Also supports
  `code.bps`, and a separate whole-file `code.bin` override at `:570`.
- Called from `src/core/loader/ncch.cpp:191`, after the `.code` LZSS decompression (`:493-504` of
  `ncch_container.cpp`) and after the `.bss` resize, and *before* `CreateProcess` at `:198`. So the
  patch is applied to the decompressed image and **dynarmic never sees an unpatched byte** — the
  pre-JIT claim holds.
- `<ModId>` is `GetModId(program_id)` (`ncch_container.cpp:28-34`), which folds update titles onto the
  base title and is the identity for a cart dump. Formatted `{:016X}`.
- On the Thor this resolves to
  `/storage/emulated/0/Download/3DS/Fw/load/mods/000400000011C500/exefs/code.ips`.

And the app-side plumbing already exists on **`feature/60fps-toggle`** (`43fa875`): `HoennSixtyFps.kt`
is a 271-line IPS emitter using `TEXT_BASE = 0x00100000` — the same convention as §4 — with the quick
menu row, an EXPERIMENTAL confirmation, a turbo interlock (stacking turbo on the patch gives 4×), and
a re-assert in `EmulationActivity.kt` because `RandomizerEngine.clearMods()` deletes the whole
`load/mods/<title>` tree on every prepare.

- **Effort:** the patch is written (§4) and the plumbing is written. The real work is (i) replace the
  patch table in `HoennSixtyFps.kt:113-139` with the single §4 record, (ii) rebase the branch, which
  is one commit off a pre-`feature/freelook-gpu` base and will conflict in `EmulationFragment.kt` and
  `activity_hoenn_quick_menu.xml`, (iii) test. Two days, most of it the rebase.
- **Risk to free look:** none. Free look reads `*(u32*)0x085F67DC` and writes a heap camera object,
  guarded to `0x08000000`–`0x0BFFFFFF` by `HeapPtr()` (`hoenn_freecam.cpp:97-99`), and the outdoor
  path writes no guest memory at all. Different address space, different mechanism, and there is no
  existing code-patching machinery anywhere in the overlay for this to disturb.
- **Risk to the randomizer:** none in address space — it byte-patches `DllField.cro` inside RomFS,
  this is ExeFS. The one real interaction is `clearMods()`, which the branch already handles.
- **Survives a game update:** **no.** v1.0-only. But that is the premise — the app rejects CIA
  installs, and free look (`0x085F67DC`) and the randomizer (`0xF906C` / `0xF1B20`) are already
  v1.0-locked. This adds no version fragility we do not already carry.
- **Caveat:** the mode byte still has to be set to 0. That is the existing public v1.0 AR code
  (`08C650C0 00000002`) and the app already runs a cheat engine, so it is free.

### (b) Emulator-side hook in our Azahar fork

We already overlay `core/` and `video_core/`, so hooking is familiar ground. But there is nothing to
hook that beats a 4-byte static patch: the change is one branch in guest code, and intercepting guest
control flow from the host means either a JIT-level hook or a per-tick memory rewrite, both strictly
more machinery for an identical result. The cheat engine cannot express it at all —
`Cheats::CheatEngine::RunCallback` is a CoreTiming event at ~15 Hz when tools are on
(`cheats.cpp:143-150`, `kSchedTools = 16'000'000`) and it writes data, not instructions.

The one thing (b) is good for is applying the same patch from our own code at load time instead of
shipping an IPS file. Worth it only if writing into `load/mods` proves awkward, which — given the
randomizer already writes there — it will not.

- **Effort:** 2-3× option (a) for the same outcome.
- **Risk:** touches the overlay set free look depends on. Option (a) touches none of it.

### (c) Not worth doing

Currently the **most likely correct answer**, and it stays the answer until §5 is resolved. If the
second draw of each pair is identical, this ships an FPS counter and a halved battery life. There is
no partial credit — "60 FPS that looks exactly like 30 FPS" is worse than not shipping, because it
invites bug reports about a feature that is working as designed.

---

## 8. Next physical step

The headroom test `MEMORY.md` asks for is already answered (§6), so **the first thing to run is not on
the Thor at all.** Everything below except the last step is desktop Azahar with the v1.0 dump.

**Step 1 — does the extra draw produce a new image? (desktop, one afternoon.) This is the go/no-go.**

First, turn **`use_skip_duplicate_frames` off** for the duration. `ThorProfile.kt:82` forces it on,
and a setting whose job is to drop frames that did not change is precisely the wrong thing to have
running while measuring whether frames change.

1. **Baseline.** Stand somewhere with continuous motion — tall grass, a patrolling NPC, the player's
   run cycle. Capture at 60 fps.
2. **Flag only.** Apply `08C650C0 00000002`. The game should run at 2× and *look* like 60 Hz motion.
   This proves two things at once: the renderer can produce 60 distinct frames on this content, and
   the mode byte at `0x08C650B4 + 0x0D` is the right one in our exact dump.
3. **Patched.** Apply the 4-byte IPS from §4 **and** the same flag code. The game must run at
   **normal speed**. If it is still 2×, the IPS did not apply — check for
   `File … patching code.bin` from `ncch_container.cpp:560` in logcat, and check the randomizer did
   not wipe the directory.
4. **Compare step 3 against step 1, frame by frame.** Either consecutive captured frames differ
   during motion — the feature is real, go build option (a) — or they arrive in identical pairs, in
   which case this is 30 Hz content presented twice and the answer is option (c).

Do not substitute "it looks smoother" for step 4. That judgement is exactly the one the published
claim appears to have got wrong, and it is the reason this document exists.

**Step 2, only if step 1 passes — menus (desktop).** Open the Bag and the party screen with the patch
and flag on. Zetta_D asserts ORAS runs menus at 60 Hz natively. If so, the patch halves menu logic
rate and cursor repeat and menu animation will visibly drag. Fixable by gating the alternation on a
byte we own rather than unconditionally, but that costs a second instruction and a second look, so
find out first.

**Step 3, only if steps 1-2 pass — the Thor.** Rebase `feature/60fps-toggle`, replace its patch table
with the §4 record, and dogfood Route 104 and Petalburg Woods with the perf overlay on. Watch for the
engine's own skip latch (§6) showing up as an irregular cadence rather than a steady 60. Confirm free
look and a randomized save both still work with the IPS present — expected, since neither shares an
address space with it, but it is a five-minute check and the randomizer's `clearMods()` interaction
is the one place they touch.

---

## 9. What was not proven

Recorded honestly, because two of these could still overturn the conclusion.

- **The second draw's content.** §5. Unresolved, and it is the feature.
- **Who sets the mode to 30 Hz.** Both initialisation paths I found — the constructor at `0x00106ECC`
  and the init at `0x00106B10` (where `r7` is provably 0, set at `0x00106B48`) — write `+0x0D = 0`,
  i.e. **60 Hz**, and set `+0x0C = 2` meaning "no request pending". Nothing I traced ever requests
  mode 1. Yet the community's v1.0 code documents `+0x0D = 1` as the runtime default, and ORAS is
  demonstrably not running the overworld at 2×, so something writes it. The singleton at `0x0062F7C4`
  has only three readers (constructor, the `GetInstance()` leaf at `0x0014E348`, one game-side site),
  and `GetInstance()` has 320 callers — the setter is in there and I did not isolate it. **Consequence
  if I am wrong about this and the game really does boot at mode 0:** the patch would halve game speed
  instead of being inert, and §8 step 1.3 would show it immediately. The test catches it either way.
- **Whether the game selects mode 0 for menus.** Asserted in the thread, not verified. §8 step 2.
- **The v1.0 equivalent of `ADDRESS_UPDATE_FRAME` (`0x0011EEA4` in v1.4).** Not established. The
  `+8` delta holds inside `0x0010E354` but there is no reason to expect it 60 KB further into `.text`,
  and the published v1.0↔v1.4 deltas elsewhere are inconsistent (`+0x3FE0` for the heap global,
  `+0x28C` for some `.rodata`). Not needed — the one-word patch removes the reason to hook.
- **The savestate cross-check did not complete.** `local/re-cam/as.00.raw` is a Citra boost archive,
  and I did locate the codeset inside it and byte-verified `.text` against the extracted `code.bin` at
  four widely separated offsets, which confirms the savestate is from this exact build. But the
  segments are not laid out at a constant offset (`.rodata` sits at `+0x47A1C0`, not `+0x47A000`), so I
  could not establish an FCRAM base and therefore could not read the live manager object at
  `0x08C650B4` to observe its runtime mode. `freecam-deep-re.md` §9 already warns against raw
  savestate scans; that warning holds.
- **The headroom figures are inferred from screenshots, not from a run designed to measure them.**
  300% / 90 FPS is what the overlay read during freelook dogfooding, and the scenes are the ones that
  happened to be captured. No battle, no Mauville, no weather. The margin is large enough that this
  is unlikely to matter, but "90 FPS sustained everywhere" is not what was measured — "90 FPS in six
  captured scenes, two of them pinned at the configured ceiling" is.
- **No device measurement of the patch itself.** Everything above is static analysis, published
  sources, and existing screenshots.

---

## 10. Files

| Path | Role |
|---|---|
| `local/dumps/Pokemon Alpha Sapphire - dump.3ds` | source, decrypted NCSD, v1.0 |
| `scratchpad/extract_code.py` | NCCH/ExeFS parse + BLZ decompress → `code_v10.bin` |
| `scratchpad/armlib.py`, `xref.py` | VA↔offset mapping, literal-pool and call-graph xrefs |
| `scratchpad/find_mgr.py`, `savestate_probe.py` | savestate probes (partial, see §9) |

In the repo, on `feature/60fps-toggle` only:

| Path | Role |
|---|---|
| `overlay/azahar/java/…/hoennforge/HoennSixtyFps.kt` | IPS emitter + patch table — **replace the table with §4** |
| `overlay/azahar/java/…/activities/EmulationActivity.kt` | re-asserts the IPS after `clearMods()` |
| `overlay/azahar/java/…/fragments/EmulationFragment.kt` | quick-menu row, turbo interlock |

And the upstream path it relies on, for reference: `emulator/azahar/src/core/file_sys/ncch_container.cpp:518`
(`ApplyCodePatch`) and `emulator/azahar/src/core/loader/ncch.cpp:191` (call site, post-decompress,
pre-`CreateProcess`).

Everything in `scratchpad/` is scratch — nothing there needs to enter the repo except this document
and, if the feature survives step 1, a 17-byte `code.ips`.

## 11. Sources

- Zetta_D, *How to Change Game Speed Independently of FPS (Example with Pokémon ORAS)* —
  <https://gbatemp.net/threads/how-to-change-game-speed-independently-of-fps-example-with-pokemon-oras.680385/>
- `David-Darras/sango-plugin` — <https://github.com/David-Darras/sango-plugin> (AS v1.4 only;
  `include/feature/feature_engine.h`, `include/address.h`)
- Reshiban, 60 FPS AR codes incl. the v1.0 set —
  <https://github.com/Reshiban/60FPS-AR-CHEATS-3DS> and
  <https://gbatemp.net/threads/60-fps-patches-cheat-codes-releases-and-discussion.550527/>
