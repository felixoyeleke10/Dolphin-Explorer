// WaterfallViewPaint.cpp — OpenGL initialisation, resize, paint, and cleanup
//
// GPU path (default — requires OpenGL 3.3 core):
//   initializeGL — compile shaders
//   paintGL      — flush pending texture uploads, one GL draw call for the
//                  amplitude raster, then QPainter for all overlays on top
//   cleanupGL    — release GL objects while context is still current
//
// CPU fallback: if GL 3.3 is unavailable, m_gl_initialized stays false and
//   paintGL falls back to WaterfallRenderer's CPU strip-cache path.

#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/painters/WaterfallOverlayPainter.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QPainter>
#include <QPen>
#include <QPolygon>

namespace {
const QColor kSeabedBandFill  (255, 220,   0,  22);  // selection box while drawing seabed pick
const QColor kSeabedBandStroke(255, 220,   0, 180);
const QColor kExternalContact (  0, 210, 220, 220);  // contacts from external data source (cyan)
} // namespace

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  initializeGL
// -----------------------------------------------------------------------------

void WaterfallView::initializeGL()
{
    connect(context(), &QOpenGLContext::aboutToBeDestroyed,
            this, &WaterfallView::cleanupGL,
            Qt::UniqueConnection);

    // Verify the driver actually delivered OpenGL 3.3 Core Profile before
    // attempting shader/VAO initialisation.  Qt silently degrades the context
    // when the hardware or driver cannot fulfil the request, so we check the
    // format that was actually obtained rather than the one we asked for.
    const QSurfaceFormat actual = context()->format();
    const bool have_33 = (actual.majorVersion() > 3)
                      || (actual.majorVersion() == 3 && actual.minorVersion() >= 3);
    if (!have_33 || actual.profile() != QSurfaceFormat::CoreProfile) {
        qWarning("[WF-GL] CPU fallback: got OpenGL %d.%d %s, need 3.3 Core",
                 actual.majorVersion(), actual.minorVersion(),
                 actual.profile() == QSurfaceFormat::CoreProfile ? "Core" : "Compat");
        return;   // m_gl_initialized stays false — CPU path handles all painting
    }

    m_gl_initialized = m_gl_renderer.initialize();
    if (!m_gl_initialized) {
        qWarning("[WF-GL] CPU fallback: GPU renderer initialisation failed "
                 "(shader compile or VAO error)");
        return;
    }

    if (!m_rows.empty())
        m_gl_renderer.uploadAmplitude(m_rows);
    m_gl_data_dirty = false;
    m_gl_src_dirty  = false;
}

// -----------------------------------------------------------------------------
//  resizeGL
// -----------------------------------------------------------------------------

void WaterfallView::resizeGL(int /*w*/, int /*h*/)
{
    updateLayout();
    m_scroll.clampToMax(rowCount(), m_renderer.maxVisible());
}

// -----------------------------------------------------------------------------
//  paintGL
// -----------------------------------------------------------------------------

void WaterfallView::paintGL()
{
    updateLayout();

    if (m_scroll.wantEnd()) {
        m_scroll.resolveWantEnd(rowCount(), m_renderer.maxVisible());
        m_dirty = true;
    }

    const WfLayout& lay = m_renderer.layout();

    // -- GPU path -------------------------------------------------------------
    if (m_gl_initialized) {
        // Flush any pending texture uploads (context is guaranteed current here).
        if (m_gl_data_dirty) {
            m_gl_renderer.uploadAmplitude(m_rows);
            m_gl_data_dirty = false;
            m_gl_src_dirty  = false;
        } else if (m_gl_src_dirty) {
            m_gl_renderer.uploadSrcParams(m_rows);
            m_gl_src_dirty = false;
        }
        if (m_gl_renderer.hasAmplitude()) {
            const bool show_port =
                (m_params.display_channel != DisplayChannel::Starboard);
            const bool show_stbd =
                (m_params.display_channel != DisplayChannel::Port);

            QPainter p(this);
            p.beginNativePainting();
            m_gl_renderer.draw(lay,
                               m_scroll.scrollRow(),
                               m_scroll.hZoom(),
                               m_scroll.hPan(),
                               show_port,
                               show_stbd,
                               m_params.slant_range_correction,
                               rowCount(),
                               m_params);
            p.endNativePainting();
            m_dirty = false;

            paintOverlays(p, lay);
            return;
        }
        // No amplitude data uploaded yet — fall through to CPU path.
    }

    // -- CPU fallback path ----------------------------------------------------
    if (m_dirty) {
        m_renderer.rebuild(m_rows,
                           m_scroll.scrollRow(),
                           m_scroll.hZoom(),
                           m_scroll.hPan());
        m_dirty = false;
    }

    QPainter p(this);
    if (!m_renderer.image().isNull())
        p.drawImage(0, kWfScaleBarH, m_renderer.image());
    paintOverlays(p, lay);
}

