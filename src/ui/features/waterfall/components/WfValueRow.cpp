// WfValueRow.cpp — custom parameter row: arrows / drag / click-to-type / wheel-locked

#include "ui/features/waterfall/components/WfValueRow.h"
#include "ui/shell/Theme.h"

#include <QEnterEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>

namespace dolphin::ui {

// -- Layout (logical px) -------------------------------------------------------
static constexpr int    kRowH     = 28;   // widget fixed height
static constexpr int    kPillW    = WfValueRow::controlWidth(); // shared control column
static constexpr int    kPillH    = 22;   // pill height
static constexpr int    kPillR    = WfValueRow::controlRightMargin();
static constexpr int    kLabelL   = 14;   // label left margin
static constexpr double kPillRad  = 5.0;
static constexpr int    kArrowW   = 20;   // arrow column width inside pill
static constexpr int    kDragPx   = 3;    // drag dead-zone (px)
static constexpr int    kRepeat0  = 450;  // ms before auto-repeat starts
static constexpr int    kRepeatN  = 70;   // ms between repeat ticks

// -- Theme colours — mode-aware (custom-painted widget) -------------------------
// mono(): the white-alpha overlay convention of the dark theme flips to
// black-alpha on light surfaces (same rule the QSS token pass applies).
static QColor mono(int a_dark, int a_light)
{
    return Theme::mode() == Theme::Mode::Light ? QColor(0, 0, 0, a_light)
                                               : QColor(255, 255, 255, a_dark);
}
static QColor kLabelCol()        { return Theme::textSubtleColor(); }
static QColor kValueCol()        { return Theme::textSecondColor(); }
static const QColor kAccent         {  10,  132, 255        };
// Pill background states (accent tints work on both themes)
static const QColor kPillDragBg     {  10,  132, 255,  30   };  // accent tint while dragging
static const QColor kPillDragBorder {  10,  132, 255, 200   };  // accent border while dragging
static QColor kPillHoverBg()     { return mono(18, 14); }
static QColor kPillHoverBorder() { return mono(30, 42); }
static QColor kPillDefaultBg()   { return mono(11,  8); }
static QColor kPillDefaultBdr()  { return mono(18, 28); }
// Arrow area
static const QColor kArrowPressBg   {  10,  132, 255,  55   };  // pressed tint
static QColor kArrowHoverBg()    { return mono(22, 16); }
static const QColor kArrowPressCol  {  10,  132, 255, 255   };  // triangle when pressed
static QColor kArrowHoverCol()   { return mono(210, 200); }
static QColor kArrowOnPillCol()  { return mono(110, 150); }
static QColor kArrowOffCol()     { return mono(65, 100); }
// Pill/arrow divider line
static QColor kSepOnPill()       { return mono(18, 26); }
static QColor kSepOff()          { return mono(10, 16); }

// -----------------------------------------------------------------------------
//  Construction
// -----------------------------------------------------------------------------

WfValueRow::WfValueRow(const QString& label,
                        double lo, double hi, double value,
                        double step, int decimals,
                        const QString& suffix,
                        QWidget* parent)
    : QWidget(parent)
    , m_label(label)
    , m_lo(lo), m_hi(hi), m_step(step)
    , m_value(qBound(lo, value, hi))
    , m_default(qBound(lo, value, hi))
    , m_decimals(decimals)
    , m_suffix(suffix)
{
    setFixedHeight(kRowH);
    setMouseTracking(true);
    setToolTip(tr("Click the value to type a number.\n"
                  "Drag up/down to adjust. Use the arrow area for single steps.\n"
                  "Mouse wheel is ignored to prevent accidental changes."));

    m_repeat_timer = new QTimer(this);
    m_repeat_timer->setSingleShot(true);
    connect(m_repeat_timer, &QTimer::timeout, this, &WfValueRow::onRepeatTick);
}

// -----------------------------------------------------------------------------
//  Public API
// -----------------------------------------------------------------------------

int  WfValueRow::intValue()    const { return qRound(m_value); }
bool WfValueRow::isAtDefault() const { return qFuzzyCompare(m_value + 1.0, m_default + 1.0); }

void WfValueRow::setValue(double v)
{
    const double c = clamp(v);
    if (qFuzzyCompare(c + 1.0, m_value + 1.0)) return;
    m_value = c;
    update();
}

void WfValueRow::setSpecialValueText(double trigger, const QString& text)
{
    m_has_special  = true;
    m_special_trig = trigger;
    m_special_text = text;
}

// -----------------------------------------------------------------------------
//  Geometry
// -----------------------------------------------------------------------------

QRect WfValueRow::pillRect() const
{
    return { width() - kPillR - kPillW, (height() - kPillH) / 2, kPillW, kPillH };
}

QRect WfValueRow::arrowAreaRect() const
{
    const QRect p = pillRect();
    return { p.right() - kArrowW, p.top(), kArrowW, p.height() };
}

QRect WfValueRow::upArrowRect() const
{
    const QRect a = arrowAreaRect();
    return { a.left(), a.top(), a.width(), a.height() / 2 };
}

QRect WfValueRow::downArrowRect() const
{
    const QRect a = arrowAreaRect();
    const int   h = a.height() / 2;
    return { a.left(), a.top() + h, a.width(), a.height() - h };
}

QRect WfValueRow::valueAreaRect() const
{
    const QRect p = pillRect();
    return { p.left(), p.top(), p.width() - kArrowW, p.height() };
}

WfValueRow::Zone WfValueRow::zoneAt(const QPoint& pos) const
{
    if (upArrowRect().contains(pos))   return Zone::UpArrow;
    if (downArrowRect().contains(pos)) return Zone::DownArrow;
    if (valueAreaRect().contains(pos)) return Zone::Value;
    return Zone::None;
}

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------

double WfValueRow::clamp(double v) const { return qBound(m_lo, v, m_hi); }

QString WfValueRow::formatValue() const
{
    if (m_has_special && qFuzzyCompare(m_value + 1.0, m_special_trig + 1.0))
        return m_special_text;
    if (m_decimals == 0)
        return QString::number(qRound(m_value)) + m_suffix;
    return QString::number(m_value, 'f', m_decimals) + m_suffix;
}

void WfValueRow::step(int dir)
{
    const double n = clamp(m_value + dir * m_step);
    if (qFuzzyCompare(n + 1.0, m_value + 1.0)) return;
    m_value = n;
    emit valueChanged(m_value);
    update();
}

void WfValueRow::onRepeatTick()
{
    if (m_press_zone == Zone::UpArrow)        step(+1);
    else if (m_press_zone == Zone::DownArrow) step(-1);
    else return;
    m_repeat_timer->start(kRepeatN);
}

// -----------------------------------------------------------------------------
//  Painting
// -----------------------------------------------------------------------------

void WfValueRow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect pill    = pillRect();
    const QRect valArea = valueAreaRect();
    const QRect arrArea = arrowAreaRect();
    const bool  editing = (m_edit && m_edit->isVisible());
    const bool  onPill  = (m_hover_zone != Zone::None);
    const bool  dragging = (m_press_zone == Zone::Value && m_dragging);

