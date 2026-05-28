#include "ui/shared/widgets/AnimatedToolButton.h"
#include "ui/shell/Theme.h"

#include <QCursor>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPropertyAnimation>

namespace dolphin::ui {

AnimatedToolButton::AnimatedToolButton(QWidget* parent)
    : QToolButton(parent)
{
    setAttribute(Qt::WA_Hover);

    m_icon_anim = new QPropertyAnimation(this, "iconSize", this);
    m_icon_anim->setEasingCurve(QEasingCurve::OutCubic);

    m_glow_anim = new QPropertyAnimation(this, "glowAlpha", this);
    m_glow_anim->setEasingCurve(QEasingCurve::OutCubic);
}

// ── Property setter ───────────────────────────────────────────────────────────

void AnimatedToolButton::setGlowAlpha(qreal v)
{
    m_glow_alpha = v;
    update();  // trigger repaint at each animation step
}

// ── Events ────────────────────────────────────────────────────────────────────

void AnimatedToolButton::enterEvent(QEnterEvent* e)
{
    QToolButton::enterEvent(e);
    if (!m_base_size.isValid()) m_base_size = iconSize();
    animateIconTo(QSize(m_base_size.width() + 4, m_base_size.height() + 4), 160);
    animateGlowTo(1.0, 160);
}

void AnimatedToolButton::leaveEvent(QEvent* e)
{
    QToolButton::leaveEvent(e);
    if (!m_base_size.isValid()) return;
    animateIconTo(m_base_size, 220);
    animateGlowTo(0.0, 220);
}

void AnimatedToolButton::mousePressEvent(QMouseEvent* e)
{
    QToolButton::mousePressEvent(e);
    if (!m_base_size.isValid()) m_base_size = iconSize();
    animateIconTo(QSize(m_base_size.width() - 3, m_base_size.height() - 3), 80);
    animateGlowTo(0.4, 80);
}

void AnimatedToolButton::mouseReleaseEvent(QMouseEvent* e)
{
    QToolButton::mouseReleaseEvent(e);
    if (!m_base_size.isValid()) return;
    const bool hovering = rect().contains(mapFromGlobal(QCursor::pos()));
    animateIconTo(hovering
        ? QSize(m_base_size.width() + 4, m_base_size.height() + 4)
        : m_base_size, 130);
    animateGlowTo(hovering ? 1.0 : 0.0, 130);
}

// ── Custom painting — soft accent glow drawn on top of the normal button ─────

void AnimatedToolButton::paintEvent(QPaintEvent* e)
{
    QToolButton::paintEvent(e);

    if (m_glow_alpha < 0.01) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Accent-coloured rounded rect, very low alpha — feels like a backlight glow.
    QColor glow(Theme::kAccent);           // #0a84ff
    glow.setAlphaF(m_glow_alpha * 0.14);
    p.setBrush(glow);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect().adjusted(3, 3, -3, -3), 5, 5);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void AnimatedToolButton::animateIconTo(QSize target, int ms)
{
    m_icon_anim->stop();
    m_icon_anim->setStartValue(iconSize());
    m_icon_anim->setEndValue(target);
    m_icon_anim->setDuration(ms);
    m_icon_anim->start();
}

void AnimatedToolButton::animateGlowTo(qreal target, int ms)
{
    m_glow_anim->stop();
    m_glow_anim->setStartValue(m_glow_alpha);
    m_glow_anim->setEndValue(target);
    m_glow_anim->setDuration(ms);
    m_glow_anim->start();
}

} // namespace dolphin::ui
