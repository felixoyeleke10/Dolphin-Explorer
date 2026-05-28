#pragma once
#include "core/SpatialRef.h"
#include <QWidget>

class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace dolphin::app { class Project; }

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  GeodesyPanel — left-context panel for CRS / coordinate management.
//
//  Shows the active source CRS (or "Auto-detect"), a summary of per-layer
//  coordinate states, and an Apply button.  Lives as a page in the main
//  context stack; toggled by the toolbar Geodesy button.
//
//  The panel is intentionally thin: it emits signals and lets MainWindow
//  own the state (m_pending_crs) and drive ImportService.
// ─────────────────────────────────────────────────────────────────────────────

class GeodesyPanel : public QWidget {
    Q_OBJECT
public:
    explicit GeodesyPanel(QWidget* parent = nullptr);

    // Call whenever the active project or the pending CRS changes.
    void refresh(dolphin::app::Project* project, const core::SpatialRef& pending_crs);

signals:
    // User confirmed a new CRS in the picker.
    void crsChanged(const core::SpatialRef& crs);
    // User clicked Apply — MainWindow should stamp crs onto unconfirmed layers.
    void applyRequested(const core::SpatialRef& crs, bool force_all);

private:
    void buildCrsSection(QVBoxLayout* root);
    void buildLayersSection(QVBoxLayout* root);

    void refreshCrsDisplay();
    void refreshLayerList();

    dolphin::app::Project* m_project    = nullptr;
    core::SpatialRef       m_crs;           // current pending/confirmed CRS

    // CRS section
    QLabel*      m_crs_value    = nullptr;
    QPushButton* m_browse_btn   = nullptr;
    QPushButton* m_clear_btn    = nullptr;

    // Layers section
    QFrame*      m_layers_card  = nullptr;
    QVBoxLayout* m_layers_vl    = nullptr;
    QLabel*      m_no_proj_lbl  = nullptr;

    // Apply row
    QFrame*      m_apply_card   = nullptr;
    QPushButton* m_apply_btn    = nullptr;
    QCheckBox*   m_force_chk    = nullptr;
};

} // namespace dolphin::ui
