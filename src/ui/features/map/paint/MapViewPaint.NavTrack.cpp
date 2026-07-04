// MapViewPaint.NavTrack.cpp — graticule grid and nav track line.
#include "ui/features/map/MapView.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shell/Theme.h"
#include "geo/GeoUtils.h"

#include <QLocale>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const QColor kNavTrackShadow(  0,   0,   0, 100);   // wide dark halo behind the track
const QColor kNavTrackLine  (255, 255, 255, 220);    // bright white GPS line

// Graticule label colors — derived from theme tokens so re-skinning is automatic.
const QColor kGratLabelFg = []{ QColor c(dolphin::ui::Theme::kTextMuted); c.setAlpha(210); return c; }();
const QColor kGratLabelBg = []{ QColor c(dolphin::ui::Theme::kBg);        c.setAlpha(170); return c; }();

// Graticule layout constants.
constexpr double kGratTargetPx    = 80.0;  // target minimum pixels between grid lines
constexpr double kGratEdgeMarginH = 30.0;  // suppress lon labels within this many px of left/right edge
constexpr double kGratEdgeMarginV = 10.0;  // suppress lat labels within this many px of top/bottom edge
constexpr int    kGratLabelPad    = 3;     // background rect padding around each label (px)
constexpr int    kGratLabelGap    = 4;     // minimum collision gap between adjacent labels (px)
static constexpr int kGratFontSizes[] = { 7, 8, 10 };  // Small / Normal / Large

// Snap x to nearest 1/2/5 × 10^n (always rounds up so lines stay ≥ target px apart).
static double snap125(double x)
{
    if (x <= 0.0) return 1e-9;
    const double mag  = std::pow(10.0, std::floor(std::log10(x)));
    const double norm = x / mag;
    return (norm <= 1.0) ? mag
         : (norm <= 2.0) ? 2.0 * mag
         : (norm <= 5.0) ? 5.0 * mag
         :                 10.0 * mag;
}

static QString fmtGeoLon(double lon, int dec)
{
    const char dir = (lon >= 0.0) ? 'E' : 'W';
    return QString::number(std::abs(lon), 'f', dec) + "\xC2\xB0" + QLatin1Char(dir);
}

static QString fmtGeoLat(double lat, int dec)
{
    const char dir = (lat >= 0.0) ? 'N' : 'S';
    return QString::number(std::abs(lat), 'f', dec) + "\xC2\xB0" + QLatin1Char(dir);
}

static QString fmtProj(double val, int dec)
{
    return QLocale().toString(val, 'f', dec) + " m";
}
} // namespace

namespace dolphin::ui {

void MapView::paintGraticule(QPainter& p) const
{
    const double sc      = baseScale() * m_zoom;
    const double cos_ref = m_is_projected
        ? 1.0
        : std::max(0.001, std::cos(m_ref_lat * (M_PI / 180.0)));

    // Two independent clean steps — lat and lon are snapped separately so both
    // always land on 1/2/5 × 10^n boundaries in their own coordinate units.
    // Longitude pixels-per-unit = sc * cos_ref (x-compressed at high latitudes).
    const double gridStepLat = snap125(kGratTargetPx / sc);
    const double gridStepLon = m_is_projected
        ? gridStepLat
        : std::min(snap125(kGratTargetPx / (sc * cos_ref)), 180.0);

    // Decimal precision per axis: enough digits to distinguish adjacent labels.
    const int decLat = std::max(0, (int)std::ceil(-std::log10(gridStepLat + 1e-15)));
    const int decLon = std::max(0, (int)std::ceil(-std::log10(gridStepLon + 1e-15)));

    // Viewport bounds — sample from mid-edge to avoid cos_ref cross-contamination.
    const double hw = width()  / 2.0;
    const double hh = height() / 2.0;
    const double lonMin = pixelToGeo({0.0,         hh}).x();
    const double lonMax = pixelToGeo({(double)width(), hh}).x();
    const double latMin = pixelToGeo({hw, (double)height()}).y();   // bottom
    const double latMax = pixelToGeo({hw, 0.0}).y();                // top
    const double lonCen = (lonMin + lonMax) * 0.5;                  // viewport centre lon for northing UTM

    // First clean-snapped line just inside each viewport edge.
    const double firstLon = std::ceil(lonMin / gridStepLon) * gridStepLon;
    const double firstLat = std::ceil(latMin / gridStepLat) * gridStepLat;

    // -- Grid lines ------------------------------------------------------------
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(m_grid_color, 1));

