// SSSMetadataPlotWidget.cpp — custom plot widget (Line / Scatter / Histogram).
#include "ui/features/metadata/SSSMetadataWindow.h"
#include "ui/shell/Theme.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  PlotMetrics helpers
// ─────────────────────────────────────────────────────────────────────────────

QPointF SSSMetadataPlotWidget::PlotMetrics::toPixel(double x, double y) const
{
    double px = area.left()   + (x - xmin) / (xmax - xmin) * area.width();
    double py = area.bottom() - (y - ymin) / (ymax - ymin) * area.height();
    return {px, py};
}

double SSSMetadataPlotWidget::PlotMetrics::fromPixelX(double px) const
{
    return xmin + (px - area.left()) / area.width() * (xmax - xmin);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SSSMetadataPlotWidget
// ─────────────────────────────────────────────────────────────────────────────

SSSMetadataPlotWidget::SSSMetadataPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void SSSMetadataPlotWidget::setLineSeries(const QVector<SSSPlotSeries>& series)
{
    m_mode = SSSChartMode::Line;
    m_series = series;
    m_zoom = 1.0; m_pan_x = 0.0;
    update();
}

void SSSMetadataPlotWidget::setScatterData(const QVector<double>& xs,
                                            const QVector<double>& ys,
                                            const QString& x_label,
                                            const QString& y_label,
                                            QColor color)
{
    m_mode = SSSChartMode::Scatter;
    m_xs = xs; m_ys = ys;
    m_x_label = x_label; m_y_label = y_label;
    m_dot_color = color;
    m_zoom = 1.0; m_pan_x = 0.0;
    update();
}

void SSSMetadataPlotWidget::setHistogramData(const QVector<double>& values,
                                              int bins,
                                              const QString& field_label,
                                              QColor color)
{
    m_mode = SSSChartMode::Histogram;
    m_hist_values = values;
    m_hist_bins   = bins;
    m_hist_label  = field_label;
    m_dot_color   = color;
    m_zoom = 1.0; m_pan_x = 0.0;
    update();
}

void SSSMetadataPlotWidget::clear()
{
    m_series.clear();
    m_xs.clear(); m_ys.clear();
    m_hist_values.clear();
    m_zoom = 1.0; m_pan_x = 0.0;
    update();
}

// ── Pan/Zoom ─────────────────────────────────────────────────────────────────

void SSSMetadataPlotWidget::wheelEvent(QWheelEvent* ev)
{
    const double factor = ev->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    m_zoom = std::clamp(m_zoom * factor, 0.1, 50.0);
    update();
}

void SSSMetadataPlotWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        m_dragging   = true;
        m_drag_start = ev->position();
    }
}

void SSSMetadataPlotWidget::mouseMoveEvent(QMouseEvent* ev)
{
    if (!m_dragging) return;
    const double dx = ev->position().x() - m_drag_start.x();
    m_pan_x += dx / width() * (1.0 / m_zoom);
    m_drag_start = ev->position();
    update();
}

void SSSMetadataPlotWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) m_dragging = false;
}

// ── Paint helpers ─────────────────────────────────────────────────────────────

SSSMetadataPlotWidget::PlotMetrics
SSSMetadataPlotWidget::computeMetrics(const QRect& plot) const
{
    PlotMetrics m;
    m.area = plot;
    m.xmin = 0; m.xmax = 1; m.ymin = 0; m.ymax = 1;
    return m;
}

static std::pair<double,double> dataRange(const QVector<double>& v)
{
    double mn =  std::numeric_limits<double>::max();
    double mx = -std::numeric_limits<double>::max();
    for (double x : v) { if (!std::isfinite(x)) continue; mn = std::min(mn,x); mx = std::max(mx,x); }
    if (mn > mx) { mn = 0; mx = 1; }
    if (mn == mx) { mn -= 0.5; mx += 0.5; }
    return {mn, mx};
}

void SSSMetadataPlotWidget::paintBackground(QPainter& p, const QRect& plot) const
{
    p.fillRect(rect(), QColor(Theme::kBg));
    p.fillRect(plot, QColor(Theme::kBgPanel));
}

void SSSMetadataPlotWidget::paintGrid(QPainter& p, const QRect& plot, int nx, int ny) const
{
    p.setPen(QPen(QColor(Theme::kBorder), 1, Qt::DotLine));
    for (int i = 0; i <= nx; ++i) {
        int gx = plot.left() + i * plot.width() / nx;
        p.drawLine(gx, plot.top(), gx, plot.bottom());
    }
    for (int i = 0; i <= ny; ++i) {
        int gy = plot.top() + i * plot.height() / ny;
        p.drawLine(plot.left(), gy, plot.right(), gy);
    }
}

