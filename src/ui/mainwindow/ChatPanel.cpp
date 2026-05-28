#include "ui/mainwindow/ChatPanel.h"
#include "ui/shell/Theme.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kPanelW    = 380;
static constexpr int kPanelH    = 560;
static constexpr int kBubbleMaxW = 270;
static constexpr int kHeaderH   = 44;
static constexpr int kInputRowH = 52;
static constexpr int kSendBtnSz = 30;

ConversationPanel::ConversationPanel(QWidget* parent) : QFrame(parent)
{
    setObjectName("convPanel");
    setFixedSize(kPanelW, kPanelH);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header (drag handle) ──────────────────────────────────────────────────
    m_header = new QWidget(this);
    m_header->setObjectName("convHeader");
    m_header->setFixedHeight(kHeaderH);
    m_header->setCursor(Qt::OpenHandCursor);
    m_header->installEventFilter(this);

    auto* hl = new QHBoxLayout(m_header);
    hl->setContentsMargins(Theme::kSpacing5, 0, 10, 0);
    hl->setSpacing(Theme::kSpacing3);

    auto* dot = new QLabel(QStringLiteral("✦"), m_header);
    dot->setObjectName("convDot");

    auto* title = new QLabel(tr("Dolphin AI"), m_header);
    title->setObjectName("convTitle");

    auto* close_btn = new QPushButton(QStringLiteral("✕"), m_header);
    close_btn->setObjectName("convCloseBtn");
    close_btn->setFixedSize(Theme::kMediumBtnSz, Theme::kMediumBtnSz);
    close_btn->setFlat(true);
    close_btn->setCursor(Qt::ArrowCursor);  // restore pointer over the button
    connect(close_btn, &QPushButton::clicked, this, &QWidget::hide);

    hl->addWidget(dot);
    hl->addWidget(title, 1);
    hl->addWidget(close_btn);
    root->addWidget(m_header);

    // ── Separator ─────────────────────────────────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setObjectName("convSep");
    sep->setFixedHeight(Theme::kSepSz);
    root->addWidget(sep);

    // ── Message scroll area ───────────────────────────────────────────────────
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("convScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setFrameShape(QFrame::NoFrame);

    auto* msg_widget = new QWidget;
    msg_widget->setObjectName("convMessages");
    m_msg_layout = new QVBoxLayout(msg_widget);
    m_msg_layout->setContentsMargins(14, 14, 14, 14);
    m_msg_layout->setSpacing(10);
    m_msg_layout->addStretch(1);

    m_scroll->setWidget(msg_widget);
    root->addWidget(m_scroll, 1);

    // ── Input separator ───────────────────────────────────────────────────────
    auto* sep2 = new QFrame(this);
    sep2->setObjectName("convSep");
    sep2->setFixedHeight(Theme::kSepSz);
    root->addWidget(sep2);

    // ── Input row ─────────────────────────────────────────────────────────────
    auto* input_row = new QWidget(this);
    input_row->setObjectName("convInputRow");
    input_row->setFixedHeight(kInputRowH);
    auto* il = new QHBoxLayout(input_row);
    il->setContentsMargins(Theme::kSpacing4, 10, Theme::kSpacing4, 10);
    il->setSpacing(Theme::kSpacing3);

    m_input = new QLineEdit(input_row);
    m_input->setObjectName("convInput");
    m_input->setPlaceholderText(tr("Ask Dolphin anything…"));
    connect(m_input, &QLineEdit::returnPressed, this, &ConversationPanel::onSend);

    auto* send_btn = new QPushButton(QStringLiteral("↑"), input_row);
    send_btn->setObjectName("convSendBtn");
    send_btn->setFixedSize(kSendBtnSz, kSendBtnSz);
    send_btn->setFlat(true);
    connect(send_btn, &QPushButton::clicked, this, &ConversationPanel::onSend);

    il->addWidget(m_input, 1);
    il->addWidget(send_btn);
    root->addWidget(input_row);

    appendMessage(tr("Hi! Ask me to open a layer, run processing, change a setting, or anything else."), false);
}

void ConversationPanel::popupBelow(QWidget* anchor)
{
    if (!anchor || !parentWidget()) return;
    const QPoint br    = anchor->mapToGlobal(QPoint(anchor->width(), anchor->height() + 4));
    const QPoint local = parentWidget()->mapFromGlobal(QPoint(br.x() - kPanelW, br.y()));
    const int max_x    = parentWidget()->width()  - kPanelW - 4;
    const int max_y    = parentWidget()->height() - kPanelH - 4;
    move(qBound(4, local.x(), max_x), qBound(4, local.y(), max_y));
    raise();
    show();
    m_input->setFocus();
}

// ── Drag-to-move via header event filter ──────────────────────────────────────

bool ConversationPanel::eventFilter(QObject* watched, QEvent* ev)
{
    if (watched != m_header) return QFrame::eventFilter(watched, ev);

    switch (ev->type()) {
    case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton) {
            m_dragging          = true;
            m_drag_start_global = me->globalPosition().toPoint();
            m_drag_start_pos    = pos();
            m_header->setCursor(Qt::ClosedHandCursor);
            me->accept();
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        if (m_dragging) {
            auto* me   = static_cast<QMouseEvent*>(ev);
            const QPoint delta = me->globalPosition().toPoint() - m_drag_start_global;
            const int pw = parentWidget() ? parentWidget()->width()  : 9999;
            const int ph = parentWidget() ? parentWidget()->height() : 9999;
            move(qBound(0, m_drag_start_pos.x() + delta.x(), pw - kPanelW),
                 qBound(0, m_drag_start_pos.y() + delta.y(), ph - kPanelH));
            me->accept();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        if (m_dragging) {
            m_dragging = false;
            m_header->setCursor(Qt::OpenHandCursor);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QFrame::eventFilter(watched, ev);
}

void ConversationPanel::onSend()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty()) return;
    m_input->clear();
    appendMessage(text, true);
    QTimer::singleShot(0, this, [this] {
        m_scroll->verticalScrollBar()->setValue(
            m_scroll->verticalScrollBar()->maximum());
    });
}

void ConversationPanel::appendMessage(const QString& text, bool is_user)
{
    auto* row = new QWidget;
    auto* rl  = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    auto* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setMaximumWidth(kBubbleMaxW);
    lbl->setAttribute(Qt::WA_StyledBackground, true);
    lbl->setObjectName(is_user ? "convBubbleUser" : "convBubbleAI");

    if (is_user) { rl->addStretch(); rl->addWidget(lbl); }
    else         { rl->addWidget(lbl); rl->addStretch(); }

    m_msg_layout->insertWidget(m_msg_layout->count() - 1, row);
}

} // namespace dolphin::ui
