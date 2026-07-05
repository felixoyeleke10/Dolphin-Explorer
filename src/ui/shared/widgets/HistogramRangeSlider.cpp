#include "ui/shared/widgets/HistogramRangeSlider.h"
#include "ui/shell/Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

HistogramRangeSlider::HistogramRangeSlider(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("histogramRange");
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setFixedHeight(64);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize HistogramRangeSlider::sizeHint() const { return {180, 64}; }

void HistogramRangeSlider::setHistogram(std::vector<float> bins)
{
    m_bins = std::move(bins);
    // sqrt-compress: sonar amplitude distributions pile up near zero, so a
    // linear scale collapses everything into one bar. sqrt keeps the tail
    // readable without a full log's noise floor.
    m_bin_max = 0.f;
    for (float& b : m_bins) { b = std::sqrt(std::max(0.f, b)); m_bin_max = std::max(m_bin_max, b); }
    if (m_bin_max <= 0.f) m_bin_max = 1.f;
    update();
}

void HistogramRangeSlider::setRange(double low, double high)
{
    low  = std::clamp(low,  0.0, 1.0);
    high = std::clamp(high, 0.0, 1.0);
    if (high < low + kMinGap) high = std::min(1.0, low + kMinGap);
    m_low = low; m_high = high;
    update();
}

double HistogramRangeSlider::xToPos(int x) const
{
    const int w = width() - 2 * kPad;
    if (w <= 0) return 0.0;
    return std::clamp(static_cast<double>(x - kPad) / w, 0.0, 1.0);
}

int HistogramRangeSlider::posToX(double pos) const
{
    return kPad + static_cast<int>(std::lround(pos * (width() - 2 * kPad)));
}

HistogramRangeSlider::Handle HistogramRangeSlider::handleAt(int x) const
{
    const int lo = posToX(m_low), hi = posToX(m_high);
    const int tol = 7;
    // Prefer whichever handle is nearer when the two overlap.
    if (std::abs(x - lo) <= tol || std::abs(x - hi) <= tol)
        return (std::abs(x - lo) <= std::abs(x - hi)) ? Handle::Low : Handle::High;
    return Handle::None;
}

void HistogramRangeSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    Handle h = handleAt(static_cast<int>(e->position().x()));
    if (h == Handle::None) {
        // Click in the track jumps the nearer handle to the cursor.
        const double p = xToPos(static_cast<int>(e->position().x()));
        h = (std::abs(p - m_low) <= std::abs(p - m_high)) ? Handle::Low : Handle::High;
    }
    m_drag = h;
    mouseMoveEvent(e);
}

void HistogramRangeSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag == Handle::None) {
        setCursor(handleAt(static_cast<int>(e->position().x())) != Handle::None
                  ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    const double p = xToPos(static_cast<int>(e->position().x()));
    if (m_drag == Handle::Low)
        m_low = std::min(p, m_high - kMinGap);
    else
        m_high = std::max(p, m_low + kMinGap);
    m_low  = std::clamp(m_low,  0.0, 1.0);
    m_high = std::clamp(m_high, 0.0, 1.0);
    update();
    emit rangeChanged(m_low, m_high);
}

void HistogramRangeSlider::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_drag == Handle::None) return;
    m_drag = Handle::None;
    setCursor(handleAt(static_cast<int>(e->position().x())) != Handle::None
              ? Qt::SizeHorCursor : Qt::ArrowCursor);
    emit rangeCommitted(m_low, m_high);
}

void HistogramRangeSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF area(kPad, 2, width() - 2 * kPad, height() - 4);
    const int loX = posToX(m_low), hiX = posToX(m_high);

    // Track background.
    QColor bg = Theme::bgColor();
    bg = bg.darker(115);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(0, 0, width(), height()), 4, 4);

    if (m_bins.empty()) {
        p.setPen(Theme::textMutedColor());
        p.drawText(rect(), Qt::AlignCenter, tr("Select a sidescan line"));
        return;
    }

    // Histogram bars: dim outside the selected [low,high] window, accent-tinted
    // inside — so the handles read as the kept amplitude range.
    const int n = static_cast<int>(m_bins.size());
    const double bw = area.width() / n;
    const QColor accent(Theme::kAccent);
    const QColor inBar  = accent.lighter(115);
    const QColor outBar = Theme::textMutedColor();
    for (int i = 0; i < n; ++i) {
        const double x  = area.left() + i * bw;
        const double h  = (m_bins[i] / m_bin_max) * (area.height() - 2);
        const bool inRange = (x + bw * 0.5) >= loX && (x + bw * 0.5) <= hiX;
        p.setBrush(inRange ? inBar : outBar);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(x, area.bottom() - h, std::max(1.0, bw - 0.5), h));
    }

    // Selected window tint.
    QColor fill = accent; fill.setAlpha(30);
    p.setBrush(fill); p.setPen(Qt::NoPen);
    p.drawRect(QRectF(loX, area.top(), hiX - loX, area.height()));

    // Handles: full-height line + a grab tab at the bottom.
    auto drawHandle = [&](int x) {
        p.setPen(QPen(accent, 1.5));
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        p.setBrush(accent); p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(x - 3, area.bottom() - 6, 6, 8), 2, 2);
    };
    drawHandle(loX);
    drawHandle(hiX);
}

} // namespace dolphin::ui