    // -- Pill background ---------------------------------------------------
    QColor bg, border;
    if (dragging) {
        bg     = kPillDragBg;
        border = kPillDragBorder;
    } else if (onPill) {
        bg     = kPillHoverBg();
        border = kPillHoverBorder();
    } else {
        bg     = kPillDefaultBg();
        border = kPillDefaultBdr();
    }
    p.setPen(QPen(border, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(pill).adjusted(0.5, 0.5, -0.5, -0.5), kPillRad, kPillRad);

    // -- Arrow area background (subtle highlight on hover / press) ---------
    {
        const bool upHov  = (m_hover_zone == Zone::UpArrow);
        const bool dnHov  = (m_hover_zone == Zone::DownArrow);
        const bool upPrs  = (m_press_zone == Zone::UpArrow);
        const bool dnPrs  = (m_press_zone == Zone::DownArrow);

        auto arrowBg = [&](bool hov, bool prs) -> QColor {
            if (prs) return kArrowPressBg;
            if (hov) return kArrowHoverBg();
            return Qt::transparent;
        };

        // Clip to pill so the highlight respects rounded corners.
        QPainterPath clip;
        clip.addRoundedRect(QRectF(pill), kPillRad, kPillRad);
        p.save();
        p.setClipPath(clip);
        p.setPen(Qt::NoPen);

        if (arrowBg(upHov, upPrs).alpha() > 0) {
            p.setBrush(arrowBg(upHov, upPrs));
            p.drawRect(upArrowRect());
        }
        if (arrowBg(dnHov, dnPrs).alpha() > 0) {
            p.setBrush(arrowBg(dnHov, dnPrs));
            p.drawRect(downArrowRect());
        }
        p.restore();
    }

    // -- Separator line between value and arrow column ---------------------
    {
        const int sx = arrArea.left();
        p.setPen(onPill ? kSepOnPill() : kSepOff());
        p.drawLine(sx, pill.top() + 4, sx, pill.bottom() - 4);
    }

    // -- Label -------------------------------------------------------------
    {
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);

        const int   lw = pill.left() - kLabelL - 6;
        const QRect lr(kLabelL, 0, lw, height());
        p.setPen(kLabelCol());
        p.drawText(lr, Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(m_label, Qt::ElideRight, lw));
    }

    // -- Value text --------------------------------------------------------
    if (!editing) {
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(kValueCol());
        p.drawText(valArea.adjusted(5, 0, -6, 0),
                   Qt::AlignRight | Qt::AlignVCenter,
                   formatValue());
    }

