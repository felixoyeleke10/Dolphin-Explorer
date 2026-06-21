#include "ui/shared/widgets/CollapsibleSection.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Anim.h"
#include "ui/shell/Theme.h"

#include <QApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QStyle>
#include <QVBoxLayout>

namespace {
// Draws a minimal padlock pixmap in the given colour.
QPixmap makeLockPixmap(const QColor& col, int sz)
{
    QPixmap px(sz, sz);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal stroke = qMax(1.2, sz * 0.13);

    // Shackle — top semicircle
    const qreal aW = sz * 0.52, aX = (sz - aW) / 2.0;
    const qreal aY = sz * 0.04, aH = sz * 0.50;
    p.setPen(QPen(col, stroke, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(aX, aY, aW, aH), 0, 180 * 16);

    // Body — filled rounded rect
    const qreal bX = sz * 0.10, bY = sz * 0.46;
    const qreal bW = sz * 0.80, bH = sz * 0.50;
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawRoundedRect(QRectF(bX, bY, bW, bH), sz * 0.10, sz * 0.10);

    return px;
}
} // namespace

namespace dolphin::ui {

static constexpr int kArrowW   = 14;   // expand/collapse arrow indicator width
static constexpr int kIconSz   = 14;   // section header icon size (px)
// kAnimMs removed — use Anim::kSection

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    m_layout = makeCompactLayout<QVBoxLayout>(this);

    // -- Clickable header bar --------------------------------------------------
    m_header = new QWidget(this);
    m_header->setObjectName("sectionHdr");
    m_header->setFixedHeight(Theme::kFormBtnH);
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setAttribute(Qt::WA_Hover, true);
    m_header->installEventFilter(this);

    auto* hl = new QHBoxLayout(m_header);
    hl->setContentsMargins(Theme::kSpacing3, 0, Theme::kSpacing3, 0);
    hl->setSpacing(5);

    m_arrow = new QLabel(QStringLiteral("▾"), m_header);
    m_arrow->setObjectName("sectionArrow");
    m_arrow->setFixedWidth(kArrowW);
    m_arrow->setAlignment(Qt::AlignCenter);

    auto* lbl = new QLabel(title, m_header);
    lbl->setObjectName("sectionTitle");

    hl->addWidget(m_arrow);
    hl->addWidget(lbl, 1);

    m_layout->addWidget(m_header);

    // -- Smooth expand/collapse animation on this widget's maximumHeight -------
    m_anim = new QPropertyAnimation(this, "maximumHeight", this);
    m_anim->setEasingCurve(Anim::kEaseInOut);
    m_anim->setDuration(Anim::kSection);

    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        if (m_expanded) {
            setMaximumHeight(QWIDGETSIZE_MAX);
        } else {
            if (m_content) m_content->setVisible(false);
            setMaximumHeight(m_header->height());
        }
    });
}

void CollapsibleSection::setIcon(const QString& icon_path)
{
    if (icon_path.isEmpty()) return;
    if (!m_icon) {
        m_icon = new QLabel(m_header);
        m_icon->setFixedSize(kIconSz, kIconSz);
        m_icon->setScaledContents(true);
        m_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* hl = qobject_cast<QHBoxLayout*>(m_header->layout());
        if (hl) hl->insertWidget(1, m_icon);  // between arrow and title
    }
    m_icon->setPixmap(QIcon(icon_path).pixmap(kIconSz, kIconSz));
}

void CollapsibleSection::setContent(QWidget* content)
{
    if (m_content) {
        m_layout->removeWidget(m_content);
        m_content->setParent(nullptr);
    }
    m_content = content;
    if (!m_content) return;

    m_content->setParent(this);
    {
        auto sp = m_content->sizePolicy();
        if (sp.verticalPolicy() == QSizePolicy::Ignored)
            sp.setVerticalPolicy(QSizePolicy::Preferred);
        m_content->setSizePolicy(sp);
    }
    m_layout->addWidget(m_content);
    m_content->setVisible(m_expanded);
    if (!m_expanded)
        setMaximumHeight(m_header->minimumHeight());
}

void CollapsibleSection::animateToExpanded(bool expanding)
{
    const int headerH = m_header->height();

    if (expanding) {
        // Show content first so its sizeHint is valid, then constrain and animate.
        if (m_content) m_content->setVisible(true);
        const int contentH = m_content ? m_content->sizeHint().height() : 0;
        const int target   = headerH + contentH;
        setMaximumHeight(headerH);  // start constrained

        m_anim->stop();
        m_anim->setStartValue(headerH);
        m_anim->setEndValue(target > headerH ? target : headerH + 1);
        m_anim->start();
    } else {
        const int from = height();
        m_anim->stop();
        m_anim->setStartValue(from);
        m_anim->setEndValue(headerH);
        m_anim->start();
    }
}

