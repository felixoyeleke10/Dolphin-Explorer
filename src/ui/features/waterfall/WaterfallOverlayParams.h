#pragma once
#include <QColor>
#include <QPen>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  WfOverlayParams — visual overlay parameters for WaterfallView.
//
//  These are pure display/UX concerns: crosshair appearance and the optional
//  slant-range grid.  They have no effect on the signal-processing pipeline
//  and are managed exclusively by WaterfallSettingsDialog.
//
//  Stored in WaterfallView::m_overlay_params.
//  Applied via WaterfallWindow::applyWfSettings().
//  Persisted to QSettings by WaterfallSettingsDialog::onApply().
// ─────────────────────────────────────────────────────────────────────────────

struct WfOverlayParams {
    // ── Crosshair ─────────────────────────────────────────────────────────
    bool         xhair_show    = true;
    QColor       xhair_color   = QColor(255, 255, 255);  // white
    Qt::PenStyle xhair_style   = Qt::SolidLine;
    int          xhair_width   = 1;                      // px
    int          xhair_opacity = 22;                     // 0–100 %

    // ── Range grid (vertical lines at constant slant-range intervals) ─────
    bool         grid_show       = false;
    QColor       grid_color      = QColor(255, 255, 255);
    int          grid_opacity    = 20;                   // 0–100 %
    float        grid_interval_m = 0.0f;                 // 0 = auto
};

} // namespace dolphin::ui
