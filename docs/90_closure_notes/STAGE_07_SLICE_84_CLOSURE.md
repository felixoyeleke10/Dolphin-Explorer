# Stage 07 · Slice 84 — Lower "Map" sensor tab removed

## Goal (user direction)
The right panel had two "Map" tabs: the upper Properties | Map | History strip
(slice 81's map display settings) and a "Map" tab in the lower sensor bar
(SSS | SBP | Map | MAG). The lower one was redundant — it showed only the
universal sections (Contact Picking), which already appear under every sensor
tab. Remove it.

## What changed

### Sensor bar — SSS | SBP | MAG
- `buildPropertiesPanel`: the Map tab is gone; ids/modality table reduced to
  {Sidescan, SubBottom, Magnetometer}. Nothing is checked at construction.
- **No-selection is now a legal state**: with no sensor layer active (empty
  project, or a Multibeam/Raster layer selected), no tab is checked and the
  modal host shows only the universal annotation sections.
- `refreshInspectorModalities` hides the whole tab strip when the project has
  no sensor modalities (no dangling empty header), and treats "nothing
  checked" as always-valid instead of requiring the old Map fallback tab.

### PanelTabBar::clearSelection()
The bar's QButtonGroup is exclusive, and exclusive groups refuse programmatic
`setChecked(false)` on the checked button (same Qt behavior as the slice-77
toolbar issue). New `clearSelection()` lifts exclusivity, unchecks all,
restores it. `refreshSensorTab` now clears first, then checks the target
sensor tab if there is one. `m_sensor_bar` is typed `PanelTabBar*` (was
`QWidget*`) so the coordinator can reach it.

### QC fix found while verifying
- **View ▸ "Tool Bar (Right)" removed** — it toggled the right tool rail that
  slice 82 retired; the action was operating on a null widget (dead clickable
  UI, D-05). The null-guarded `setRightToolBarVisible` plumbing stays for
  workspace-state compatibility.

## Verification
- Build green; `ctest -E PerfBaseline` → 16/16 passed.
- Live-verified via in-app widget grab (the VM's 1280×720 virtual display is
  smaller than the app's 1440×900 default window and its native GL viewport
  overpaints siblings in screen captures — environment artifact, not a bug;
  layout geometry was confirmed correct by instrumented dump):
  - Empty project: sensor bar hidden, panel + right edge strip present.
  - Sidescan project loaded: bar shows only "SSS", auto-checked; SBP/MAG
    hidden; upper strip still Properties | Map | History; universal Contact
    Picking sections intact.
