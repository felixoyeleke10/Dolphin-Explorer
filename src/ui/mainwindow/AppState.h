#pragma once
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/mainwindow/Notification.h"
#include "app/layers/LayerUtils.h"   // Modality enum
#include <QObject>
#include <string>

namespace dolphin::ui {

// Active map/viewer tool. Matches MapView::InputMode values where they overlap.
enum class ToolMode {
    Pan         = 0,  // default navigation cursor
    Select      = 1,  // rubber-band selection
    Zoom        = 2,  // zoom-to-rect
    Measure     = 3,  // distance measurement
    ContactPick = 4,  // point contact placement
};

// Typed snapshot of what the user currently has selected.
// layer_id is empty and modality is Unknown when nothing is selected.
struct SelectionState {
    std::string   layer_id;
    app::Modality modality = app::Modality::Unknown;

    bool operator==(const SelectionState& o) const noexcept {
        return layer_id == o.layer_id && modality == o.modality;
    }
    bool operator!=(const SelectionState& o) const noexcept { return !(*this == o); }
};

// Central live-settings authority. One instance is owned by MainWindow.
//
// Responsibilities:
//   • Holds the current settings struct in memory — single source of truth.
//   • Emits settingsChanged(s) for broad refresh paths (applyLiveSettings).
//   • Emits fine-grained signals so individual coordinators can react to
//     exactly the settings that affect them without re-running unrelated paths.
//
// The dialog (AppSettingsDialog) owns QSettings persistence: it saves before
// emitting applied(s). AppState::apply() receives that signal and distributes
// it to subscribers. At startup, load() reads from QSettings without emitting.
class AppState : public QObject {
    Q_OBJECT
public:
    explicit AppState(QObject* parent = nullptr);

    const AppSettingsDialog::Settings& current()   const { return m_current; }
    const SelectionState&              selection() const { return m_selection; }
    ToolMode                           toolMode()  const { return m_tool_mode; }

    // Read persisted settings from QSettings into the live state.
    // Called once at construction; does NOT emit any signals.
    void load();

    // Distribute a new settings snapshot: update live state, emit signals for
    // everything that changed. Persistence is the caller's (dialog's) job.
    void apply(const AppSettingsDialog::Settings& s);

    // Update the active selection. Emits selectionChanged only if it differs.
    void setSelection(const SelectionState& s);

    // Update the active tool. Emits toolModeChanged only if it differs.
    void setToolMode(ToolMode mode);

    // Route a structured notification to any subscribed UI consumer.
    void postNotification(const Notification& n);

signals:
    // Broad — MainWindow::applyLiveSettings subscribes here.
    void settingsChanged(const AppSettingsDialog::Settings& s);

    // Fine-grained — individual coordinators subscribe as needed.
    // Only emitted when the specific value actually changed.
    void soundVelocityChanged(double v);
    void defaultPaletteChanged(int idx);
    void autoStretchChanged(bool v);

    // Emitted when the active layer / modality selection changes.
    void selectionChanged(const SelectionState& s);

    // Emitted when the active tool changes.
    void toolModeChanged(dolphin::ui::ToolMode mode);

    // Emitted for any structured notification (info/warning/error from any subsystem).
    void notificationPosted(const dolphin::ui::Notification& n);

private:
    AppSettingsDialog::Settings m_current;
    SelectionState              m_selection;
    ToolMode                    m_tool_mode = ToolMode::Pan;
};

} // namespace dolphin::ui
