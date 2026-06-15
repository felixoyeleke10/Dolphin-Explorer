# Stage 06 — Slice 02 Closure: LayerDisplayCoordinator Extraction

## What shipped

Extracted layer-selection state and navigation history out of `MainWindow` into a new `LayerDisplayCoordinator` (LDC) QObject:

**New files:**
- `src/ui/mainwindow/LayerDisplayCoordinator.h`
- `src/ui/mainwindow/LayerDisplayCoordinator.cpp`

**Members moved out of MainWindow:**
- `m_active_layer_id` → `LDC::m_active_layer_id`
- `m_navigation_history` → `LDC::m_navigation_history`
- `m_navigation_index` → `LDC::m_navigation_index`
- `m_replaying_navigation` → `LDC::m_replaying`

**Methods moved into LDC:**
- `onNavigateBack()`, `onNavigateForward()` (now public slots)
- `clearNavigationHistory()` → `clearHistory()`
- `pruneNavigationHistory()` → `pruneHistory(project*)`
- `recordNavigationSelection()` → `recordSelection(id)`
- `updateNavigationButtons()` (replaced by `navigationChanged` signal)

**LDC signals:**
- `layerActivationRequested(string)` — back/forward navigation fires this; MainWindow calls `onLayerSelected()` in response
- `navigationChanged(bool back, bool fwd)` — MainWindow enables/disables nav toolbar buttons

**MainWindow wiring:**
- Nav buttons now connect to `m_layer_ctrl->navigateBack/Forward()` directly (Chrome.cpp)
- `layerActivationRequested` → `MainWindow::onLayerSelected`
- `navigationChanged` → lambda enabling `m_btn_nav_back` / `m_btn_nav_forward`

**Helper on MainWindow:** `activeLayerId()` returns `m_layer_ctrl->activeLayerId()` — a `const std::string&` read used in all 30+ call sites across 8 aspect files.

**`onLayerSelected` stays in MainWindow** — it drives 12+ widget subsystems and is fundamentally view-dispatch code. It now calls `m_layer_ctrl->recordSelection()` and `m_layer_ctrl->setActiveLayer()` instead of doing the history/state management itself.

**Files touched:** `MainWindow.h`, `MainWindow.cpp`, `MainWindow.Chrome.cpp`, `MainWindow.Layout.cpp`, `MainWindow.MapContextMenu.cpp`, `MainWindow.ProjectBinding.cpp`, `MainWindow.Tools.cpp`, `coordinators/MainWindow.LayerCoordinator.cpp`, `coordinators/MainWindow.WaterfallCoordinator.cpp`, `coordinators/MainWindow.NodeGraphCoordinator.cpp`, `coordinators/MainWindow.SubBottomCoordinator.cpp`, `CMakeLists.txt`

Builds clean (20/20 Ninja steps, DolphinExplorer.exe linked).

## What's next

Both extraction slices from the original plan are done. Potential follow-on:
- Extract `ImportController` wiring from MainWindow.cpp into its own coordinator
- Begin `LayerDisplayCoordinator` → `onLayerSelected` modality dispatch refactor (SBP map build could move into an `SbpMapCoordinator`)