    // Longitude (vertical) lines — integer index avoids float accumulation.
    for (int i = 0; ; ++i) {
        const double lon = firstLon + i * gridStepLon;
        if (lon > lonMax + gridStepLon * 0.01) break;
        const double px = geoToPixel(lon, 0.0).x();
        if (px < 0.0 || px > width()) continue;
        p.drawLine(QPointF(px, 0.0), QPointF(px, height()));
    }

    // Latitude (horizontal) lines.
    for (int i = 0; ; ++i) {
        const double lat = firstLat + i * gridStepLat;
        if (lat > latMax + gridStepLat * 0.01) break;
        const double py = geoToPixel(0.0, lat).y();
        if (py < 0.0 || py > height()) continue;
        p.drawLine(QPointF(0.0, py), QPointF(width(), py));
    }

    // -- Coordinate labels -----------------------------------------------------
    // Resolve coordinate display mode:
    //   fmt 0 = Auto  → show degrees when geographic, metres when projected
    //   fmt 1 = Degrees  → always geographic (degrees); no-op when projected
    //   fmt 2 = E/N      → UTM easting/northing; falls back to metres when projected
    //   fmt 3 = Both     → degrees + UTM E/N stacked (geographic data only)
    const int fmt = std::clamp(m_grat_coord_fmt, 0, 3);
    const bool show_both = !m_is_projected && (fmt == 3);
    const bool utm_only  = !m_is_projected && (fmt == 2);

    p.setRenderHint(QPainter::Antialiasing, true);
    QFont font = p.font();  // inherit widget font family
    font.setPointSize(kGratFontSizes[std::clamp(m_grat_label_size, 0, 2)]);
    p.setFont(font);
    const QFontMetrics fm(font);
    const int lh      = fm.height();
    const int label_h = show_both ? 2 * lh : lh;  // stacked height for "Both" mode

    // Longitude labels — bottom edge, centred on each vertical line.
    {
        const int lblY = height() - label_h - kGratLabelPad * 2 - 2;
        int lastRight  = -99;

        for (int i = 0; ; ++i) {
            const double lon = firstLon + i * gridStepLon;
            if (lon > lonMax + gridStepLon * 0.01) break;
            const double px = geoToPixel(lon, 0.0).x();
            if (px < kGratEdgeMarginH || px > width() - kGratEdgeMarginH) continue;

            // Build primary label
            QString lbl1, lbl2;
            if (m_is_projected) {
                lbl1 = fmtProj(lon, decLon);
            } else if (utm_only) {
                double e, n; int z; bool nh;
                if (!dolphin::geo::latLonToUtm(m_ref_lat, lon, z, nh, e, n)) continue;
                lbl1 = fmtUtmE(e);
            } else {
                lbl1 = fmtGeoLon(lon, decLon);
                if (show_both) {
                    double e, n; int z; bool nh;
                    if (dolphin::geo::latLonToUtm(m_ref_lat, lon, z, nh, e, n))
                        lbl2 = fmtUtmE(e);
                }
            }

            const int lw1  = fm.horizontalAdvance(lbl1);
            const int lw2  = lbl2.isEmpty() ? 0 : fm.horizontalAdvance(lbl2);
            const int lw   = qMax(lw1, lw2);
            const int lx   = qRound(px) - lw / 2;
            const int bgL  = lx - kGratLabelPad;
            const int bgR  = lx + lw + kGratLabelPad;

            if (bgL <= lastRight + kGratLabelGap) continue;

            p.fillRect(QRect(bgL, lblY - kGratLabelPad,
                             lw + 2*kGratLabelPad, label_h + 2*kGratLabelPad),
                       kGratLabelBg);
            p.setPen(kGratLabelFg);
            p.drawText(QRect(lx, lblY,          lw, lh), Qt::AlignLeft | Qt::AlignTop, lbl1);
            if (!lbl2.isEmpty())
                p.drawText(QRect(lx, lblY + lh, lw, lh), Qt::AlignLeft | Qt::AlignTop, lbl2);
            lastRight = bgR;
        }
    }

