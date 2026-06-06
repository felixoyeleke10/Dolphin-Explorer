// MapView3D.Overlays.cpp — QPainter HUD overlays: HUD badges, grid labels,
// scale bar, compass rose.  All GL draw calls stay in MapView3D.Paint.cpp.

#include "ui/features/map/MapView3D.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shell/Theme.h"
#include "geo/GeoUtils.h"

#include <QLocale>
#include <QMatrix4x4>
#include <QPainter>
#include <QPolygonF>
#include <QVector3D>

#include <algorithm>
#include <cmath>

namespace {

const QColor kHudBadgeBg   (  0,   0,   0, 140);
const QColor kGratLabelFg = []{
    QColor c(dolphin::ui::Theme::kTextMuted); c.setAlpha(210); return c; }();
const QColor kGratLabelBg = []{
    QColor c(dolphin::ui::Theme::kBg);        c.setAlpha(170); return c; }();

using dolphin::ui::Theme::kGratEdgeMarginH;
using dolphin::ui::Theme::kGratEdgeMarginV;
using dolphin::ui::Theme::kGratLabelPad;
using dolphin::ui::Theme::kGratLabelGap;

static constexpr int kGratFontSizes[] = { 7, 8, 10 };  // Small / Normal / Large

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

void MapView3D::drawHUD(QPainter& painter)
{
    const QString info = QString("N %1°  ·  pitch %2°  ·  %3 m")
        .arg(qRound(m_camera.yaw))
        .arg(qRound(m_camera.pitch))
        .arg(qRound(m_camera.distance));

    QFont f = painter.font();
    f.setPointSizeF(f.pointSizeF() * 0.82);
    painter.setFont(f);
    const QFontMetrics fm(f);
    const QSize ts = fm.boundingRect(info).size() + QSize(12, 6);
    const QRect badge(12, height() - ts.height() - 10, ts.width(), ts.height());

    painter.fillRect(badge, kHudBadgeBg);
    painter.setPen(QColor(Theme::kIconStroke));
    painter.drawText(badge, Qt::AlignCenter, info);

    int nextX = badge.right() + 8;

    if (m_show_fps) {
        const QString fps_str = QString("FPS %1").arg(m_fps_avg < 1.f ? 0 : qRound(m_fps_avg));
        const QRect fpsbadge(nextX, badge.top(), fm.boundingRect(fps_str).width() + 12, badge.height());
        painter.fillRect(fpsbadge, kHudBadgeBg);
        painter.setPen(QColor(Theme::kIconStroke));
        painter.drawText(fpsbadge, Qt::AlignCenter, fps_str);
    }

    // -- Depth colormap legend -------------------------------------------------
    if (!m_terrain_layers.empty()) {
        const auto& T = m_terrain_layers.front();
        const QString leg = QString("Depth: %1 m → %2 m")
            .arg(qRound(-T.z_max)).arg(qRound(-T.z_min));
        painter.setPen(QColor(Theme::kIconStroke));
        painter.drawText(15, badge.top() - 2, leg);
    }

    // -- SBP curtain depth legend ----------------------------------------------
    if (!m_curtain_layers.empty()) {
        float z_range_max = 0.f;
        for (const auto& C : m_curtain_layers)
            z_range_max = std::max(z_range_max, C.z_range);
        const QString leg = QString("SBP depth: 0 m → %1 m").arg(qRound(z_range_max));
        painter.setPen(QColor(Theme::kIconStroke));
        painter.drawText(15, badge.top() - 2 - (m_terrain_layers.empty() ? 0 : 15), leg);
    }

    // -- Grid coordinate labels ------------------------------------------------
    if (m_show_grid && m_has_origin)
        drawGridLabels(painter);

    // -- Scale bar + compass rose ----------------------------------------------
    if (m_has_origin)
        drawScaleBar3D(painter);
    drawCompassRose(painter);

    // -- Empty state -----------------------------------------------------------
    if (m_layers.empty() && m_terrain_layers.empty()) {
        painter.setPen(QColor(Theme::kTextDim));
        QFont big = painter.font();
        big.setPointSizeF(big.pointSizeF() * 1.5);
        painter.setFont(big);
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("No data loaded\nImport a survey to view in 3D"));
    }
}

