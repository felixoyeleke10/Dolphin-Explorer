// PanelChatWidget.cpp — the chat panel chrome: construction, input handling,
// the empty state, and message-bubble rendering. The local Ollama backend
// (setup chain, streaming, process management) lives in PanelChatWidget.Ollama.cpp.
#include "ui/mainwindow/rightpanel/PanelChatWidget.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

static constexpr int kBubbleMaxW = 210;
static constexpr int kInputMinH  = 30;
static constexpr int kInputMaxH  = 68;

struct ModelEntry { const char* label; const char* id; };
static const ModelEntry kModels[] = {
    { "Llama 3.2 (1B)",  "llama3.2:1b"  },
    { "Qwen 2.5 (1.5B)", "qwen2.5:1.5b" },
    { "Phi-3 Mini",      "phi3:mini"    },
    { "Llama 3.2 (3B)",  "llama3.2:3b"  },
};
static constexpr int kDefaultModel = 0;

// -- Construction / destruction ------------------------------------------------

PanelChatWidget::PanelChatWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("panelChat");

    m_nam = new QNetworkAccessManager(this);

    auto* root = makeCompactLayout<QVBoxLayout>(this);

    // -- Header ----------------------------------------------------------------
    auto* hdr = new QWidget(this);
    hdr->setObjectName("panelChatHdr");
    hdr->setFixedHeight(Theme::kPanelHdrH);
    auto* hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(Theme::kSpacing4, 0, Theme::kSpacing3, 0);
    hl->setSpacing(Theme::kSpacing2);

    auto* icon = new QLabel(QStringLiteral("✶"), hdr);
    icon->setObjectName("panelChatIcon");

    auto* title = new QLabel(tr("Dolphin AI"), hdr);
    title->setObjectName("panelChatTitle");

    m_model_combo = new QComboBox(hdr);
    m_model_combo->setObjectName("panelChatModelCombo");
    m_model_combo->setFixedHeight(Theme::kSmallBtnSz);
    m_model_combo->setCursor(Qt::PointingHandCursor);
    for (const auto& m : kModels)
        m_model_combo->addItem(QLatin1String(m.label), QLatin1String(m.id));
    m_model_combo->setCurrentIndex(kDefaultModel);
    // Switching model invalidates the confirmed state
    connect(m_model_combo, &QComboBox::currentIndexChanged,
            this, [this] { m_confirmed_model.clear(); });

    auto* new_btn = new QPushButton(tr("New"), hdr);
    new_btn->setObjectName("panelChatNewBtn");
    new_btn->setFixedHeight(Theme::kSmallBtnSz);
    new_btn->setCursor(Qt::PointingHandCursor);
    new_btn->setToolTip(tr("Start a new conversation"));
    connect(new_btn, &QPushButton::clicked, this, &PanelChatWidget::clearChat);

    hl->addWidget(icon);
    hl->addWidget(title);
    hl->addStretch(1);
    hl->addWidget(m_model_combo);
    hl->addWidget(new_btn);
    root->addWidget(hdr);

    // -- Separator -------------------------------------------------------------
    auto* sep = new QFrame(this);
    sep->setObjectName("panelChatSep");
    sep->setFixedHeight(Theme::kSepSz);
    root->addWidget(sep);

    // -- Message scroll area ---------------------------------------------------
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("panelChatScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setFrameShape(QFrame::NoFrame);

    auto* msg_widget = new QWidget;
    msg_widget->setObjectName("panelChatMessages");
    m_msg_layout = new QVBoxLayout(msg_widget);
    m_msg_layout->setContentsMargins(10, 12, 10, 12);
    m_msg_layout->setSpacing(8);

    buildEmptyState(m_msg_layout);

    m_scroll->setWidget(msg_widget);
    root->addWidget(m_scroll, 1);

    // -- Input separator -------------------------------------------------------
    auto* sep2 = new QFrame(this);
    sep2->setObjectName("panelChatSep");
    sep2->setFixedHeight(Theme::kSepSz);
    root->addWidget(sep2);

    // -- Input area ------------------------------------------------------------
    auto* input_box = new QFrame(this);
    input_box->setObjectName("panelChatInputBox");
    auto* il = new QVBoxLayout(input_box);
    il->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2,
                           Theme::kSpacing3, Theme::kSpacing2);
    il->setSpacing(Theme::kSpacing1);

    m_input = new QTextEdit(input_box);
    m_input->setObjectName("panelChatInput");
    m_input->setPlaceholderText(tr("Ask anything about your data…"));
    m_input->setFixedHeight(kInputMinH);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input->installEventFilter(this);

    auto* btn_row = makeCompactLayout<QHBoxLayout>();

    auto* hint_lbl = new QLabel(tr("Shift+⏎ new line"), input_box);
    hint_lbl->setObjectName("panelChatHint");

    m_send_btn = new QPushButton(QStringLiteral("↑"), input_box);
    m_send_btn->setObjectName("panelChatSendBtn");
    m_send_btn->setFixedSize(26, 26);
    m_send_btn->setCursor(Qt::PointingHandCursor);
    m_send_btn->setEnabled(false);
    connect(m_send_btn, &QPushButton::clicked, this, &PanelChatWidget::onSend);

    btn_row->addWidget(hint_lbl);
    btn_row->addStretch(1);
    btn_row->addWidget(m_send_btn);

    il->addWidget(m_input);
    il->addLayout(btn_row);
    root->addWidget(input_box);

    connect(m_input->document(), &QTextDocument::contentsChanged, this, [this] {
        const int doc_h = static_cast<int>(m_input->document()->size().height()) + 2;
        const int new_h = std::clamp(doc_h, kInputMinH, kInputMaxH);
        if (m_input->height() != new_h) m_input->setFixedHeight(new_h);
        m_send_btn->setEnabled(!m_reply && !m_pull_reply &&
                               !m_input->toPlainText().trimmed().isEmpty());
    });

    appendMessage(tr("Hi! Ask me about your survey data, processing results, "
                     "nav corrections, or display settings. Ollama runs locally "
                     "— no internet required."), false);
}

