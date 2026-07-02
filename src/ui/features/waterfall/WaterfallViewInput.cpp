// WaterfallViewInput.cpp — mouse, wheel, resize, and leave event handlers

#include "ui/features/waterfall/WaterfallView.h"

#include <QContextMenuEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Geo conversion — shared by contact picking and feature drawing
// -----------------------------------------------------------------------------

bool WaterfallView::rangeToGeo(int row, core::SidescanChannel ch, float range_m,
                               double& lat, double& lon, bool& is_projected) const
{
    if (row < 0 || row >= rowCount()) return false;

    const double nav_lat = m_rows[row].lat;
    const double nav_lon = m_rows[row].lon;
    is_projected         = m_rows[row].is_projected;
    lat = nav_lat;
    lon = nav_lon;
    if (nav_lat == 0.0 && nav_lon == 0.0) return false;

    const float heading  = m_rows[row].heading_deg;
    const float altitude = m_rows[row].altitude_m;

    // Slant → ground range (Pythagorean correction when altitude known)
    float ground_range = range_m;
    if (altitude > 0.f && range_m > altitude)
        ground_range = std::sqrt(range_m * range_m - altitude * altitude);

    // Bearing perpendicular to heading: port = left, stbd = right
    const float bearing_deg =
        (ch == core::SidescanChannel::Port) ? heading - 90.f : heading + 90.f;
    const double bearing_rad = bearing_deg * M_PI / 180.0;

    if (!is_projected) {
        constexpr double R = 6371000.0;
        const double d    = ground_range / R;
        const double lat1 = nav_lat * M_PI / 180.0;
        const double lon1 = nav_lon * M_PI / 180.0;
        const double lat2 = std::asin(std::sin(lat1) * std::cos(d)
                                      + std::cos(lat1) * std::sin(d) * std::cos(bearing_rad));
        const double lon2 = lon1 + std::atan2(
            std::sin(bearing_rad) * std::sin(d) * std::cos(lat1),
            std::cos(d) - std::sin(lat1) * std::sin(lat2));
        lat = lat2 * 180.0 / M_PI;
        lon = lon2 * 180.0 / M_PI;
    } else {
        lat = nav_lat + ground_range * std::cos(bearing_rad);
        lon = nav_lon + ground_range * std::sin(bearing_rad);
    }
    return true;
}

// -----------------------------------------------------------------------------
//  Mouse events
// -----------------------------------------------------------------------------