void MapView3D::drawGridLabels(QPainter& painter)
{
    if (!m_gl_ready) return;

    const float dist = m_camera.distance;
    const float r    = std::max(m_scene_radius * 2.0f, dist * 4.0f);
    float step = std::pow(10.f, std::floor(std::log10(std::max(r / 4.f, 1.f))));
    while (r / step > 12.f) step *= 2.f;
    while (r / step < 4.f)  step /= 2.f;
    step = std::max(step, 0.1f);

    const QMatrix4x4 mvp = m_camera.mvp();

    auto project = [&](float lx, float ly, float& sx, float& sy) -> bool {
        const QVector4D clip = mvp.map(QVector4D(lx, ly, 0.f, 1.f));
        if (clip.w() < 0.001f) return false;
        sx = (clip.x() / clip.w() + 1.f) * 0.5f * width();
        sy = (1.f - clip.y() / clip.w()) * 0.5f * height();
        return true;
    };

    const double cosLat = m_is_projected
        ? 1.0
        : std::max(0.001, std::cos(m_origin_y * (M_PI / 180.0)));
    auto toWorldX = [&](float lx) -> double {
        return m_is_projected ? m_origin_x + lx
                              : m_origin_x + lx / (cosLat * 111320.0);
    };
    auto toWorldY = [&](float ly) -> double {
        return m_is_projected ? m_origin_y + ly
                              : m_origin_y + ly / 111320.0;
    };

    const double stepInUnits = m_is_projected ? (double)step : (double)step / 111320.0;
    const int dec = std::max(0, (int)std::ceil(-std::log10(stepInUnits + 1e-15)));

    const float tx = m_camera.target.x();
    const float ty = m_camera.target.y();
    const int   nLines = static_cast<int>(r / step) + 2;
    const float ox = std::floor(tx / step) * step;
    const float oy = std::floor(ty / step) * step;

    QFont font = painter.font();
    font.setPointSize(kGratFontSizes[std::clamp(m_grat_label_size, 0, 2)]);
    painter.setFont(font);
    const QFontMetrics fm(font);
    const int lh = fm.height();

    const int fmt = std::clamp(m_grat_coord_fmt, 0, 3);
    const bool show_both = !m_is_projected && (fmt == 3);
    const bool utm_only  = !m_is_projected && (fmt == 2);
    const int label_h = show_both ? 2 * lh : lh;

    // -- Easting labels (bottom edge) ------------------------------------------
    struct LabelCandidate { float sx; QString text; QString text2; };
    std::vector<LabelCandidate> eastCands;
    eastCands.reserve(nLines * 2 + 4);

    for (int i = -nLines; i <= nLines; ++i) {
        const float lx = ox + i * step;
        float sx, sy;
        if (!project(lx, ty, sx, sy)) continue;
        if (sx < kGratEdgeMarginH || sx > width() - kGratEdgeMarginH) continue;

        QString lbl1, lbl2;
        if (m_is_projected) {
            lbl1 = fmtProj(toWorldX(lx), dec);
        } else if (utm_only) {
            double e, n; int z; bool nh;
            if (!dolphin::geo::latLonToUtm(m_origin_y, toWorldX(lx), z, nh, e, n)) continue;
            lbl1 = fmtUtmE(e);
        } else {
            lbl1 = fmtGeoLon(toWorldX(lx), dec);
            if (show_both) {
                double e, n; int z; bool nh;
                if (dolphin::geo::latLonToUtm(m_origin_y, toWorldX(lx), z, nh, e, n))
                    lbl2 = fmtUtmE(e);
            }
        }
        eastCands.push_back({ sx, lbl1, lbl2 });
    }
    std::sort(eastCands.begin(), eastCands.end(),
              [](const LabelCandidate& a, const LabelCandidate& b){ return a.sx < b.sx; });

    {
        const int lblY = height() - label_h - kGratLabelPad * 2 - 2;
        int lastRight  = -99;
        for (const auto& c : eastCands) {
            const int lw1 = fm.horizontalAdvance(c.text);
            const int lw2 = c.text2.isEmpty() ? 0 : fm.horizontalAdvance(c.text2);
            const int lw  = qMax(lw1, lw2);
            const int lx  = qRound(c.sx) - lw / 2;
            const int bgL = lx - kGratLabelPad;
            const int bgR = lx + lw + kGratLabelPad;
            if (bgL <= lastRight + kGratLabelGap) continue;
            painter.fillRect(QRect(bgL, lblY - kGratLabelPad,
                                   lw + 2*kGratLabelPad, label_h + 2*kGratLabelPad),
                             kGratLabelBg);
            painter.setPen(kGratLabelFg);
            painter.drawText(QRect(lx, lblY,      lw, lh), Qt::AlignLeft | Qt::AlignTop, c.text);
            if (!c.text2.isEmpty())
                painter.drawText(QRect(lx, lblY + lh, lw, lh), Qt::AlignLeft | Qt::AlignTop, c.text2);
            lastRight = bgR;
        }
    }

    // -- Northing labels (left edge) -------------------------------------------
    std::vector<LabelCandidate> northCands;
    northCands.reserve(nLines * 2 + 4);

    for (int i = -nLines; i <= nLines; ++i) {
        const float ly = oy + i * step;
        float sx, sy;
        if (!project(tx, ly, sx, sy)) continue;
        if (sy < kGratEdgeMarginV || sy > height() - kGratEdgeMarginV) continue;

        QString lbl1, lbl2;
        if (m_is_projected) {
            lbl1 = fmtProj(toWorldY(ly), dec);
        } else if (utm_only) {
            double e, n; int z; bool nh;
            if (!dolphin::geo::latLonToUtm(toWorldY(ly), m_origin_x, z, nh, e, n)) continue;
            lbl1 = fmtUtmN(n);
        } else {
            lbl1 = fmtGeoLat(toWorldY(ly), dec);
            if (show_both) {
                double e, n; int z; bool nh;
                if (dolphin::geo::latLonToUtm(toWorldY(ly), m_origin_x, z, nh, e, n))
                    lbl2 = fmtUtmN(n);
            }
        }
        northCands.push_back({ sy, lbl1, lbl2 });
    }
    std::sort(northCands.begin(), northCands.end(),
              [](const LabelCandidate& a, const LabelCandidate& b){ return a.sx > b.sx; });

    {
        int lastTop = height() + 99;
        for (const auto& c : northCands) {
            const int lw1 = fm.horizontalAdvance(c.text);
            const int lw2 = c.text2.isEmpty() ? 0 : fm.horizontalAdvance(c.text2);
            const int lw  = qMax(lw1, lw2);
            if (m_grat_label_rotated) {
                // Rotated 90° CCW: lw1 becomes vertical extent. Secondary omitted (too cluttered).
                const int halfLw = lw1 / 2;
                const int bgBot = qRound(c.sx) + halfLw + 1;
                if (bgBot >= lastTop - kGratLabelGap) continue;
                painter.save();
                painter.translate(kGratLabelPad, qRound(c.sx));
                painter.rotate(-90.0);
                const QRect tr(-halfLw, 0, lw1, lh);
                painter.fillRect(tr.adjusted(-kGratLabelPad, -1, kGratLabelPad, 1), kGratLabelBg);
                painter.setPen(kGratLabelFg);
                painter.drawText(tr, Qt::AlignLeft | Qt::AlignTop, c.text);
                painter.restore();
                lastTop = qRound(c.sx) - halfLw - 1;
            } else {
                const int half_h = label_h / 2;
                const int bgBot  = qRound(c.sx) + half_h + 1;
                if (bgBot >= lastTop - kGratLabelGap) continue;
                const int ly = qRound(c.sx) - half_h;
                painter.fillRect(QRect(1, ly - 1, lw + 2*kGratLabelPad, label_h + 2), kGratLabelBg);
                painter.setPen(kGratLabelFg);
                painter.drawText(QRect(kGratLabelPad + 1, ly,      lw, lh),
                                 Qt::AlignLeft | Qt::AlignTop, c.text);
                if (!c.text2.isEmpty())
                    painter.drawText(QRect(kGratLabelPad + 1, ly + lh, lw, lh),
                                     Qt::AlignLeft | Qt::AlignTop, c.text2);
                lastTop = ly - 1;
            }
        }
    }
}

