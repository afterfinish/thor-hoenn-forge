# Nocturne UI fidelity pass — handoff

Branch: **`design/nocturne-fidelity`**. This brings the UI to the real delivered
Claude Design (`Hoenn Forge UI.dc.html`, project `ca62c4e3-…`), fixing the gaps the
first pass missed. Visual preview (rendered from these exact tokens):
**https://claude.ai/code/artifact/42733fcc-0ab8-4eff-9945-441c529abfc9**

> The design **pivoted off an early emerald/copper palette**
> (a "starting point — refine" that the designer overrode) to the **Nocturne** blurple
> system. That early palette is stale; ignore it. Local `design/exports/*.png` have
> **empty brand-glyph slots** and `design/*handoff.html` is a compiled bundle — neither
> is a readable source, which is why the glyph was missed. Source of truth is the
> `.dc.html` in the Claude Design MCP.

## What was wrong before → now

| Area | Before | Now |
|---|---|---|
| Palette | emerald/copper (`app/`) | Nocturne blurple `#9184d9` on `#161826` |
| Primary buttons | solid accent fill | **outlined** (2px accent, accent label) + controller double focus-ring + 0.98 press |
| Radii | 16/14/10 | chip 6 · input 8 · btn 10 · card 12 · dialog 14 |
| Font | system Roboto | **Inter** (600 titles, 500 brand, 400 body) + **IBM Plex Mono** seeds |
| Brand glyph | absent (text-only headers) | spark+wave `i-mark` in every header, splash, chips |
| App icon | soft raster / emerald triangle | crisp **vector** ball-wave `i-ball` + monochrome themed layer |
| Motion | none | transitions, press-scale, splash loader, menu/toast anims |
| In-game menus | unstyled | branded START quick menu + save/load slots |

## Token & component resources (`overlay/azahar/res/`)

- `values/hoenn_colors.xml` — full Nocturne ramps + semantic + tints. `values/hoenn_dimens.xml` — radii/spacing/touch/focus. `values/hoenn_styles.xml` — Inter text + outlined button styles. `font/inter*.ttf`, `font/hoenn_mono*.ttf` + family XMLs.
- Buttons: `drawable/hoenn_btn_{primary,secondary,ghost,danger,icon}.xml` (+ `color/hoenn_btn_*_text.xml`), press scale `animator/hoenn_press.xml`.
- Surfaces: `hoenn_bg_card.xml` (focus ring state), `hoenn_bg_card_selected.xml`, `hoenn_row_menu.xml`, `hoenn_input.xml`, `hoenn_bg_dialog.xml`, `hoenn_inset_panel.xml`, `hoenn_rule_fade.xml` (fading rule), `hoenn_progress.xml`.
- Chips `hoenn_chip{,_muted,_on}.xml`, checkbox `hoenn_checkbox*.xml`, badge `hoenn_badge.xml`, controller glyphs `hoenn_glyph_{circle,pill}.xml`.
- Icons: `hoenn_ic_{play,save,load,chevron,reroll,edit}.xml`; brand mark `hoenn_mark.xml`.
- Includes: `layout/hoenn_brand_header.xml` (mark + wordmark — now in every screen header), `layout/hoenn_controller_hint_bar.xml` (A/B/START).

## New screens (drop-in layouts — wire in the Azahar build)

- `layout/activity_hoenn_splash.xml` — 01 Splash. Wire a splash/entry activity: fade the mark in, hold ≤1.5 s, then fade to Legal (first run) or Home. Set it LAUNCHER in the manifest.
- `layout/activity_hoenn_quick_menu.xml` — 33 START quick menu. Inflate over the **paused** emulation surface when START is pressed; enter with `@anim/hoenn_menu_in` (200 ms fade + 10% slide-up); duck audio. Rows: `rowResume`/`rowSave`/`rowLoad`.
- `layout/activity_hoenn_save_slots.xml` — 34/35 slots (Load = same shell + Auto slot). `slot1`…; empty slots use `hoenn_slot_empty`.

## Motion — provided, still to wire

- **Screen transitions**: `anim/hoenn_{enter,exit,pop_enter,pop_exit}.xml` + style `Hoenn.WindowAnimation` (in `values/hoenn_theme.xml`). Apply once via the app theme: `<item name="android:windowAnimationStyle">@style/Hoenn.WindowAnimation</item>`, or per-activity `overrideActivityTransition(...)` (API 34+) / `overridePendingTransition` (< 34). A ready `ThemeOverlay.Hoenn` bundles the font + transitions.
- **Press scale 0.98**: already on every Hoenn button (`stateListAnimator`), no wiring.
- **Splash loader / validating / preparing**: indeterminate bars use the platform indeterminate drawable (accent tint). For the exact 34%-bar shimmer, drive a `translationX` ValueAnimator (see design `@keyframes hf-shimmer`); active stage spinner = `hf-spin` 0.9 s.
- **Turbo toast** (36): `anim/hoenn_toast_in.xml`, bottom-left, auto-fade ~1.2 s; no sound, never blocks input.
- **Dump success** (09): draw the check in ~240 ms; **no confetti** anywhere (design says so twice).

## App icon (both modules)

Vector adaptive icon: `hoenn_ic_launcher_{foreground,background,monochrome}.xml` (overlay) / `ic_launcher_{foreground,background,monochrome}.xml` (`app/`), wired in `mipmap-anydpi-v26/`. `app/` manifest now points at `@mipmap/ic_launcher`. Raster fallbacks (< API 26) regenerate from `scripts/gen-launcher-icons.py` (now draws the ball-wave). Launcher = `i-ball`; brand mark = `i-mark` — deliberately different.

## Notes / TODO for engineering

- The shipping build is the Windows `scripts/build-android.ps1` overlay-onto-Azahar; **not buildable on macOS**, so this pass was verified visually (preview above) + by resource validation, not a compiled APK. Please build + dogfood on the Thor.
- Confirm the emulator theme parent so `windowAnimationStyle` / `ThemeOverlay.Hoenn` merges cleanly.
- Game-name tags in the new dialogs use placeholder text ("Alpha Sapphire"); bind to the real `gameName` at runtime.
- `app/` is the superseded prototype (re-skinned so no wrong-palette surface remains); `overlay/azahar/` is the product.
