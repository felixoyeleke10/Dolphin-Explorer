# Stage 07 Slice 01 Closure — Two-Section Properties Panel with Sensor/Modality Tab Bar

## Goal
Split the Properties panel into two persistent halves: upper (Properties/Chats/History with generic/universal tools) and lower (SSS|SBP|Map|MAG sensor tab bar with modality-specific tools). The lower half is always visible regardless of which upper tab is active.

## Architecture

```
Right Properties Panel
┌─────────────────────┐
│ Properties│Chats│History  ← upper tab bar (unchanged)
├─────────────────────┤
│  Info / Navigation  │  ← upper scroll: UniversalOnly modules
│  Geometry / etc.    │     (always relevant, no modality filter)
│                     │
╞═════════════════════╡  ← QSplitter handle (user-draggable)
│ SSS │ SBP │Map│ MAG │  ← sensor tab bar (sensorTabBar)
├─────────────────────┤
│  Display / Palette  │  ← lower scroll: ModalOnly modules
│  Radiometry / etc.  │     (filtered by active sensor tab)
└─────────────────────┘
```

## Key Changes

**`RightPanelHost`** gained a `ShowMode` enum (`UniversalOnly` / `ModalOnly`).
- `UniversalOnly`: creates Info, Navigation, Geometry only. Always visible, no filter logic.
- `ModalOnly`: creates Display(SSS), SbpDisplay/Gain/Signal, Radiometry, Enhancement only.
  Filtered by `setModalityFilter(Modality)` and `setAvailableModalities()`.
- `computeFilterVisible()` uses the mode to decide section visibility.
- `m_current_layer` stored so `setModalityFilter()` can re-apply visibility without re-calling setLayer.

**`InspectorPanel`** now uses `ShowMode::UniversalOnly` for its inner `m_layer` host.

**`MainWindow`** gained `m_modal_host` (`RightPanelHost*`, `ModalOnly`) — the lower panel owner.

**`buildPropertiesPanel()`** now builds a `QSplitter(Qt::Vertical)`:
- Upper widget: existing Properties/Chats/History tabs + `m_props_stack`
- Lower widget: sensor tab bar + modal scroll area wrapping `m_modal_host`

**Signal re-wiring** — all modal signals now come from `m_modal_host` not `m_inspector->rightPanelHost()`:
- `paletteChanged` / `channelChanged`: connected in Shell.cpp
- `sbpParamsChanged` / `sbpGainModule()` / `sbpSignalModule()`: SubBottomCoordinator
- `gainPanel()` / `imagingPanel()`: sourced from `m_modal_host` in buildPropertiesPanel
- `setPalette` / `setSbpParams`: all coordinator files updated

## Files Changed
- `src/ui/mainwindow/rightpanel/RightPanelHost.h/.cpp` — ShowMode enum, split constructor, computeFilterVisible
- `src/ui/mainwindow/panels/InspectorPanel.cpp` — ShowMode::UniversalOnly
- `src/ui/mainwindow/MainWindow.h` — fwd-decl RightPanelHost, m_modal_host, refreshSensorTab()
- `src/ui/mainwindow/MainWindow.MainArea.cpp` — QSplitter layout, modal scroll area with m_modal_host
- `src/ui/mainwindow/MainWindow.Shell.cpp` — paletteChanged/channelChanged from m_modal_host
- `src/ui/mainwindow/MainWindow.ProjectBinding.cpp` — clearLayer on project change
- `src/ui/mainwindow/coordinators/MainWindow.LayerCoordinator.cpp` — setLayer on modal host, refreshSensorTab uses m_modal_host
- `src/ui/mainwindow/coordinators/MainWindow.SubBottomCoordinator.cpp` — all rightPanelHost() → m_modal_host
- `src/ui/mainwindow/coordinators/MainWindow.WaterfallCoordinator.cpp` — setSbpParams → m_modal_host
- `src/ui/shell/AppStylePanels.cpp` — sensorTabBar, propsSplitter handle styles

## Build Result
All targets clean, 0 errors.
