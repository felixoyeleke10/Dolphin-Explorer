# Stage 06 — Slice 03 Closure: ActivityLog + TaskProgressController Extraction

## What shipped

Completed the third and final extraction pass from `MainWindow`, pulling the activity journal
and processing-dialog lifecycle into their own named types.

### ActivityLog

**New file:** `src/ui/mainwindow/ActivityLog.h` (header-only)

- `enum class ActivityKind` moved out of `MainWindow.h`
- `struct ActivityEntry` (`kind`, `description`, `timestamp`) moved out of `MainWindow.h`
- `class ActivityLog` — lightweight non-QObject journal with `record()`, `entries()`, `clear()`; capped at 1000 entries

**Members removed from MainWindow:**
- `enum class ActivityKind {...}` (15 values)
- `struct ProjectActivityEntry {...}` (renamed to `ActivityEntry` in its new home)
- `std::vector<ProjectActivityEntry> m_activity_log`
- `static constexpr int kActivityLogMaxEntries = 1000` (from Layout.cpp)

**Replaced with:** `ActivityLog m_activity_log;` (value member, no allocation overhead)

**MainWindow.Layout.cpp** updated to use the new `.entries()` API; `recordActivity()` now
delegates to `m_activity_log.record(kind, description)`.

---

### TaskProgressController

**New files:**
- `src/ui/mainwindow/TaskProgressController.h`
- `src/ui/mainwindow/TaskProgressController.cpp`

**Owns:** lazy-created `ProcessingDialog* m_dlg`, `OperationManager* m_op_mgr`

**Public slots:** `taskBegin(id, label)`, `taskDone(id)`, `taskFail(id, error)`

**Internal:** `onCancelRequested()` → `m_op_mgr->cancelAll()`;
`onDialogDestroyed()` → clears the `m_dlg` pointer after `WA_DeleteOnClose`

**Members removed from MainWindow:**
- `ProcessingDialog* m_processing_dlg = nullptr;`
- `void onCancelProcessing();`
- `class ProcessingDialog;` forward declaration

**Replaced with:** `TaskProgressController* m_task_ctrl = nullptr;`

**MainWindow.Processing.cpp** rewritten as 3-line delegates to `m_task_ctrl`.

**MainWindow.cpp** constructs `m_task_ctrl` immediately after `m_session_ctrl` and
`m_layer_ctrl`, in the same PSC/LDC block.

---

## Files changed

| File | Change |
|------|--------|
| `src/ui/mainwindow/ActivityLog.h` | **Created** |
| `src/ui/mainwindow/TaskProgressController.h` | **Created** |
| `src/ui/mainwindow/TaskProgressController.cpp` | **Created** |
| `src/ui/mainwindow/MainWindow.h` | Removed old enum/struct/vector/dlg members; added includes + new members |
| `src/ui/mainwindow/MainWindow.cpp` | Added `m_task_ctrl` construction |
| `src/ui/mainwindow/MainWindow.Layout.cpp` | Switched to `ActivityLog` API |
| `src/ui/mainwindow/MainWindow.Processing.cpp` | Rewritten as thin delegates |
| `src/ui/CMakeLists.txt` | Added `TaskProgressController.cpp` |

## Build result

138/138 Ninja steps — clean link, `DolphinExplorer.exe` produced.

## What's next

`MainWindow` is now a composition shell: project lifecycle (PSC), layer display (LDC),
activity journal (ActivityLog), and processing-dialog lifecycle (TPC) each live in a named
type. The remaining logic in aspect files is either view-dispatch (correct place) or
import/correction orchestration (deferred, complex dependency graph).
