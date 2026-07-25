# Hoenn Forge — Living Pokédex (OCR) Design Brief

**Feature:** START quick menu → **Pokédex** — OCR the **top** 3DS screen, match against an **offline** species DB, show a custom entry on the **bottom** screen (multi-hit → user chooses).  
**Context:** Azahar-class Android core, AYN Thor dual AMOLED first, GPL-3 stack, offline-first, no ROMs/dumps/keys.  
**Status:** Research / design only (not implementation). Parallel companion feature; does not block freecam RE.

**Repo hooks already present:**
- START quick menu: `overlay/azahar/java/.../EmulationFragment.kt` → `openHoennQuickMenu()`
- Thor dual layout: `profiles/thor.json` — primary **single_screen** (top), secondary **bottom_screen_only**
- Native screenshot pipeline: `RendererBase::RequestScreenshot` (Android JNI export still needed)
- Layout helpers: `Layout::SingleFrameLayout` for top-only framebuffer capture

---

## A. OCR on emulator top screen

### A.1 Capture path (recommended)

Native 3DS top screen is **400×240**. With Thor `resolution_factor: 4`, rendered top is ~**1600×960** — good for OCR.

| Approach | Verdict |
|----------|---------|
| **Native `RequestScreenshot` + top-only layout** | **Primary** — exact guest pixels, layout-driven crop |
| **PixelCopy on primary SurfaceView** | Fallback / prototype only |
| **Read GL/Vulkan from Java** | Avoid (threading / API hell) |

**Pipeline:**

```text
START → Pokédex
  → pause emulation (preferred)
  → NativeLibrary.captureTopScreenArgb(...)
       C++: SingleFrameLayout(top only) + RequestScreenshot
  → Bitmap → ML Kit TextRecognizer
  → tokens → name matcher → UI on bottom / dialog
```

**Code touch points:**
- Core: `video_core/renderer_base.{h,cpp}`, Vulkan `RenderScreenshot*`
- Layout: `framebuffer_layout` → `SingleFrameLayout(..., swapped=false)`
- JNI: `native.cpp` + `NativeLibrary.kt`
- UI: `EmulationFragment.openHoennQuickMenu()` + `hoennforge/pokedex/*`

Capture **logical top framebuffer**, not “whatever is on display 0.”

### A.2 OCR engines

| Library | Size | Offline | Recommendation |
|---------|-----:|---------|----------------|
| **ML Kit Text Recognition v2 (bundled)** | ~4 MB / script | Yes | **v1 winner** (Latin) |
| ML Kit via Play Services | ~260 KB + download | After GMS fetch | Bad for offline-first |
| Tesseract 5 | ~10–20+ MB | Yes | FOSS fallback later |

**v1:** `com.google.mlkit:text-recognition:16.0.1` (bundled Latin). JP module later if needed.

**GPL note:** Document ML Kit in NOTICE; keep as separable Gradle dep. Pure-FOSS path later = Tesseract.

### A.3 Scene handling (ORAS EN)

| Scene | Difficulty | Strategy |
|-------|------------|----------|
| Battle nameplate | Easy | Best default; large mid-upper text |
| Summary / PC / in-game dex | Easy | Full-frame OCR |
| Overworld signs | Hard | Low priority |
| Mega / forme labels | Medium | Forme table after base name |

**Preprocess:** top crop only; 2× upscale if resolution_factor ≤ 2; no aggressive binarize.

**Tokens:** ML Kit word elements; drop len&lt;3 except short species; strip pure digits / UI words (`Lv`, `HP`, `PP`).

### A.4 Performance

**On demand only** (menu open). One screenshot + OCR. Prefer pause. No continuous OCR.

### A.5 Language

v1 = English OR/AS. JP later with Japanese ML Kit + name column.

---

## B. Offline Pokédex data

| Source | License | Fit |
|--------|---------|-----|
| **veekun/pokedex** CSV → SQLite | MIT | **Primary** — evolutions, stats, moves |
| **pokemon-showdown `data/`** | MIT | Learnsets / battle stats cross-check |
| **PokeAPI** offline dump | BSD-3 | Build-time generator only |
| Official sprites / scrapes | Nintendo / unclear | **Do not ship** |

### Recommended ship product

```text
assets/pokedex/hoenn_forge_dex.db   (~1–3 MB)
  species, formes, base_stats, abilities, evolutions,
  moves, learnset, name_aliases
```

Build script: veekun + Showdown → SQLite at CI/host. No runtime pokeapi.co.

### Sprites

v1: type-colored placeholders / open icons. No Nintendo art in git or APK. Optional later: extract from **user dump** on-device only.

---

## C. Name matching

1. Normalize (NFKC, lower, ♀/♂, Mr. Mime, Farfetch'd, Mega/Primal strip).  
2. Exact alias → prefix (len≥5 unique) → Levenshtein (dist by length, score ≥ 0.75).  
3. Prefer large / centered OCR boxes (battle nameplate prior).  
4. Short names (Abra, Mew, Onix): **exact only**.  
5. Species dictionary only (not moves/items) to cut false positives.

**Multi-hit:** 0 → toast; 1 → open; 2+ → chooser list before entry.

---

## D. Bottom-screen UX (Thor)

- Top: frozen game frame (OCR source).  
- Bottom / secondary: Pokédex overlay; emulation paused.  
- Single-display fallback: modal / bottom sheet.

**v1 entry sections:** name, #, typing, base stats, abilities.  
**Next:** evolutions + methods, level-up moves, TM/egg tabs.  
**Nav:** B close; Prev/Next only among multi-hit list.

---

## E. Architecture

```text
hoennforge/pokedex/
  PokedexMenu.kt, TopScreenCapture.kt, OcrService.kt,
  NameMatcher.kt, PokedexRepository.kt, UI + models
assets/pokedex/hoenn_forge_dex.db
scripts/build-pokedex-db.py
jni: captureTopScreen*
```

**APK delta:** ~5–8 MB (ML Kit + DB + placeholders).

| Milestone | Deliverable |
|-----------|-------------|
| **M1** | Capture + OCR + multi-hit chooser (toast/dialog) |
| **M2** | Bottom entry: types, stats, abilities |
| **M3** | Evolutions + level-up moves |
| **M4** | Sprite strategy + polish + optional JP |

### Legal checklist

- No ROMs/dumps/keys in repo/APK  
- MIT/BSD data only + NOTICE  
- No official sprites  
- ML Kit attribution  
- Fan trademark disclaimer in About  

---

## F. Recommended stack (winners)

| Layer | Choice |
|-------|--------|
| OCR | ML Kit bundled Latin |
| Capture | `RequestScreenshot` + top-only `SingleFrameLayout` via JNI |
| Data | SQLite from veekun + Showdown (build-time) |
| Matching | Alias map + Levenshtein in Kotlin |
| Sprites | Placeholders only (v1) |
| UI | START menu row → secondary/bottom sheet |

### M1 implementation sketch

1. JNI `captureTopScreenRgba`  
2. Menu item in `openHoennQuickMenu()`  
3. ML Kit `OcrService`  
4. Minimal `species` + `name_aliases` DB  
5. Dialog chooser dogfood on Thor (wild battle + summary)

**M1 pass bar:** ≥90% correct on 20 random Hoenn wild nameplates; multi-hit chooser works; airplane mode OK.

---

*Research pass 2026-07-25. Implementation not started.*