    // Latitude labels — left edge, vertically centred on each horizontal line.
    // Supports: horizontal vs rotated 90° CCW; single vs stacked (Both mode).
    {
        int lastTop = height() + 99;

        for (int i = 0; ; ++i) {
            const double lat = firstLat + i * gridStepLat;
            if (lat > latMax + gridStepLat * 0.01) break;
            const double py = geoToPixel(0.0, lat).y();

            // Build primary and (optional) secondary label
            QString lbl1, lbl2;
            if (m_is_projected) {
                lbl1 = fmtProj(lat, decLat);
            } else if (utm_only) {
                double e, n; int z; bool nh;
                if (!dolphin::geo::latLonToUtm(lat, lonCen, z, nh, e, n)) continue;
                lbl1 = fmtUtmN(n);
            } else {
                lbl1 = fmtGeoLat(lat, decLat);
                if (show_both) {
                    double e, n; int z; bool nh;
                    if (dolphin::geo::latLonToUtm(lat, lonCen, z, nh, e, n))
                        lbl2 = fmtUtmN(n);
                }
            }

            const int lw1  = fm.horizontalAdvance(lbl1);
            const int lw2  = lbl2.isEmpty() ? 0 : fm.horizontalAdvance(lbl2);
            const int lw   = qMax(lw1, lw2);

            if (m_grat_label_rotated) {
                // Rotated 90° CCW: lw (the widest line) becomes the vertical extent.
                // In "Both" mode, rotate only the primary label; secondary is omitted
                // (stacking rotated labels would be too cluttered).
                const int halfLw = lw1 / 2;
                if (py < kGratEdgeMarginV + halfLw || py > height() - kGratEdgeMarginV - halfLw) continue;
                const int bgBot = qRound(py) + halfLw + 1;
                if (bgBot >= lastTop - kGratLabelGap) continue;

                p.save();
                p.translate(kGratLabelPad, qRound(py));
                p.rotate(-90.0);
                const QRect tr(-halfLw, 0, lw1, lh);
                p.fillRect(tr.adjusted(-kGratLabelPad, -1, kGratLabelPad, 1), kGratLabelBg);
                p.setPen(kGratLabelFg);
                p.drawText(tr, Qt::AlignLeft | Qt::AlignTop, lbl1);
                p.restore();
                lastTop = qRound(py) - halfLw - 1;
            } else {
                if (py < kGratEdgeMarginV || py > height() - kGratEdgeMarginV) continue;
                const int half_h = label_h / 2;
                const int bgBot  = qRound(py) + half_h + 1;
                if (bgBot >= lastTop - kGratLabelGap) continue;

                const int ly = qRound(py) - half_h;
                p.fillRect(QRect(1, ly - 1, lw + 2*kGratLabelPad, label_h + 2),
                           kGratLabelBg);
                p.setPen(kGratLabelFg);
                p.drawText(QRect(kGratLabelPad + 1, ly,       lw, lh),
                           Qt::AlignLeft | Qt::AlignTop, lbl1);
                if (!lbl2.isEmpty())
                    p.drawText(QRect(kGratLabelPad + 1, ly + lh, lw, lh),
                               Qt::AlignLeft | Qt::AlignTop, lbl2);
                lastTop = ly - 1;
            }
        }
    }
}

void MapView::paintNavTrack(QPainter& p) const
{
    if (m_nav_track.empty()) return;

    auto drawTrackPts = [&](const std::vector<QPointF>& pts, const QColor& col, float w_px) {
        p.setPen(QPen(col, w_px, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPointF from;
        bool ok = false;
        for (const QPointF& pt : pts) {
            if (std::isnan(pt.x())) { ok = false; continue; }
            QPointF to = geoToPixel(pt.x(), pt.y());
            if (ok) p.drawLine(from, to);
            from = to; ok = true;
        }
    };

    drawTrackPts(m_nav_track, kNavTrackShadow, 3.0f);
    drawTrackPts(m_nav_track, kNavTrackLine,   1.5f);
}

// NOTE: SBP (Profile-kind) layers used to get a per-segment depth-coloured
// "rainbow" ribbon here (paintProfileTracks). Removed on user direction — a
// nav line is a nav line: SBP tracks now render through the combined white
// track like every other modality (and the per-segment QPen churn it cost on
// every pan/zoom frame is gone). Depth stays in the SBP viewer / 3D curtains.

} // namespace dolphin::ui