    // -- Arrow triangles ---------------------------------------------------
    {
        const bool upPrs = (m_press_zone == Zone::UpArrow);
        const bool dnPrs = (m_press_zone == Zone::DownArrow);
        const bool upHov = (m_hover_zone == Zone::UpArrow);
        const bool dnHov = (m_hover_zone == Zone::DownArrow);

        auto arrowCol = [&](bool hov, bool prs) -> QColor {
            if (prs) return kArrowPressCol;
            if (hov) return kArrowHoverCol();
            return onPill ? kArrowOnPillCol() : kArrowOffCol();
        };

        p.setPen(Qt::NoPen);
        const int cx = arrArea.center().x();

        // Up triangle
        const QRect ur = upArrowRect();
        const int   uy = ur.center().y() - 1;
        p.setBrush(arrowCol(upHov, upPrs));
        p.drawPolygon(QPolygon({ {cx, uy - 3}, {cx - 4, uy + 2}, {cx + 4, uy + 2} }));

        // Down triangle
        const QRect dr = downArrowRect();
        const int   dy = dr.center().y() + 1;
        p.setBrush(arrowCol(dnHov, dnPrs));
        p.drawPolygon(QPolygon({ {cx, dy + 3}, {cx - 4, dy - 2}, {cx + 4, dy - 2} }));
    }
}

// -----------------------------------------------------------------------------
//  Mouse events
// -----------------------------------------------------------------------------

void WfValueRow::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;

    m_press_zone  = zoneAt(e->pos());
    m_dragging    = false;

    if (m_press_zone == Zone::UpArrow) {
        step(+1);
        m_repeat_timer->start(kRepeat0);
        setCursor(Qt::ArrowCursor);
    } else if (m_press_zone == Zone::DownArrow) {
        step(-1);
        m_repeat_timer->start(kRepeat0);
        setCursor(Qt::ArrowCursor);
    } else if (m_press_zone == Zone::Value) {
        m_drag_origin_y = e->globalPosition().y();
        m_drag_origin_v = m_value;
        setCursor(Qt::SizeVerCursor);
    }
    update();
}

void WfValueRow::mouseMoveEvent(QMouseEvent* e)
{
    // Update hover zone for repaint.
    const Zone prev = m_hover_zone;
    m_hover_zone = (m_press_zone == Zone::None) ? zoneAt(e->pos()) : m_hover_zone;
    if (m_hover_zone != prev) update();

    // Update cursor.
    if (m_press_zone == Zone::None) {
        const Zone z = zoneAt(e->pos());
        setCursor(z == Zone::Value ? Qt::SizeVerCursor : Qt::ArrowCursor);
    }

    // Drag on value area.
    if (m_press_zone != Zone::Value) return;
    const double delta = m_drag_origin_y - e->globalPosition().y();
    if (!m_dragging && std::abs(delta) < kDragPx) return;

    m_dragging = true;
    const double factor  = (e->modifiers() & Qt::ShiftModifier) ? 0.1 : 1.0;
    const double new_val = clamp(m_drag_origin_v + delta * m_step * factor);
    if (!qFuzzyCompare(new_val + 1.0, m_value + 1.0)) {
        m_value = new_val;
        emit valueChanged(m_value);
        update();
    }
}

void WfValueRow::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;

    m_repeat_timer->stop();

    const Zone  released      = m_press_zone;
    const bool  was_dragging  = m_dragging;
    m_press_zone = Zone::None;
    m_dragging   = false;

    // Update hover zone based on current position.
    m_hover_zone = zoneAt(e->pos());
    setCursor(m_hover_zone == Zone::Value ? Qt::SizeVerCursor : Qt::ArrowCursor);
    update();

    // Clean click on value area (no drag) → open editor.
    if (released == Zone::Value && !was_dragging)
        startEdit();
}

void WfValueRow::wheelEvent(QWheelEvent* e)
{
    e->ignore();   // always blocked — can't change value by scrolling
}

void WfValueRow::leaveEvent(QEvent*)
{
    m_hover_zone = Zone::None;
    if (m_press_zone == Zone::None)
        setCursor(Qt::ArrowCursor);
    update();
}

// -----------------------------------------------------------------------------
//  Inline editor
// -----------------------------------------------------------------------------

void WfValueRow::startEdit()
{
    if (m_edit) return;

    m_edit = new QLineEdit(this);
    m_edit->setObjectName("wfValueEdit");
    m_edit->setGeometry(valueAreaRect());
    m_edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_edit->setText(m_decimals == 0
                    ? QString::number(qRound(m_value))
                    : QString::number(m_value, 'f', m_decimals));
    m_edit->selectAll();
    m_edit->installEventFilter(this);
    m_edit->show();
    m_edit->setFocus();
    connect(m_edit, &QLineEdit::editingFinished, this, &WfValueRow::commitEdit);
    update();
}

void WfValueRow::commitEdit()
{
    if (!m_edit) return;
    m_edit->disconnect();
    bool ok = false;
    const double v = m_edit->text().toDouble(&ok);
    cancelEdit();
    if (ok) {
        const double c = clamp(v);
        if (!qFuzzyCompare(c + 1.0, m_value + 1.0)) {
            m_value = c;
            emit valueChanged(m_value);
        }
    }
    update();
}

void WfValueRow::cancelEdit()
{
    if (!m_edit) return;
    m_edit->disconnect();
    m_edit->deleteLater();
    m_edit = nullptr;
    update();
}

bool WfValueRow::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_edit && ev->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
            cancelEdit();
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

} // namespace dolphin::ui