void SSSMetadataPlotWidget::paintAxes(QPainter& p, const QRect& plot) const
{
    p.setPen(QPen(QColor(Theme::kBorder), 1));
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
    p.drawLine(plot.bottomLeft(), plot.topLeft());
}

void SSSMetadataPlotWidget::paintXTicks(QPainter& p, const PlotMetrics& m, int nx) const
{
    p.setFont(QFont("Segoe UI", 7));
    p.setPen(QColor(Theme::kTextMuted));
    for (int i = 0; i <= nx; ++i) {
        double t   = double(i) / nx;
        double val = m.xmin + t * (m.xmax - m.xmin);
        int    gx  = m.area.left() + int(t * m.area.width());
        p.drawText(QRect(gx - 20, m.area.bottom() + 3, 40, 14),
                   Qt::AlignHCenter, QString::number(val, 'g', 4));
    }
}

void SSSMetadataPlotWidget::paintYTicks(QPainter& p, const PlotMetrics& m, int ny) const
{
    const int ml = m.area.left();
    p.setFont(QFont("Segoe UI", 7));
    p.setPen(QColor(Theme::kTextMuted));
    for (int i = 0; i <= ny; ++i) {
        double t   = double(i) / ny;
        double val = m.ymin + t * (m.ymax - m.ymin);
        int    gy  = m.area.top() + int((1.0 - t) * m.area.height());
        p.drawText(QRect(0, gy - 7, ml - 4, 14),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(val, 'g', 4));
    }
}

void SSSMetadataPlotWidget::paintEmptyHint(QPainter& p, const QRect& plot) const
{
    p.setPen(QColor(Theme::kTextDim));
    p.setFont(QFont("Segoe UI", 9));
    p.drawText(plot, Qt::AlignCenter,
               m_mode == SSSChartMode::Line
                   ? "Enable \"Show in plot\" for one or more fields"
                   : "No data to display");
}

void SSSMetadataPlotWidget::paintLegend(QPainter& p, const QRect& plot) const
{
    if (m_series.isEmpty()) return;
    p.setFont(QFont("Segoe UI", 7));
    int lx = plot.right() - 4;
    for (int si = 0; si < m_series.size(); ++si) {
        const auto& s = m_series[si];
        QFontMetrics fm(p.font());
        const int lw = fm.horizontalAdvance(s.label) + 4;
        const int ly = plot.top() + 6 + si * 14;
        p.setPen(s.color);
        p.drawText(QRect(lx - lw, ly - 7, lw, 14),
                   Qt::AlignRight | Qt::AlignVCenter, s.label);
        lx -= lw + 4;
    }
}

void SSSMetadataPlotWidget::paintLine(QPainter& p, const PlotMetrics& m) const
{
    const int n_vis = std::count_if(m_series.begin(), m_series.end(),
        [](const SSSPlotSeries& s){ return !s.values.isEmpty(); });
    if (n_vis == 0) return;

    for (const auto& series : m_series) {
        const int n = series.values.size();
        if (n == 0) continue;

        auto [vmin, vmax] = dataRange(series.values);
        PlotMetrics ms = m;
        ms.xmin = 0.0;
        ms.xmax = static_cast<double>(std::max(1, n - 1));
        ms.ymin = vmin; ms.ymax = vmax;

        QPainterPath path;
        bool first = true;
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(series.values[i])) { first = true; continue; }
            QPointF pt = ms.toPixel(double(i), series.values[i]);
            if (first) { path.moveTo(pt); first = false; }
            else         path.lineTo(pt);
        }
        QColor lc = series.color;
        lc.setAlpha(n_vis > 1 ? 170 : 220);
        p.setPen(QPen(lc, series.thickness));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        if (series.add_dots) {
            p.setBrush(series.color);
            p.setPen(Qt::NoPen);
            for (int i = 0; i < n; ++i)
                if (std::isfinite(series.values[i]))
                    p.drawEllipse(ms.toPixel(double(i), series.values[i]), 2.5, 2.5);
        }
    }
}

void SSSMetadataPlotWidget::paintScatter(QPainter& p, const PlotMetrics& m) const
{
    const int n = std::min(m_xs.size(), m_ys.size());
    if (n == 0) return;

    QColor dot = m_dot_color;
    dot.setAlpha(n > 5000 ? 80 : 160);

    p.setPen(QPen(dot.darker(120), 0.5));
    p.setBrush(dot);
    const double r = n > 10000 ? 1.5 : (n > 2000 ? 2.0 : 3.0);

    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(m_xs[i]) || !std::isfinite(m_ys[i])) continue;
        p.drawEllipse(m.toPixel(m_xs[i], m_ys[i]), r, r);
    }
}

