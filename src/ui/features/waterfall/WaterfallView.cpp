// WaterfallView.cpp — construction and layout
//
// Data API (setPings, setParams, setSeabedTool, …) → WaterfallViewData.cpp
// Paint / overlay                                  → WaterfallViewPaint.cpp
// Seabed helpers (gap-fill, box-detect, pen-snap)  → WaterfallViewSeabed.cpp
// Signal-processing algorithms                     → WaterfallViewProcessing.cpp
// Mouse / wheel / resize events                    → WaterfallViewInput.cpp

#include "ui/features/waterfall/WaterfallView.h"
#include "render/sonar/SSSPalette.h"

#include <QOpenGLContext>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  Compatibility forwarder
// ─────────────────────────────────────────────────────────────────────────────

const char* WaterfallView::paletteName(int idx)
{
    return SSSPalette::name(idx);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────

WaterfallView::WaterfallView(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(200, 100);

    m_scroll.setScrollChangedCallback(
        [this](int sr, int tr, int vr) {
            emit scrollChanged(sr, tr, vr);
        });
}

WaterfallView::~WaterfallView()
{
    if (QOpenGLContext* ctx = context()) {
        disconnect(ctx, &QOpenGLContext::aboutToBeDestroyed,
                   this, &WaterfallView::cleanupGL);
    }

    if (m_gl_initialized && isValid()) {
        makeCurrent();
        m_gl_renderer.cleanup();
        m_gl_initialized = false;
        doneCurrent();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallView::updateLayout()
{
    const qreal dpr = devicePixelRatio();
    if (width() == m_last_w && height() == m_last_h && dpr == m_last_dpr && !m_layout_dirty) return;
    m_last_w   = width();
    m_last_h   = height();
    m_last_dpr = dpr;
    m_layout_dirty = false;

    const int old_visible = m_renderer.maxVisible();

    WfLayout layout;
    layout.widget_w = width();
    layout.widget_h = height();
    layout.nadir_x  = (width() - kWfRulerW) / 2 + kWfRulerW;
    layout.img_h    = qMax(0, height() - kWfScaleBarH - (m_show_amp_bar ? kWfAmpBarH : 0));

    layout.dpr          = dpr;
    layout.phys_w       = qRound(layout.widget_w  * layout.dpr);
    layout.phys_widget_h = qRound(layout.widget_h * layout.dpr);
    layout.phys_img_h   = qRound(layout.img_h     * layout.dpr);
    layout.phys_nadir   = qRound(layout.nadir_x   * layout.dpr);
    layout.phys_ruler_w = qRound(kWfRulerW        * layout.dpr);

    if (m_v_pings_per_cm > 0.f) {
        const float dpi_y = physicalDpiY() > 0 ? static_cast<float>(physicalDpiY()) : 96.f;
        layout.row_height = std::max(1, static_cast<int>(dpi_y / 2.54f / m_v_pings_per_cm + 0.5f));
    }
    layout.phys_row_h   = qRound(layout.row_height * layout.dpr);

    m_renderer.setLayout(layout);
    m_dirty             = true;
    m_amp_profile_dirty = true;

    if (m_renderer.maxVisible() != old_visible && !m_rows.empty())
        m_scroll.resync(rowCount(), m_renderer.maxVisible());
}

} // namespace dolphin::ui
