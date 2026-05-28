#pragma once
#include <QSize>
#include <QToolButton>

class QPaintEvent;
class QPropertyAnimation;

namespace dolphin::ui {

// QToolButton with two independent animations:
//   1. Icon scale — icon grows on hover (+4 px) and shrinks on press (−3 px).
//   2. Glow overlay — a soft accent highlight fades in/out behind the icon.
// Both run simultaneously via separate QPropertyAnimations.
// Drop-in replacement for QToolButton in activity bar and right tool bar.
class AnimatedToolButton : public QToolButton {
    Q_OBJECT
    Q_PROPERTY(qreal glowAlpha READ glowAlpha WRITE setGlowAlpha)
public:
    explicit AnimatedToolButton(QWidget* parent = nullptr);

    qreal glowAlpha() const  { return m_glow_alpha; }
    void  setGlowAlpha(qreal v);

protected:
    void enterEvent(QEnterEvent* e)        override;
    void leaveEvent(QEvent* e)             override;
    void mousePressEvent(QMouseEvent* e)   override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e)        override;

private:
    void animateIconTo(QSize target, int ms);
    void animateGlowTo(qreal target, int ms);

    QPropertyAnimation* m_icon_anim  = nullptr;
    QPropertyAnimation* m_glow_anim  = nullptr;
    QSize               m_base_size;
    qreal               m_glow_alpha = 0.0;
};

} // namespace dolphin::ui