PanelChatWidget::~PanelChatWidget()
{
    // Abort any in-flight network requests cleanly
    if (m_reply)      { m_reply->abort();      m_reply->deleteLater();      }
    if (m_pull_reply) { m_pull_reply->abort();  m_pull_reply->deleteLater(); }
    // Leave the ollama server running — it's a system-level service
}

// -- Accessors / input ---------------------------------------------------------

QString PanelChatWidget::currentModelId() const
{
    return m_model_combo ? m_model_combo->currentData().toString() : QString{};
}

bool PanelChatWidget::eventFilter(QObject* obj, QEvent* evt)
{
    if (obj == m_input && evt->type() == QEvent::KeyPress) {
        const auto* ke = static_cast<QKeyEvent*>(evt);
        if (ke->key() == Qt::Key_Return && !(ke->modifiers() & Qt::ShiftModifier)) {
            onSend();
            return true;
        }
    }
    return QWidget::eventFilter(obj, evt);
}

void PanelChatWidget::setInputEnabled(bool enabled)
{
    m_input->setEnabled(enabled);
    m_send_btn->setEnabled(enabled && !m_input->toPlainText().trimmed().isEmpty());
    if (enabled) m_input->setFocus();
}

void PanelChatWidget::appendStreamingBubble()
{
    auto* row = new QWidget;
    auto* rl  = makeCompactLayout<QHBoxLayout>(row);

    m_stream_label = new QLabel(QStringLiteral("▍"));
    m_stream_label->setWordWrap(true);
    m_stream_label->setMaximumWidth(kBubbleMaxW);
    m_stream_label->setAttribute(Qt::WA_StyledBackground, true);
    m_stream_label->setObjectName("convBubbleAI");

    rl->addWidget(m_stream_label);
    rl->addStretch();

    m_stream_row = row;
    m_msg_layout->insertWidget(m_msg_layout->count() - 1, row);
    scrollToBottom();
}

// -- Empty state ---------------------------------------------------------------