void WaterfallView::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) { ev->ignore(); return; }

    const int rh  = m_renderer.layout().row_height;
    const int row = m_scroll.scrollRow() + (ev->pos().y() - kWfScaleBarH) / rh;

    if (m_contact_tool == 1 && row >= 0 && row < rowCount()) {
        // Contact pick — record a WfContact and emit contactPicked.
        core::SidescanChannel ch;
        float range_m = 0.f;
        if (m_renderer.xToRange(ev->pos().x(), row, m_rows,
                                 m_scroll.hZoom(), m_scroll.hPan(), ch, range_m)
            && range_m > 0.f) {
            WfContact c;
            c.row_idx        = row;
            c.ch             = ch;
            c.range_m        = range_m;
            c.classification = m_contact_class;
            m_contacts.push_back(c);

            // Compute real contact position: offset nav point by ground range
            // perpendicular to the vessel heading (shared with feature drawing).
            double contact_lat = 0.0, contact_lon = 0.0;
            bool   proj        = false;
            rangeToGeo(row, ch, range_m, contact_lat, contact_lon, proj);
            // Square grab of the waterfall around the pick, used as the contact's
            // thumbnail in the Contact Manager. Grabbed now, before update()
            // repaints, so the new marker dot is not baked into the snapshot.
            QPixmap snapshot;
            {
                const QImage fb = grabFramebuffer();
                if (!fb.isNull()) {
                    const qreal dpr  = devicePixelRatioF();
                    const int   side = std::max(1, static_cast<int>(160 * dpr));
                    const int   cx   = static_cast<int>(ev->pos().x() * dpr);
                    const int   cy   = static_cast<int>(ev->pos().y() * dpr);
                    const int   w    = std::min(side, fb.width());
                    const int   h    = std::min(side, fb.height());
                    const int   x    = std::clamp(cx - w / 2, 0, fb.width()  - w);
                    const int   y    = std::clamp(cy - h / 2, 0, fb.height() - h);
                    snapshot = QPixmap::fromImage(fb.copy(QRect(x, y, w, h)));
                    snapshot.setDevicePixelRatio(dpr);
                }
            }
            emit contactPicked(row, ch, range_m, contact_lat, contact_lon, proj,
                               snapshot);

            m_dirty = true;
            update();
        }
        ev->accept();
        return;
    }

    if (m_feature_tool != 0 && row >= 0 && row < rowCount()) {
        // Feature draw — each click adds a geo vertex (offset like a contact pick).
        core::SidescanChannel ch;
        float range_m = 0.f;
        if (m_renderer.xToRange(ev->pos().x(), row, m_rows,
                                 m_scroll.hZoom(), m_scroll.hPan(), ch, range_m)) {
            double lat = 0.0, lon = 0.0;
            bool   proj = false;
            if (rangeToGeo(row, ch, range_m, lat, lon, proj)) {
                m_feature_pts.push_back(QPointF(lon, lat));   // (lon,lat)
                m_feature_px.push_back(ev->pos());
                m_feature_proj = proj;
                m_dirty = true;
                update();
            }
        }
        ev->accept();
        return;
    }

    // Helper: returns true if the clicked channel is allowed by m_seabed_channel.
    // 0=Both (always ok), 1=Port only, 2=Starboard only.
    auto channelAllowed = [this](core::SidescanChannel clicked) -> bool {
        if (m_seabed_channel == 0) return true;
        if (m_seabed_channel == 1) return clicked == core::SidescanChannel::Port;
        return clicked == core::SidescanChannel::Starboard;
    };

    if (m_seabed_tool == 1 && row >= 0 && row < rowCount()) {
        // Pen — click anywhere; smartPenRange snaps to the channel-selected array.
        const float range_m = smartPenRange(row, ev->pos().x());
        if (range_m > 0.f) {
            m_rows[row].seabed = {range_m, 1.f, true, true};
            if (m_rows[row].timestamp_us != 0)
                m_manual_seabed[m_rows[row].timestamp_us] = range_m;
            m_pen_last_row     = row;
            m_pen_last_range   = range_m;
            m_dirty = true;
            update();
        }
        m_seabed.beginDrag(row);
        ev->accept();
    } else if (m_seabed_tool == 2 && row >= 0 && row < rowCount()) {
        // Box — anchor the start; channel filtering is applied during detection.
        core::SidescanChannel ch;
        float range_m = 0.f;
        m_renderer.xToRange(ev->pos().x(), row, m_rows,
                             m_scroll.hZoom(), m_scroll.hPan(), ch, range_m);
        m_box_anchor_row   = row;
        m_box_anchor_range = (range_m > 0.f) ? range_m : 0.f;
        m_box_press_sx     = ev->pos().x();
        m_box_press_sy     = ev->pos().y();
        m_cursor_x         = ev->pos().x();
        m_cursor_y         = ev->pos().y();
        ev->accept();
    } else if (m_seabed_tool == 3 && row >= 0 && row < rowCount()) {
        // Eraser — always clears (seabed pick is channel-agnostic).
        if (m_rows[row].timestamp_us != 0)
            m_manual_seabed.erase(m_rows[row].timestamp_us);
        m_rows[row].seabed = {};
        m_dirty = true;
        update();
        ev->accept();
    }
}

