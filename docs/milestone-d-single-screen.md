# Milestone D — Single-screen toggle (optional, last)

**Delivers:** requirement **#4**  
**Depends on:** Milestone A layouts  
**Status:** Not started — **do not prioritize before A–C**  

---

## Goal

Settings toggle for devices **without** a usable dual-screen setup (most Android handhelds/phones):

- **Dual** — default on AYN Thor (top + bottom 3DS screens)  
- **Single** — large primary view of the top screen (or combined layout); bottom screen accessible via tap/hold or swap control  

---

## UX

- Settings → Display → **Screen mode**: Dual | Single  
- Optional: **Swap screens**, **Bottom screen as overlay**  
- Persist per device  
- Auto-suggest Dual when two displays / Thor model detected; else Single  

---

## Implementation sketch

- Reuse Azahar custom layout system  
- Ship two JSON/layout presets: `thor_dual`, `generic_single`  
- Hot-apply without full reinstall  

---

## Exit criteria

- [ ] Toggle works mid-session or on next launch (document which)  
- [ ] Single mode playable for bag/DexNav (touch path exists)  
- [ ] Dual remains best on Thor  
- [ ] Documented in README  

---

## Non-goals

- Replicating full OoT3D “single screen HUD redesign” art for ORAS in v1  
- Removing touch dependency for every 3DS menu (game limitation)  