void MapView3D::drawScaleBar3D(QPainter& painter)
{
    const float dist = m_camera.distance;
    const float r    = std::max(m_scene_radius * 2.0f, dist * 4.0f);
    float step = std::pow(10.f, std::floor(std::log10(std::max(r / 4.f, 1.f))));
    while (r / step > 12.f) step *= 2.f;
    while (r / step < 4.f)  step /= 2.f;
    step = std::max(step, 0.1f);

    const QMatrix4x4 mvp = m_camera.mvp();
    auto projX = [&](float lx, float ly) -> float {
        const QVector4D clip = mvp.map(QVector4D(lx, ly, 0.f, 1.f));
        if (clip.w() < 0.001f) return -1.f;
        return (clip.x() / clip.w() + 1.f) * 0.5f * static_cast<float>(width());
    };

    const float tx = m_camera.target.x();
    const float ty = m_camera.target.y();
    const float barPx = std::abs(projX(tx + step * 0.5f, ty) - projX(tx - step * 0.5f, ty));

    if (barPx < 4.f || barPx > 400.f) return;

    const QString label = step >= 1000.f
        ? QString("%1 km").arg(double(step) / 1000.0, 0, 'g', 3)
        : QString("%1 m").arg(double(step), 0, 'g', 3);

    const int barX0 = (width()  - static_cast<int>(barPx)) / 2;
    const int barX1 = barX0 + static_cast<int>(barPx);
    const int barY  = height() - 14;
    const int txtY  = height() - 18;

    painter.setPen(QPen(QColor(0, 0, 0, Theme::kMapOverlayShadowAlpha), 3.0));
    painter.drawLine(barX0, barY + 1, barX1, barY + 1);
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawLine(barX0, barY, barX1, barY);
    painter.drawLine(barX0, barY - 3, barX0, barY + 3);
    painter.drawLine(barX1, barY - 3, barX1, barY + 3);

    QFont sf = painter.font();
    sf.setPointSize(Theme::kGratLabelFontPt);
    painter.setFont(sf);
    painter.setPen(QColor(0, 0, 0, Theme::kMapOverlayTextShadowAlpha));
    painter.drawText(barX0 - 1, txtY + 1, static_cast<int>(barPx) + 2, 14,
                     Qt::AlignHCenter | Qt::AlignTop, label);
    painter.setPen(Qt::white);
    painter.drawText(barX0, txtY, static_cast<int>(barPx) + 2, 14,
                     Qt::AlignHCenter | Qt::AlignTop, label);
}