void SSSMetadataPlotWidget::paintHistogram(QPainter& p, const PlotMetrics& m) const
{
    if (m_hist_values.isEmpty() || m_hist_bins < 1) return;

    auto [vmin, vmax] = dataRange(m_hist_values);
    const double step  = (vmax - vmin) / m_hist_bins;
    if (step <= 0) return;

    QVector<int> counts(m_hist_bins, 0);
    for (double v : m_hist_values) {
        if (!std::isfinite(v)) continue;
        int bi = static_cast<int>((v - vmin) / step);
        bi = std::clamp(bi, 0, m_hist_bins - 1);
        ++counts[bi];
    }

    const int max_count = *std::max_element(counts.begin(), counts.end());
    if (max_count == 0) return;

    const double bar_w = double(m.area.width()) / m_hist_bins;
    QColor fill = m_dot_color;
    fill.setAlpha(180);

    for (int bi = 0; bi < m_hist_bins; ++bi) {
        const double frac  = double(counts[bi]) / max_count;
        const int    bar_h = static_cast<int>(frac * m.area.height());
        const int    bx    = m.area.left() + static_cast<int>(bi * bar_w);
        const QRect  bar(bx, m.area.bottom() - bar_h,
                         std::max(1, static_cast<int>(bar_w) - 1), bar_h);
        p.fillRect(bar, fill);
        p.setPen(m_dot_color.lighter(130));
        p.drawRect(bar);
    }
}

void SSSMetadataPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int ml = 54, mr = 12, mt = 12, mb = 22;
    const QRect plot(ml, mt, width() - ml - mr, height() - mt - mb);

    paintBackground(p, plot);

    PlotMetrics m;
    m.area = plot;

    bool has_data = false;

    if (m_mode == SSSChartMode::Line) {
        for (const auto& s : m_series)
            if (!s.values.isEmpty()) { has_data = true; break; }
        if (!has_data) { paintEmptyHint(p, plot); return; }
        m.xmin = 0; m.xmax = 1; m.ymin = 0; m.ymax = 1;
    } else if (m_mode == SSSChartMode::Scatter) {
        has_data = !m_xs.isEmpty();
        if (!has_data) { paintEmptyHint(p, plot); return; }
        auto [x0, x1] = dataRange(m_xs);
        auto [y0, y1] = dataRange(m_ys);
        const double xpad = (x1 - x0) * 0.04, ypad = (y1 - y0) * 0.04;
        m.xmin = x0 - xpad; m.xmax = x1 + xpad;
        m.ymin = y0 - ypad; m.ymax = y1 + ypad;
    } else {
        has_data = !m_hist_values.isEmpty();
        if (!has_data) { paintEmptyHint(p, plot); return; }
        auto [v0, v1] = dataRange(m_hist_values);
        m.xmin = v0; m.xmax = v1; m.ymin = 0; m.ymax = 1;
    }

    paintGrid(p, plot, 5, 4);
    paintAxes(p, plot);
    paintYTicks(p, m, 4);

    if (m_mode == SSSChartMode::Line) {
        paintLine(p, m);
        paintLegend(p, plot);
        {
            int n = 0;
            for (const auto& s : m_series) n = std::max(n, static_cast<int>(s.values.size()));
            m.xmin = 0; m.xmax = std::max(1, n - 1);
        }
        paintXTicks(p, m, 5);
    } else if (m_mode == SSSChartMode::Scatter) {
        paintScatter(p, m);
        paintXTicks(p, m, 5);
        p.setFont(QFont("Segoe UI", 8));
        p.setPen(QColor(Theme::kTextSubtle));
        p.drawText(QRect(plot.left(), height() - 14, plot.width(), 14),
                   Qt::AlignHCenter, m_x_label);
        p.save();
        p.translate(10, plot.top() + plot.height() / 2);
        p.rotate(-90);
        p.drawText(QRect(-50, -8, 100, 16), Qt::AlignHCenter, m_y_label);
        p.restore();
    } else {
        paintHistogram(p, m);
        paintXTicks(p, m, 5);
        p.setFont(QFont("Segoe UI", 8));
        p.setPen(QColor(Theme::kTextSubtle));
        p.drawText(QRect(plot.left(), height() - 14, plot.width(), 14),
                   Qt::AlignHCenter, m_hist_label);
    }
}

} // namespace dolphin::ui
