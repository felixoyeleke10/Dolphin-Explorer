// WfToggleRow.cpp — animated toggle switch row

#include "ui/features/waterfall/components/WfToggleRow.h"
#include "ui/shell/Anim.h"
#include "ui/shell/Theme.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

namespace dolphin::ui {

// -- Layout --------------------------------------------------------------------
static constexpr int    kRowH      = 28;
static constexpr int    kLabelL    = 14;
static constexpr int    kSwitchR   = 10;   // right margin (matches WfValueRow kPillR)
static constexpr int    kSwitchW   = 36;
static constexpr int    kSwitchH   = 20;
static constexpr int    kThumbD    = 16;   // thumb diameter
static constexpr int    kThumbInset = 2;   // gap between thumb and track edge

// -- Colors --------------------------------------------------------------------
static QColor kLabelCol() { return Theme::textSubtleColor(); }  // mode-aware
static const QColor kAccent     { QLatin1String(Theme::kAccent) };       // #0a84ff
static QColor kTrackOff()
{
    return Theme::mode() == Theme::Mode::Light ? QColor(0, 0, 0, 30)
                                               : QColor(255, 255, 255, 38);
}
static const QColor kThumbShadow{   0,   0,   0,  40 };  // black 16% — rendering constant
static constexpr int kTrackHoverBoost = 18;  // alpha increment when hovered-off

// -----------------------------------------------------------------------------

WfToggleRow::WfToggleRow(const QString& label, bool checked, QWidget* parent)
    : QWidget(parent)
    , m_label(label)
    , m_checked(checked)
    , m_default_checked(checked)
    , m_anim_pos(checked ? 1.0 : 0.0)
{
    setFixedHeight(kRowH);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click to turn this option on or off."));

    m_animation = new QPropertyAnimation(this, "animPos", this);
    m_animation->setDuration(Anim::kFast);
    m_animation->setEasingCurve(Anim::kEaseToggle);
}

void WfToggleRow::setChecked(bool v)
{
    if (m_checked == v) return;
    m_checked  = v;
    m_animation->stop();
    m_anim_pos = v ? 1.0 : 0.0;
    update();
}

// -----------------------------------------------------------------------------
//  Paint
// -----------------------------------------------------------------------------

void WfToggleRow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // -- Track -------------------------------------------------------------
    const QRect track(width() - kSwitchR - kSwitchW,
                      (height() - kSwitchH) / 2,
                      kSwitchW, kSwitchH);

    // Interpolate track color off → on
    auto lerp = [](int a, int b, qreal t) -> int {
        return a + qRound((b - a) * t);
    };
    const QColor trackCol(
        lerp(kTrackOff().red(),   kAccent.red(),   m_anim_pos),
        lerp(kTrackOff().green(), kAccent.green(), m_anim_pos),
        lerp(kTrackOff().blue(),  kAccent.blue(),  m_anim_pos),
        lerp(kTrackOff().alpha(), kAccent.alpha(), m_anim_pos)
    );

    // Subtle hover brightening when off
    QColor bg = trackCol;
    if (m_hovered && m_anim_pos < 0.5)
        bg = QColor(kTrackOff().red(), kTrackOff().green(), kTrackOff().blue(),
                    qMin(trackCol.alpha() + kTrackHoverBoost, 255));

    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(track), kSwitchH / 2.0, kSwitchH / 2.0);

    // -- Thumb -------------------------------------------------------------
    const qreal travel = kSwitchW - kThumbD - kThumbInset * 2;
    const qreal thumbX = track.left() + kThumbInset + m_anim_pos * travel;
    const qreal thumbY = track.top()  + kThumbInset;

    // Subtle shadow
    p.setBrush(kThumbShadow);
    p.drawEllipse(QRectF(thumbX + 0.5, thumbY + 1.0, kThumbD, kThumbD));

    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(thumbX, thumbY, kThumbD, kThumbD));

    // -- Label -------------------------------------------------------------
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(kLabelCol());

    const int lw = track.left() - kLabelL - 6;
    p.drawText(QRect(kLabelL, 0, lw, height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(m_label, Qt::ElideRight, lw));
}

// -----------------------------------------------------------------------------
//  Input
// -----------------------------------------------------------------------------

void WfToggleRow::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    m_checked = !m_checked;
    emit toggled(m_checked);
    m_animation->stop();
    m_animation->setStartValue(m_anim_pos);
    m_animation->setEndValue(m_checked ? 1.0 : 0.0);
    m_animation->start();
}

void WfToggleRow::enterEvent(QEnterEvent*)
{
    m_hovered = true;
    update();
}

void WfToggleRow::leaveEvent(QEvent*)
{
    m_hovered = false;
    update();
}

} // namespace dolphin::ui