void PanelChatWidget::buildEmptyState(QVBoxLayout* into)
{
    m_empty_state = new QWidget;
    m_empty_state->setObjectName("panelChatEmpty");
    auto* vl = new QVBoxLayout(m_empty_state);
    vl->setContentsMargins(Theme::kSpacing4, 0, Theme::kSpacing4, 0);
    vl->setSpacing(0);

    auto* icon_lbl = new QLabel(QStringLiteral("✶"), m_empty_state);
    icon_lbl->setObjectName("panelChatEmptyIcon");
    icon_lbl->setAlignment(Qt::AlignCenter);

    auto* title_lbl = new QLabel(tr("Ask me anything"), m_empty_state);
    title_lbl->setObjectName("panelChatEmptyTitle");
    title_lbl->setAlignment(Qt::AlignCenter);

    auto* sub_lbl = new QLabel(
        tr("About your data, processing steps,\ndisplay settings, or nav corrections."),
        m_empty_state);
    sub_lbl->setObjectName("panelChatEmptySub");
    sub_lbl->setAlignment(Qt::AlignCenter);
    sub_lbl->setWordWrap(true);

    auto* chips = new QWidget(m_empty_state);
    chips->setObjectName("panelChatChips");
    auto* gl = makeCompactLayout<QGridLayout>(chips, Theme::kSpacing2);

    struct Chip { const char* label; const char* prompt; };
    static const Chip kChips[] = {
        { "Describe this layer",  "Describe the current layer and what data it contains."  },
        { "Data quality check",   "What data quality issues should I look out for?"        },
        { "Explain nav errors",   "Explain the navigation corrections shown in this file." },
        { "Export options",       "What export formats are available and when to use each?" },
    };
    for (int i = 0; i < 4; ++i) {
        auto* btn = new QPushButton(tr(kChips[i].label), chips);
        btn->setObjectName("panelChatChip");
        btn->setCursor(Qt::PointingHandCursor);
        const QString prompt = tr(kChips[i].prompt);
        connect(btn, &QPushButton::clicked, this, [this, prompt] {
            clearEmptyState();
            m_input->setPlainText(prompt);
            onSend();
        });
        gl->addWidget(btn, i / 2, i % 2);
    }

    vl->addWidget(icon_lbl);
    vl->addSpacing(6);
    vl->addWidget(title_lbl);
    vl->addSpacing(4);
    vl->addWidget(sub_lbl);
    vl->addSpacing(Theme::kSpacing5);
    vl->addWidget(chips);

    into->addStretch(1);
    into->addWidget(m_empty_state);
    into->addStretch(1);
}

void PanelChatWidget::clearEmptyState()
{
    if (!m_empty_state) return;
    while (m_msg_layout->count() > 0) {
        auto* item = m_msg_layout->takeAt(0);
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    m_empty_state = nullptr;
    m_msg_layout->addStretch(1);
}

void PanelChatWidget::clearChat()
{
    if (m_reply)      { m_reply->abort();      m_reply->deleteLater();      m_reply      = nullptr; }
    if (m_pull_reply) { m_pull_reply->abort();  m_pull_reply->deleteLater(); m_pull_reply = nullptr; }
    if (m_poll_timer) m_poll_timer->stop();

    m_stream_label = nullptr;
    m_stream_row   = nullptr;

    while (m_msg_layout->count() > 0) {
        auto* item = m_msg_layout->takeAt(0);
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    m_msg_count     = 0;
    m_history       = QJsonArray{};
    m_stream_buf.clear();
    m_full_response.clear();
    setInputEnabled(true);
    buildEmptyState(m_msg_layout);
}

// -- Message append ------------------------------------------------------------

void PanelChatWidget::appendMessage(const QString& text, bool is_user)
{
    if (m_msg_count == 0 && m_empty_state) clearEmptyState();
    ++m_msg_count;

    auto* row = new QWidget;
    auto* rl  = makeCompactLayout<QHBoxLayout>(row);

    auto* lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setMaximumWidth(kBubbleMaxW);
    lbl->setAttribute(Qt::WA_StyledBackground, true);
    lbl->setObjectName(is_user ? "convBubbleUser" : "convBubbleAI");

    if (is_user) { rl->addStretch(); rl->addWidget(lbl); }
    else         { rl->addWidget(lbl); rl->addStretch(); }

    m_msg_layout->insertWidget(m_msg_layout->count() - 1, row);
    scrollToBottom();
}

void PanelChatWidget::scrollToBottom()
{
    QTimer::singleShot(0, this, [this] {
        m_scroll->verticalScrollBar()->setValue(
            m_scroll->verticalScrollBar()->maximum());
    });
}

} // namespace dolphin::ui
