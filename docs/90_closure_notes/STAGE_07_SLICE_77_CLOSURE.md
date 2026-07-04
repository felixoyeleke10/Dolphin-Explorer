# Stage 07 · Slice 77 — MainWindow tool-state QC (annotation tools × exclusive toolbar group)

## Goal
QC pass over the MainWindow surface of the uncommitted work (feature-tool
redesign wiring, SSS-parity panels) tracing tool-state flows for desyncs, plus
a parity re-verification of the new Gain/Imaging controls.

## Bugs found and fixed

### 1. Exclusive QButtonGroup silently refuses unchecking (state desync)
The nav toolbar (Cursor/Select/Zoom/Measure/Contact) lives in one **exclusive**
`QButtonGroup`. Qt refuses *programmatic* `setChecked(false)` on the checked
button of an exclusive group (`QAbstractButton::setChecked` early-returns), so
`syncAnnotationToggles(false, …)` never actually uncheck­ed the Contact button:

- Activate Contact → pick a feature tool → toolbar still shows **Contact
  active** while the map draws features.
- Toggle contact off from the panel → toolbar Contact stays checked while the
  map pans.

**Fix** (`MainWindow.Tools.cpp` / `.ToolBar.cpp` / `.h`): the group is now the
member `m_tool_grp`. `syncAnnotationToggles`:
- contact on → check the Contact button (group unchecks the previous tool);
- feature tool on → clear **all** nav checks by briefly lifting exclusivity
  (`setExclusive(false)` → uncheck signal-blocked → `setExclusive(true)`) — a
  feature tool owns the map, so no toolbar tool shows active;
- both off → nothing: the nav-tool handler taking over checks its own button.

### 2. Divergent contact-activation paths
The panel `pickToggled(true)` handler duplicated `onAddContact()` minus the
`ViewportHost`/`AppState` tool-mode updates and the no-project guard, so
activating contact picking from the panel left the app-wide tool mode stale.
**Fix**: the panel handler now exits 3D then calls `onAddContact()`; toggle-off
calls `onToolCursor()` (single deactivation path: pan mode + Cursor checked +
AppState synced). `onDrawFeature(0)` likewise routes through `onToolCursor()`.

### 3. No-project early-returns left buttons checked
Clicking toolbar Contact with no project checked the button (the exclusive
group checks on click, before the slot runs) then early-returned — UI claimed
an inactive mode. Same for the feature panel buttons. **Fix**: `onAddContact`
falls back to `onToolCursor()`; `onDrawFeature` snaps the panel buttons off via
`syncAnnotationToggles(false, 0)`.

## Verified clean (no action needed)
- **Gain panel AGC parity**: Mode/Strength/Along-Track Window/Smoothing
  Type/Smoothing Window/Edge Skip/Noise Floor — ranges, defaults, steps, units
  and tooltips match the waterfall exactly; `setParams` ends with
  `updateControlStates()` so Variable-mode visibility is correct after load.
- **Imaging panel parity**: Destripe Window/Subdivision/Capping, BPN Smooth
  Radius, MLE Tile Pings/Tile Samps — exact waterfall parity; signal blockers
  complete.
- **MapView never self-switches input modes** — mode ownership stays with
  MainWindow, so no hidden panel-toggle desync paths.
- **Viewer exclusivity (SBP + waterfall)** — contact↔feature (↔seabed in the
  waterfall) wiring is symmetric; view setters clear the opposing tool, panel
  setters are signal-blocked. One nit fixed: waterfall feature status text now
  mentions the pen, matching the SBP viewer (SBP/SSS parity).

## Verification
Full build green (MSVC + Ninja); `ctest -E PerfBaseline` → 16/16 passed.
