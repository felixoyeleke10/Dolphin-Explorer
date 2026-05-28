#pragma once
#include <QColor>
#include <QPen>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  SubBottomViewStyle — visual overlay parameters for SubBottomView.
//
//  Crosshair, depth grid, and bottom-track render style are not managed by
//  SubBottomDisplayPanel (which lives in the right side panel and handles
//  palette / gain / sound-speed only). They are set exclusively via
//  SubBottomSettingsDialog and applied through SubBottomWindow::applySettings().
// ─────────────────────────────────────────────────────────────────────────────

struct SubBottomViewStyle {
    // ── Crosshair ─────────────────────────────────────────────────────────
    bool         xhair_show     = true;
    QColor       xhair_color    = QColor(0, 255, 255);   // cyan
    Qt::PenStyle xhair_style    = Qt::DashLine;
    int          xhair_width    = 1;                     // px
    int          xhair_opacity  = 45;                    // 0–100 %

    // ── Depth grid (horizontal lines at constant two-way time intervals) ──
    bool         grid_show        = false;
    QColor       grid_color       = QColor(255, 255, 255);
    int          grid_opacity     = 20;                  // 0–100 %
    float        grid_interval_ms = 0.0f;                // 0 = auto

    // ── Bottom track stripe ───────────────────────────────────────────────
    QColor       bt_color        = QColor(255, 60, 60);
    int          bt_thickness    = 2;                    // px stripe height (1–6)
};

} // namespace dolphin::ui
