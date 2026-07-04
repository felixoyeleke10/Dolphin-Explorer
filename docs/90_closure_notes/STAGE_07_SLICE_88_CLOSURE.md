# Stage 07 · Slice 88 — Welcome-card launcher redesign

## Goal (user direction)
The slice-87 empty-map launcher looked flat/cheap ("too ugly — make it modern
like Apple or OpenAI"). Redesign to a first-class welcome screen.

## Design
One solid **welcome card** (Xcode-style), centred over the map canvas:
- Hero: dolphin logo (48 px), "Dolphin Explorer" 22 px semibold,
  "Marine survey workstation" subtitle.
- Actions: filled-accent "Import Files…" pill + quiet-outline "New Project"
  (new `newProjectRequested` signal → MainWindow::onNewProject).
- "RECENT" list: rows with a rounded icon chip, project name, and
  last-modified date (locale short format); whole row is one click target
  (children mouse-transparent); hover tint.
- Canvas behind gets a faint accent radial glow (paintEmptyState) instead of
  the old text watermark.
- Everything sits ON the card (a theme surface) — deliberate: the canvas
  colour is user-configurable and often dark even in the light theme, so
  text floating directly on it can never guarantee contrast. Verified in
  BOTH themes (dark card / white card).

## Root-cause found on the way — the @font token-order bug (REVERTED)
The card/row styles would not apply. QSS dump revealed why: in AppStyle's
token table, "@font" (family) is replaced BEFORE "@fontBase/@fontXxs/…", so
every size token becomes "<family-list>Base" garbage. Qt drops those
declarations app-wide — every `font-size: @fontXxx` in the codebase has been
silently inert — and in this block the quoted/comma garbage derailed parsing
of ALL subsequent rules (which is why the launcher card never painted).

Fixing the order retroactively applied hundreds of latent font sizes and
visibly reskinned the whole app; the user immediately asked to revert. The
original order is restored and documented in AppStyle.cpp as a KNOWN TRAP:
the app's typography is tuned around the bug. Rule going forward: new QSS
uses literal px sizes (this launcher block does). A deliberate app-wide
typography pass can fix it properly later if ever desired.

Also fixed: `@overlayActive` used in the first cut is not a token (silently
invalid) — replaced with `@overlayHov`.

## Round 2 — status-bar palette picker removed (user direction)
With the palette now in Views ▸ MAP, the status-bar "Palette" spin-picker is
redundant. Removed: PaletteSpinBox class, label+field, `setMapPalette`,
`paletteRequested`, the Chrome wiring, and the sync call in
`onPaletteChanged` (all other consumers — inspector, waterfall, SBP, Views —
still sync through DisplayStateManager). The status bar now reads
Coordinate | Scale | Rotation | CRS | AI.

## Verification
Build green; 16/16 tests. In-app grabs of the launcher verified in dark and
light themes: card, hero, buttons, recent rows with dates all render as
designed; canvas glow visible; floating 3D button unaffected.