void CollapsibleSection::setExpanded(bool expanded)
{
    if (m_expanded == expanded) return;
    m_expanded = expanded;

    if (m_applicable)
        m_arrow->setText(expanded ? QStringLiteral("▾") : QStringLiteral("▸"));

    if (!m_applicable) {
        // Locked sections: instant toggle of hint visibility.
        if (m_hint) m_hint->setVisible(expanded);
        setMaximumHeight(expanded ? QWIDGETSIZE_MAX : m_header->minimumHeight());
        emit expandedChanged(expanded);
        return;
    }

    animateToExpanded(expanded);
    emit expandedChanged(expanded);
}

void CollapsibleSection::toggle() { setExpanded(!m_expanded); }

void CollapsibleSection::setApplicable(bool applicable, const QString& hint)
{
    if (m_applicable == applicable) {
        if (!applicable && m_hint && !hint.isEmpty())
            m_hint->setText(hint);
        return;
    }
    m_applicable = applicable;

    m_header->setProperty("locked", !applicable);
    m_header->style()->unpolish(m_header);
    m_header->style()->polish(m_header);

    if (!applicable) {
        m_arrow->setPixmap(makeLockPixmap(QColor(Theme::kTextDisabled), 11));
        if (m_content) m_content->setVisible(false);
        setMaximumHeight(QWIDGETSIZE_MAX);

        if (!m_hint) {
            m_hint = new QLabel(this);
            m_hint->setObjectName("sectionHint");
            m_hint->setWordWrap(true);
            m_hint->setContentsMargins(22, Theme::kSpacing2, 10, Theme::kSpacing3);
            m_layout->addWidget(m_hint);
        }
        m_hint->setText(hint.isEmpty()
            ? QStringLiteral("Not available for this layer type.")
            : hint);
        m_hint->setVisible(true);
    } else {
        m_arrow->setText(m_expanded ? QStringLiteral("▾") : QStringLiteral("▸"));
        if (m_hint) m_hint->setVisible(false);
        if (m_content && m_expanded) m_content->setVisible(true);
        if (!m_expanded) setMaximumHeight(m_header->minimumHeight());
    }
}

void CollapsibleSection::setBadge(const QString& text)
{
    if (text.isEmpty()) return;
    if (!m_badge) {
        m_badge = new QLabel(m_header);
        m_badge->setObjectName("sectionBadge");
        m_badge->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* hl = qobject_cast<QHBoxLayout*>(m_header->layout());
        if (hl) hl->addWidget(m_badge);
    }
    m_badge->setText(text);
}

void CollapsibleSection::setReorderable(bool on)
{
    m_reorderable = on;
    // Open-hand hint that the header can be grabbed; click still toggles.
    if (m_header) m_header->setCursor(on ? Qt::OpenHandCursor : Qt::PointingHandCursor);
}

bool CollapsibleSection::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj != m_header) return QWidget::eventFilter(obj, ev);

    switch (ev->type()) {
    case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton) {
            m_press_pos  = me->globalPosition().toPoint();
            m_drag_armed = m_reorderable && m_applicable;
            m_dragging   = false;
        }
        return false;  // let press through (hover/press styling)
    }
    case QEvent::MouseMove: {
        if (!m_drag_armed) return false;
        auto* me = static_cast<QMouseEvent*>(ev);
        if (!(me->buttons() & Qt::LeftButton)) return false;
        const QPoint g = me->globalPosition().toPoint();
        if (!m_dragging &&
            (g - m_press_pos).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragging = true;
            m_header->setCursor(Qt::ClosedHandCursor);
            emit reorderStarted();
        }
        if (m_dragging) { emit reorderMoved(g); return true; }
        return false;
    }
    case QEvent::MouseButtonRelease: {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() != Qt::LeftButton) return false;
        m_drag_armed = false;
        if (m_dragging) {
            m_dragging = false;
            if (m_reorderable) m_header->setCursor(Qt::OpenHandCursor);
            emit reorderFinished();
            return true;
        }
        toggle();   // a plain click (no drag) toggles expand/collapse
        return true;
    }
    default:
        break;
    }
    return QWidget::eventFilter(obj, ev);
}

} // namespace dolphin::ui