void WaterfallView::mouseMoveEvent(QMouseEvent* ev)
{
    // -- Seabed pen drag ---------------------------------------------------
    if (m_seabed.isDragging() && (ev->buttons() & Qt::LeftButton)) {
        m_cursor_x = ev->pos().x();
        m_cursor_y = ev->pos().y();
        const int rh  = m_renderer.layout().row_height;
        const int row = m_scroll.scrollRow() + (ev->pos().y() - kWfScaleBarH) / rh;
        if (row >= 0 && row < rowCount()) {
            // Smart snap: place seabed at peak amplitude of the channel-selected array.
            const float snapped = smartPenRange(row, ev->pos().x());
            if (snapped > 0.f) {
                // Interpolate gaps caused by fast mouse movement.
                if (m_pen_last_row >= 0 && std::abs(row - m_pen_last_row) > 1) {
                    const int   r0     = qMin(m_pen_last_row, row);
                    const int   r1     = qMax(m_pen_last_row, row);
                    const float range0 = (m_pen_last_row < row) ? m_pen_last_range : snapped;
                    const float range1 = (m_pen_last_row < row) ? snapped : m_pen_last_range;
                    const int   len    = r1 - r0 + 1;
                    for (int i = r0; i <= r1; ++i) {
                        if (i >= 0 && i < rowCount()) {
                            const float t = (len > 1) ? static_cast<float>(i - r0) / (len - 1) : 0.f;
                            const float r = range0 + t * (range1 - range0);
                            m_rows[i].seabed = {r, 1.f, true, true};
                            if (m_rows[i].timestamp_us != 0)
                                m_manual_seabed[m_rows[i].timestamp_us] = r;
                        }
                    }
                } else {
                    m_rows[row].seabed = {snapped, 1.f, true, true};
                    if (m_rows[row].timestamp_us != 0)
                        m_manual_seabed[m_rows[row].timestamp_us] = snapped;
                }
                m_pen_last_row   = row;
                m_pen_last_range = snapped;
            }
        }
        m_dirty = true;
        update();
        return;
    }

    // -- Box tool rubber-band update ---------------------------------------
    if (m_seabed_tool == 2 && (ev->buttons() & Qt::LeftButton) && m_box_anchor_row >= 0) {
        m_cursor_x = ev->pos().x();
        m_cursor_y = ev->pos().y();
        update();
        return;
    }

    // -- Eraser drag: clear seabed for every row swept by the cursor -------
    if (m_seabed_tool == 3 && (ev->buttons() & Qt::LeftButton)) {
        m_cursor_x = ev->pos().x();
        m_cursor_y = ev->pos().y();
        const int rh  = m_renderer.layout().row_height;
        const int row = m_scroll.scrollRow() + (ev->pos().y() - kWfScaleBarH) / rh;
        if (row >= 0 && row < rowCount()) {
            if (m_rows[row].timestamp_us != 0)
                m_manual_seabed.erase(m_rows[row].timestamp_us);
            m_rows[row].seabed = {};
            m_dirty = true;
            update();
        }
        return;
    }

    // -- Normal cursor tracking --------------------------------------------
    const int new_cx = ev->pos().x();
    const int new_cy = ev->pos().y();
    const bool cursor_moved = (new_cx != m_cursor_x || new_cy != m_cursor_y);
    m_cursor_x = new_cx;
    m_cursor_y = new_cy;

    const int rh  = m_renderer.layout().row_height;
    const int row = m_scroll.scrollRow() + (m_cursor_y - kWfScaleBarH) / rh;

    core::SidescanChannel ch;
    float range_m = 0.f;
    if (m_renderer.xToRange(m_cursor_x, row, m_rows,
                             m_scroll.hZoom(), m_scroll.hPan(), ch, range_m)) {
        const float rp = (ch == core::SidescanChannel::Port)     ? range_m : 0.f;
        const float rs = (ch == core::SidescanChannel::Starboard) ? range_m : 0.f;
        double lat = 0.0, lon = 0.0;
        bool is_proj = false;
        if (row >= 0 && row < rowCount()) {
            lat     = m_rows[row].lat;
            lon     = m_rows[row].lon;
            is_proj = m_rows[row].is_projected;
        }
        emit cursorMoved(row, rp, rs, 0.f, 0.f, lat, lon, is_proj);
    }
    if (cursor_moved) update();
}

void WaterfallView::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) return;

    if (m_contact_tool != 0) {
        return;  // contact pick was already handled on press — don't fire pingClicked
    }
    if (m_feature_tool != 0) {
        return;  // feature draw handled on press — don't fire pingClicked
    }
    if (m_seabed.isDragging()) {
        m_seabed.endDrag();
        m_pen_last_row = -1;
        return;  // pen drag finished — don't fire pingClicked
    }
    if (m_seabed_tool == 3) {
        // Eraser released — fill cleared gaps with straight-line interpolation
        // so the seabed line is never broken, only reshaped.
        interpolateSeabedGaps();
        m_dirty = true;
        update();
        return;
    }
    if (m_seabed_tool == 2 && m_box_anchor_row >= 0) {
        // Box released — auto-detect seabed within the selected rows + range window.
        if (rowCount() == 0) { m_box_anchor_row = -1; return; }
        const int rh      = m_renderer.layout().row_height;
        const int cur_row = qBound(0,
            m_scroll.scrollRow() + (ev->pos().y() - kWfScaleBarH) / rh,
            rowCount() - 1);

        core::SidescanChannel ch;
        float cur_range = 0.f;
        if (!m_renderer.xToRange(ev->pos().x(), cur_row, m_rows,
                                  m_scroll.hZoom(), m_scroll.hPan(), ch, cur_range)
            || cur_range <= 0.f) {
            cur_range = m_box_anchor_range;
        }

        const int   r0        = qMin(m_box_anchor_row, cur_row);
        const int   r1        = qMax(m_box_anchor_row, cur_row);
        const float range_min = std::min(m_box_anchor_range, cur_range);
        const float range_max = std::max(m_box_anchor_range, cur_range);

        detectSeabedInBox(r0, r1, range_min, range_max);

        m_box_anchor_row = -1;
        m_box_press_sx   = -1;
        m_box_press_sy   = -1;
        m_dirty          = true;
        update();
        return;
    }

    // No seabed tool active — report the click to the window.
    const int rh  = m_renderer.layout().row_height;
    const int row = m_scroll.scrollRow() + (ev->pos().y() - kWfScaleBarH) / rh;
    core::SidescanChannel ch;
    float range_m = 0.f;
    if (m_renderer.xToRange(ev->pos().x(), row, m_rows,
                             m_scroll.hZoom(), m_scroll.hPan(), ch, range_m)
        && row < rowCount())
        emit pingClicked(row, ch, range_m);
}