// -----------------------------------------------------------------------------
//  paintOverlays — common to both GPU and CPU paths
// -----------------------------------------------------------------------------

void WaterfallView::paintOverlays(QPainter& p, const WfLayout& lay)
{
    if (m_seabed_tool > 0 || m_show_seabed_line) {
        WaterfallOverlayPainter::paintSeabedOverlay(p, m_rows, lay, m_scroll);
    }

    if (m_seabed_tool == 2 && m_box_press_sx >= 0 && m_cursor_x >= 0) {
        const QRect band(QPoint(qMin(m_box_press_sx, m_cursor_x),
                                qMin(m_box_press_sy, m_cursor_y)),
                         QPoint(qMax(m_box_press_sx, m_cursor_x),
                                qMax(m_box_press_sy, m_cursor_y)));
        p.save();
        p.fillRect(band, kSeabedBandFill);
        QPen rp(kSeabedBandStroke);
        rp.setWidth(1);
        rp.setStyle(Qt::DashLine);
        p.setPen(rp);
        p.drawRect(band);
        p.restore();
    }

    if (!m_external_contacts.empty()) {
        WaterfallOverlayPainter::paintContactOverlay(p, m_external_contacts, m_rows, lay,
                                                     m_scroll, kExternalContact);
    }
    if (!m_contacts.empty()) {
        WaterfallOverlayPainter::paintContactOverlay(p, m_contacts, m_rows, lay, m_scroll);
    }
    // Feature draw draft (polygon/polyline in progress) — teal, dashed live edge.
    if (m_feature_tool != 0 && !m_feature_px.empty()) {
        const QColor line (120, 240, 255, 230);
        const QColor live (120, 240, 255, 150);
        const QColor fill (120, 240, 255,  40);
        const QColor vtx  ( 60, 200, 200, 240);
        const bool polygon = (m_feature_tool == 1);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        QPolygon chain;
        for (const QPoint& pt : m_feature_px) chain << pt;

        if (polygon && chain.size() >= 2 && m_cursor_x >= 0) {
            QPolygon closed = chain;
            closed << QPoint(m_cursor_x, m_cursor_y);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawPolygon(closed);
        }
        if (chain.size() >= 2) {
            p.setPen(QPen(line, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(chain);
        }
        if (m_cursor_x >= 0) {
            p.setPen(QPen(live, 1.5, Qt::DashLine));
            p.drawLine(chain.back(), QPoint(m_cursor_x, m_cursor_y));
            if (polygon && chain.size() >= 2)
                p.drawLine(QPoint(m_cursor_x, m_cursor_y), chain.front());
        }
        p.setPen(QPen(QColor(0, 0, 0, 150), 1));
        p.setBrush(vtx);
        for (const QPoint& pt : chain) p.drawEllipse(pt, 3, 3);
        p.restore();
    }

    WaterfallOverlayPainter::paintScaleBar(p, m_rows, lay, m_scroll, samplesPerPing());
    if (m_show_amp_bar) {
        if (m_amp_profile_dirty || m_scroll.scrollRow() != m_amp_profile_scroll_row) {
            m_cached_amp_profile     = computeAmpProfile(m_rows, m_scroll, lay);
            m_amp_profile_scroll_row = m_scroll.scrollRow();
            m_amp_profile_dirty      = false;
        }
        WaterfallOverlayPainter::paintAmplitudeBar(p, m_cached_amp_profile, lay, m_scroll);
    }
    WaterfallOverlayPainter::paintRangeGrid  (p, m_rows, lay, m_scroll, m_overlay_params);
    WaterfallOverlayPainter::paintCrosshair  (p, lay, m_cursor_x, m_cursor_y, m_overlay_params);
    WaterfallOverlayPainter::paintEmptyState (p, rect(), m_rows.empty());
}

// -----------------------------------------------------------------------------
//  GPU acceleration control
// -----------------------------------------------------------------------------

bool WaterfallView::isGpuRendering() const { return m_gl_initialized; }

void WaterfallView::setGpuAccel(bool enabled)
{
    if (!enabled) {
        m_gl_initialized = false;
        update();
        return;
    }
    if (!m_gl_initialized && isValid()) {
        makeCurrent();
        m_gl_initialized = m_gl_renderer.initialize();
        if (m_gl_initialized && !m_rows.empty()) {
            m_gl_renderer.uploadAmplitude(m_rows);
            m_gl_data_dirty = false;
        }
        doneCurrent();
    }
    update();
}

// -----------------------------------------------------------------------------
//  cleanupGL — called via aboutToBeDestroyed while context is current
// -----------------------------------------------------------------------------

void WaterfallView::cleanupGL()
{
    if (!isValid()) {
        m_gl_initialized = false;
        return;
    }

    makeCurrent();
    m_gl_renderer.cleanup();
    m_gl_initialized = false;
    doneCurrent();
}

} // namespace dolphin::ui
