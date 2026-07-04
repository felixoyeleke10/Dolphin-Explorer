# Stage 07 · Slice 80 — Light theme (runtime-switchable)

## Goal
The app shipped dark-only; the Appearance page showed a disabled
"Light (coming soon)" item. Add a real light theme, switchable at runtime
without restart, from a single styling authority.

## Architecture

### Runtime theme mode
`Theme::Mode { Dark, Light }` + `Theme::mode()/setMode()` (new `Theme.cpp` in
dolphin-ui-style). The `Theme::k*` constants remain the DARK palette — the
design-system source of truth used by both the QSS token table and (for now)
custom-painted widgets.

### AppStyle::apply(mode) — one call, whole app
New entry point owning everything the theme touches:
1. `Theme::setMode(mode)`
2. **QPalette** — the macOS-dark palette moved out of `main.cpp` into
   AppStyle; a matching macOS-light palette added.
3. **Stylesheet** — `sheet()` re-tokenises for the current mode.
4. **Native window frames** — every open top-level gets its DWM title bar
   re-themed (`applyDarkTitleBar` now follows `Theme::mode()`).

### Mode-aware token table
Every colour token in `applyTokens()` resolves through `C(dark, light)`:
light counterparts for all surfaces/borders/text/accent/icon/semantic tokens
(e.g. bg `#111113`→`#ececee`, textPrimary `#f2f2f7`→`#1c1c1e`, accent
`#0a84ff`→`#007aff`, warning darkened for light-surface contrast). Overlay
tokens flip from white-alpha to black-alpha. Additionally a wholesale
`rgba(255,255,255,…) → rgba(0,0,0,…)` post-pass runs on the assembled QSS in
light mode, so the many hardcoded white-alpha hover/fill literals from the
dark-first design adapt without touching every segment. Black literals
(shadows) stay black in both modes.

### Persistence + switching
- Startup: `main.cpp` reads the existing `app/theme` QSettings key
  (0=Dark, 1=Light) and calls `AppStyle::apply` before the MainWindow exists.
- Live switch: `MainWindow::applyLiveSettings` applies the mode immediately on
  settings Apply — no restart. The Appearance page's Light item is enabled and
  the restart hint updated (theme = immediate; density/font = next launch).

## Deliberate design decision
Sonar imagery, the map canvas, and the GL views stay **dark in both modes** —
standard for hydrographic software (SeaView/SonarWiz ship light chrome over
dark imagery). Custom-painted panels that read `Theme::k*` directly are the
known styling-centralization debt (721 call sites, tracked plan) and migrate
incrementally; the QSS-driven chrome — toolbars, panels, menus, dialogs,
tables, buttons, the whole shell — re-skins completely.

## Verification
Full rebuild green (195 targets — Theme.h touches everything);
`ctest -E PerfBaseline` → 16/16 passed.

## Round 2 — user QC of light mode (slow / black dropdown / gray icons+text)
1. **Slow switch** — `applyLiveSettings` re-set the app stylesheet (full
   re-polish of every widget) on EVERY settings apply, and startup polished
   twice (main.cpp + MainWindow ctor). Now: change-guard (`want !=
   Theme::mode()`), single startup application.
2. **Command palette still black** — it had its own hardcoded VS Code-dark
   constants (card bg, borders, text, selection). All 12 now resolve through
   `Theme::mode()`; selected-row text no longer hardcodes white (invisible on
   the light selection tint).
3. **Icons unreadable (gray-on-white)** — the SVG set is authored with the
   dark stroke #aeaeb2. New `Theme::icon()` re-tints to #48484d at load in
   light mode (per-mode cache); ALL icon call sites (~45, incl. the
   ViewerToolbar / activity-bar / toolbar chokepoints and variable-path sites)
   now route through it. Widgets created before a live switch keep their old
   tint until restart — windows opened after the switch are correct.
4. **Painted text/lines unreadable** — chrome painters (LINES tree,
   CollapsibleSection lock glyph, metadata plots, contact manager headers,
   import progress, layer picker) read the dark `Theme::k*` constants
   directly. Added mode-aware runtime getters (`Theme::textMutedColor()` …
   `bgCardColor()`) mirroring AppStyle's token table and converted those
   painters. Imagery canvases (map, waterfall rendering/painters, SBP paint,
   node graph) intentionally keep the dark constants.

## Round 3 — verified live (screenshots via UI automation)
- **Every combo box in the app had NO dropdown arrow**: the QSS styles the
  `::drop-down` subcontrol but never assigned a `::down-arrow` image, so Qt
  rendered nothing — invisible affordance in both themes, but glaring on light
  ("dropdown has no color"). Added explicit arrow glyphs to the global
  QComboBox rule + wfCombo + wfFreqSelector + avPaletteCombo +
  panelChatModelCombo.
- **QSS-referenced glyph assets are dark-authored**: spin_up/spin_down SVGs are
  filled near-white (#e5e5ea) — invisible on light fields. Added
  `spin_*_light.svg` variants (#48484d) and an automatic asset swap in the
  light-mode QSS post-pass, so every `url(:/icons/spin_*)` reference re-themes
  without touching individual rules.
- Confirmed by launching the app in light mode and screenshotting: command
  palette renders as a white floating card with readable rows; the Settings
  dialog shows white combos, dark text, and visible arrows.

## Round 4 — light viewport backgrounds
The Map → Background presets were all dark (Dark/Deep Blue/Slate/Charcoal/
Night) — in light theme the viewport had no light option at all. Added
**White / Light Grey / Chart Paper** presets (hex-matched persistence, so
appending is backward-safe) and updated the hint (dark = sonar mosaic
contrast, light = chart-style review). Verified overlay readability on light
canvases: badges, the measure box, and graticule labels all carry their own
dark chips/backgrounds.

## Round 5 — light-mode text contrast (user: "fonts blend in too much")
- The light palette's mid-tier text sat near 3:1 on white. Darkened the whole
  hierarchy while keeping ranking: subtle #6e6e73→#54545a, muted
  #86868b→#66666c, soft #55555a→#46464c, dim #9aa6b2→#84909c, iconStroke
  #48484d→#3c3c42 — applied in ALL definition sites in lockstep (QSS token
  table, Theme runtime getters, Theme::icon tint, spin-arrow glyph assets,
  command palette). Verified live: tree metadata rows now clearly legible.
- **WfValueRow / WfToggleRow painted with hardcoded dark-theme constants**
  (value text #e5e5ea = invisible on white; white-alpha pills/arrows/track).
  Converted to mode-aware lookups: text via Theme::textSubtle/SecondColor(),
  overlays via a mono(white-alpha dark, black-alpha light) helper — the same
  inversion rule the QSS pass applies. This fixes the right-panel value pills
  ("0.80", "12 dB", "50 pings") the user reported as unreadable.
- Remaining known: tree status-green line names (#28a745) are ~3:1 on white —
  offered light variants for ready/failed/pending status colours as follow-up.

## Follow-ups
- Migrate direct `Theme::k*` paint sites to mode-aware lookups per the
  styling-centralization plan (priority: right-panel section headers, tree
  delegates, status bar custom paint).
- Icon strokes are `#aeaeb2` SVGs tinted for dark; on light surfaces they read
  slightly faint — consider a light-mode icon tint pass (QIcon colorize or a
  parallel icon set).