// -----------------------------------------------------------------------------
//  Wheel event
// -----------------------------------------------------------------------------

void WaterfallView::wheelEvent(QWheelEvent* ev)
{
    const int angle = ev->angleDelta().y();

    if (ev->modifiers() & Qt::ControlModifier) {
        // Horizontal zoom (Ctrl+scroll)
        const WfLayout& lay = m_renderer.layout();
        const int stbd_w    = lay.widget_w - lay.nadir_x;
        const int ns        = samplesPerPing();
        const float auto_z  = (stbd_w > 0 && ns > 0) ? float(stbd_w) / ns : 1.f;
        const float factor  = (angle > 0) ? 1.25f : 0.8f;
        if (m_scroll.zoomBy(factor, auto_z)) {
            m_dirty = true; update();
        }
        ev->accept();
        return;
    }

    if (ev->modifiers() & Qt::ShiftModifier) {
        // Horizontal pan (Shift+scroll)
        const int delta = -angle / 40;
        if (m_scroll.panBy(delta * 5, qMax(0, samplesPerPing() - 1))) {
            m_dirty = true; update();
        }
        ev->accept();
        return;
    }

    // Vertical scroll
    const int delta = -angle / 40;
    const auto res  = m_scroll.scrollBy(delta, rowCount(), m_renderer.maxVisible());
    if (res == WaterfallScrollController::ScrollResult::AtTop)
        emit scrollBeyondBounds(-1);
    else if (res == WaterfallScrollController::ScrollResult::AtBottom)
        emit scrollBeyondBounds(+1);
    else { m_dirty = true; update(); }
}

// -----------------------------------------------------------------------------
//  Resize / leave
// -----------------------------------------------------------------------------

void WaterfallView::resizeEvent(QResizeEvent* e)
{
    QOpenGLWidget::resizeEvent(e);  // triggers FBO resize and resizeGL()
    updateLayout();
    m_scroll.clampToMax(rowCount(), m_renderer.maxVisible());
}

void WaterfallView::leaveEvent(QEvent*)
{
    m_cursor_x = -1;
    m_cursor_y = -1;
    // Emit with zeroed range so WaterfallScrollSync clears the status bar
    // (and in turn emits cursorUpdated(0,...) so MainWindow also clears).
    emit cursorMoved(-1, 0.f, 0.f, 0.f, 0.f, 0.0, 0.0, false);
    update();
}

// -----------------------------------------------------------------------------
//  Feature draw — commit / cancel
// -----------------------------------------------------------------------------

void WaterfallView::mouseDoubleClickEvent(QMouseEvent* ev)
{
    if (m_feature_tool != 0 && ev->button() == Qt::LeftButton) {
        const bool   polygon = (m_feature_tool == 1);
        const size_t min_pts = polygon ? 3u : 2u;
        if (m_feature_pts.size() >= min_pts)
            emit featureDrawn(m_feature_pts, polygon, m_feature_proj);
        m_feature_pts.clear();
        m_feature_px.clear();
        m_dirty = true;
        update();
        ev->accept();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(ev);
}

void WaterfallView::keyPressEvent(QKeyEvent* ev)
{
    if (m_feature_tool != 0 && !m_feature_pts.empty()) {
        switch (ev->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter: {
            const bool   polygon = (m_feature_tool == 1);
            const size_t min_pts = polygon ? 3u : 2u;
            if (m_feature_pts.size() >= min_pts)
                emit featureDrawn(m_feature_pts, polygon, m_feature_proj);
            m_feature_pts.clear();
            m_feature_px.clear();
            m_dirty = true; update();
            return;
        }
        case Qt::Key_Escape:
            m_feature_pts.clear();
            m_feature_px.clear();
            m_dirty = true; update();
            return;
        case Qt::Key_Backspace:
            m_feature_pts.pop_back();
            m_feature_px.pop_back();
            m_dirty = true; update();
            return;
        default:
            break;
        }
    }
    QOpenGLWidget::keyPressEvent(ev);
}

void WaterfallView::contextMenuEvent(QContextMenuEvent* event)
{
    // While drawing a feature, right-click cancels the in-progress draft.
    if (m_feature_tool != 0 && !m_feature_pts.empty()) {
        m_feature_pts.clear();
        m_feature_px.clear();
        m_dirty = true;
        update();
        event->accept();
        return;
    }
    emit contextMenuRequested(event->globalPos());
}

} // namespace dolphin::ui
