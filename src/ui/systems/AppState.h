#pragma once
#include "ui/systems/Notification.h"
#include "app/layers/LayerUtils.h"   // Modality enum
#include "ui/shell/Theme.h"
#include <QColor>
#include <QObject>
#include <QString>
#include <string>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  AppSettings — application-wide live settings.
//  Canonical definition is here so spine systems can depend on AppState alone
//  without pulling in the dialog.  AppSettingsDialog exposes this as a type
//  alias so all existing code continues to work unchanged.
// -----------------------------------------------------------------------------
struct AppSettings {
    // -- General -------------------------------------------------------
    int     units_system    = 0;
    int     depth_unit      = 0;
    int     coord_format    = 0;
    int     date_format     = 0;

    // -- Appearance ----------------------------------------------------
    int     theme           = 0;
    int     ui_density      = 1;
    int     font_size       = 1;
    bool    show_tooltips   = true;
    bool    show_grid       = true;

    // -- Performance ---------------------------------------------------
    int     worker_threads  = 4;
    int     tile_cache_mb   = 512;
    int     autosave_min    = 5;
    bool    gpu_accel       = true;

    // -- Data defaults -------------------------------------------------
    double  sound_velocity  = 1500.0;
    bool    auto_stretch    = true;
    int     default_palette = 0;
    int     default_crs     = 0;

    // -- Export --------------------------------------------------------
    QString export_dir;
    int     export_format   = 0;

    // -- Map -----------------------------------------------------------
    QColor  map_bg_color    { Theme::kBg };
    int     map_grid_preset    = 0;
    int     grid_label_size    = 1;
    bool    grid_label_rotated = false;
    int     graticule_coord    = 0;
};

// Active map/viewer tool. Matches MapView::InputMode values where they overlap.
enum class ToolMode {
    Pan         = 0,
    Select      = 1,
    Zoom        = 2,
    Measure     = 3,
    ContactPick = 4,
};

// Typed snapshot of what the user currently has selected.
struct SelectionState {
    std::string   layer_id;
    app::Modality modality = app::Modality::Unknown;

    bool operator==(const SelectionState& o) const noexcept {
        return layer_id == o.layer_id && modality == o.modality;
    }
    bool operator!=(const SelectionState& o) const noexcept { return !(*this == o); }
};

// Central live-settings authority. One instance is owned by MainWindow.
class AppState : public QObject {
    Q_OBJECT
public:
    explicit AppState(QObject* parent = nullptr);

    const AppSettings&    current()   const { return m_current; }
    const SelectionState& selection() const { return m_selection; }
    ToolMode              toolMode()  const { return m_tool_mode; }

    void load();
    void apply(const AppSettings& s);
    void setSelection(const SelectionState& s);
    void setToolMode(ToolMode mode);
    void postNotification(const Notification& n);

signals:
    void settingsChanged(const AppSettings& s);
    void soundVelocityChanged(double v);
    void defaultPaletteChanged(int idx);
    void autoStretchChanged(bool v);
    void selectionChanged(const SelectionState& s);
    void toolModeChanged(dolphin::ui::ToolMode mode);
    void notificationPosted(const dolphin::ui::Notification& n);

private:
    AppSettings    m_current;
    SelectionState m_selection;
    ToolMode       m_tool_mode = ToolMode::Pan;
};

} // namespace dolphin::ui