void MapView3D::drawCompassRose(QPainter& painter)
{
    constexpr int kR  = 20;
    constexpr int kCX = 35;
    constexpr int kCY = 45;

    const int cx = width() - kCX;
    const int cy = kCY;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(cx, cy);
    painter.rotate(static_cast<double>(-m_camera.yaw));

    painter.setPen(QPen(QColor(255, 255, 255, 50), 1));
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawEllipse(QPoint(0, 0), kR, kR);

    painter.setPen(QPen(QColor(255, 255, 255, 80), 1));
    for (int a = 0; a < 360; a += 90) {
        const float ar = float(a) * float(M_PI) / 180.f;
        const float ix = std::sin(ar) * float(kR - 1);
        const float iy = -std::cos(ar) * float(kR - 1);
        const float ox = std::sin(ar) * float(kR - 5);
        const float oy = -std::cos(ar) * float(kR - 5);
        painter.drawLine(QPointF(ix, iy), QPointF(ox, oy));
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawConvexPolygon(QPolygonF({ QPointF(0, -(kR - 4)),
                                          QPointF(-3.5f, 2), QPointF(3.5f, 2) }));

    painter.setBrush(QColor(180, 50, 50, 200));
    painter.drawConvexPolygon(QPolygonF({ QPointF(0, kR - 4),
                                          QPointF(-3.5f, -2), QPointF(3.5f, -2) }));

    painter.setBrush(QColor(210, 210, 210));
    painter.drawEllipse(QPoint(0, 0), 2, 2);

    // "N" label — drawn in un-rotated space so it always reads upright
    painter.restore();
    painter.save();
    painter.translate(cx, cy);
    QFont nf = painter.font();
    nf.setPointSize(7);
    nf.setBold(true);
    painter.setFont(nf);
    painter.setPen(QColor(210, 210, 210, 220));
    const int nw = QFontMetrics(nf).horizontalAdvance("N");
    painter.drawText(-nw / 2, -(kR + 2), "N");

    painter.restore();
}

} // namespace dolphin::ui
