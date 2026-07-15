#include "ui/features/waterfall/components/TvgCurveEditor.h"
#include "ui/shell/Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLineF>
#include <algorithm>
#include <cmath>

namespace dolphin::ui {
namespace {
constexpr float kNearHandleM = 50.f;
constexpr float kFarHandleM  = 200.f;
constexpr float kMaxGainDb   = 80.f;
}

TvgCurveEditor::TvgCurveEditor(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(170);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Drag either handle to shape TVG gain versus range. The same curve applies to PORT and STBD."));
}

void TvgCurveEditor::setCoefficients(float spreading, float absorption)
{
    m_spreading = std::clamp(spreading, 0.f, 40.f);
    m_absorption = std::clamp(absorption, 0.f, 2.f);
    update();
}

QRectF TvgCurveEditor::plotRect() const
{
    return QRectF(rect()).adjusted(34.0, 10.0, -10.0, -25.0);
}

float TvgCurveEditor::gainAt(float range_m) const
{
    const float r = std::max(1.f, range_m);
    return m_spreading * std::log10(r) + m_absorption * (r - 1.f);
}

QPointF TvgCurveEditor::handlePoint(float range_m) const
{
    const QRectF p = plotRect();
    const qreal x = p.left() + p.width() * range_m / kFarHandleM;
    const qreal y = p.bottom() - p.height()
        * std::clamp(gainAt(range_m), 0.f, kMaxGainDb) / kMaxGainDb;
    return {x, y};
}

void TvgCurveEditor::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF p = plotRect();

    QPen grid(Theme::borderColor());
    grid.setWidthF(1.0);
    painter.setPen(grid);
    for (int i = 0; i <= 4; ++i) {
        const qreal x = p.left() + p.width() * i / 4.0;
        const qreal y = p.bottom() - p.height() * i / 4.0;
        painter.drawLine(QPointF(x, p.top()), QPointF(x, p.bottom()));
        painter.drawLine(QPointF(p.left(), y), QPointF(p.right(), y));
    }

    painter.setPen(Theme::textMutedColor());
    const QFontMetrics fm(font());
    for (int i = 0; i <= 4; ++i) {
        const QString xText = QString::number(i * 50);
        const qreal x = p.left() + p.width() * i / 4.0;
        painter.drawText(QPointF(x - fm.horizontalAdvance(xText) / 2.0, p.bottom() + 16), xText);
        const QString yText = QString::number(i * 20);
        const qreal y = p.bottom() - p.height() * i / 4.0 + fm.ascent() / 2.0;
        painter.drawText(QPointF(p.left() - fm.horizontalAdvance(yText) - 5, y), yText);
    }
    painter.drawText(QPointF(p.right() - 8, p.bottom() + 16), tr("m"));
    painter.drawText(QPointF(3, p.top() + fm.ascent()), tr("dB"));

    QPainterPath curve;
    for (int px = 0; px <= static_cast<int>(p.width()); ++px) {
        const float range = kFarHandleM * px / std::max(1.0, p.width());
        const qreal y = p.bottom() - p.height()
            * std::clamp(gainAt(range), 0.f, kMaxGainDb) / kMaxGainDb;
        const QPointF point(p.left() + px, y);
        px == 0 ? curve.moveTo(point) : curve.lineTo(point);
    }
    QPen curvePen{QColor(Theme::kAccent)};
    curvePen.setWidthF(2.0);
    painter.setPen(curvePen);
    painter.drawPath(curve);

    painter.setPen(QPen(Theme::textPrimaryColor(), 1.5));
    painter.setBrush(QColor(Theme::kAccent));
    painter.drawEllipse(handlePoint(kNearHandleM), 5.0, 5.0);
    painter.drawEllipse(handlePoint(kFarHandleM), 5.0, 5.0);
    painter.setPen(Theme::textSubtleColor());
    painter.drawText(p.adjusted(4, 3, -4, -3), Qt::AlignTop | Qt::AlignRight,
                     tr("PORT / STBD"));
}

void TvgCurveEditor::mousePressEvent(QMouseEvent* event)
{
    const qreal nearDist = QLineF(event->position(), handlePoint(kNearHandleM)).length();
    const qreal farDist  = QLineF(event->position(), handlePoint(kFarHandleM)).length();
    m_drag_handle = nearDist <= farDist ? 1 : 2;
    updateHandle(event->position());
}

void TvgCurveEditor::mouseMoveEvent(QMouseEvent* event)
{
    if (m_drag_handle) updateHandle(event->position());
}

void TvgCurveEditor::mouseReleaseEvent(QMouseEvent*) { m_drag_handle = 0; }

void TvgCurveEditor::updateHandle(const QPointF& position)
{
    const QRectF p = plotRect();
    const float requestedGain = std::clamp(
        static_cast<float>((p.bottom() - position.y()) / p.height()) * kMaxGainDb,
        0.f, kMaxGainDb);
    const float g1 = m_drag_handle == 1 ? requestedGain : gainAt(kNearHandleM);
    const float g2 = m_drag_handle == 2 ? requestedGain : gainAt(kFarHandleM);

    const float l1 = std::log10(kNearHandleM);
    const float l2 = std::log10(kFarHandleM);
    const float d1 = kNearHandleM - 1.f;
    const float d2 = kFarHandleM - 1.f;
    const float determinant = l1 * d2 - l2 * d1;
    const float spreading = (g1 * d2 - g2 * d1) / determinant;
    const float absorption = (l1 * g2 - l2 * g1) / determinant;
    setCoefficients(spreading, absorption);
    emit coefficientsChanged(m_spreading, m_absorption);
}

} // namespace dolphin::ui
