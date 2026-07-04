// PanelChatWidget.cpp — the assistant console chrome: construction, input
// handling, the banner state, and transcript rendering. The local Ollama
// backend (setup chain, streaming, process management) lives in
// PanelChatWidget.Ollama.cpp.
//
// Design: this pane lives in the bottom dock next to Problems / Output /
// Jobs / Terminal and reads as a query console, not a consumer chat app —
// no bubbles or avatars. Queries are monospace `›` prompt lines with a
// timestamp; answers are full-width blocks set off by an accent left rule.
#include "ui/mainwindow/rightpanel/PanelChatWidget.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QEvent>
#include <QFrame>
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
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

static constexpr int kInputMinH = 26;
static constexpr int kInputMaxH = 68;

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

    // -- Header strip (matches the Terminal pane's header) -----------------------
    auto* hdr = new QWidget(this);
    hdr->setObjectName("panelChatHdr");
    hdr->setFixedHeight(Theme::kPanelHdrH);
    auto* hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(Theme::kSpacing4, 0, Theme::kSpacing3, 0);
    hl->setSpacing(Theme::kSpacing2);

    auto* title = new QLabel(tr("Assistant"), hdr);
    title->setObjectName("panelChatTitle");

    auto* badge = new QLabel(tr("on-device"), hdr);
    badge->setObjectName("panelChatBadge");
    badge->setToolTip(tr("Runs a local model via Ollama — no data leaves this machine."));

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

    auto* clear_btn = new QPushButton(tr("Clear"), hdr);
    clear_btn->setObjectName("panelChatClearBtn");
    clear_btn->setFixedHeight(Theme::kSmallBtnSz);
    clear_btn->setCursor(Qt::PointingHandCursor);
    clear_btn->setToolTip(tr("Clear the transcript and start over"));
    connect(clear_btn, &QPushButton::clicked, this, &PanelChatWidget::clearChat);

    hl->addWidget(title);
    hl->addWidget(badge);
    hl->addStretch(1);
    hl->addWidget(m_model_combo);
    hl->addWidget(clear_btn);
    root->addWidget(hdr);

    // -- Transcript scroll area --------------------------------------------------
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("panelChatScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setFrameShape(QFrame::NoFrame);

    auto* msg_widget = new QWidget;
    msg_widget->setObjectName("panelChatMessages");
    m_msg_layout = new QVBoxLayout(msg_widget);
    m_msg_layout->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3,
                                     Theme::kSpacing4, Theme::kSpacing3);
    m_msg_layout->setSpacing(4);

    buildEmptyState(m_msg_layout);

    m_scroll->setWidget(msg_widget);
    root->addWidget(m_scroll, 1);

    // -- Input row (terminal-style prompt line) -----------------------------------
    auto* input_row = new QFrame(this);
    input_row->setObjectName("panelChatInputRow");
    auto* il = new QHBoxLayout(input_row);
    il->setContentsMargins(Theme::kSpacing4, Theme::kSpacing2,
                           Theme::kSpacing3, Theme::kSpacing2);
    il->setSpacing(Theme::kSpacing2);

    auto* prompt = new QLabel(QStringLiteral("›"), input_row);
    prompt->setObjectName("panelChatPrompt");
    prompt->setAlignment(Qt::AlignTop);
    prompt->setContentsMargins(0, 3, 0, 0);

    m_input = new QTextEdit(input_row);
    m_input->setObjectName("panelChatInput");
    m_input->setPlaceholderText(
        tr("Ask about your survey data — Enter to send, Shift+Enter for a new line"));
    m_input->setFixedHeight(kInputMinH);
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input->installEventFilter(this);

    m_send_btn = new QPushButton(tr("Send"), input_row);
    m_send_btn->setObjectName("panelChatSendBtn");
    m_send_btn->setFixedHeight(Theme::kSmallBtnSz);
    m_send_btn->setCursor(Qt::PointingHandCursor);
    m_send_btn->setEnabled(false);
    connect(m_send_btn, &QPushButton::clicked, this, &PanelChatWidget::onSend);

    il->addWidget(prompt);
    il->addWidget(m_input, 1);
    il->addWidget(m_send_btn, 0, Qt::AlignBottom);
    root->addWidget(input_row);

    connect(m_input->document(), &QTextDocument::contentsChanged, this, [this] {
        const int doc_h = static_cast<int>(m_input->document()->size().height()) + 2;
        const int new_h = std::clamp(doc_h, kInputMinH, kInputMaxH);
        if (m_input->height() != new_h) m_input->setFixedHeight(new_h);
        m_send_btn->setEnabled(!m_reply && !m_pull_reply &&
                               !m_input->toPlainText().trimmed().isEmpty());
    });
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

// -- Transcript rows -------------------------------------------------------------

// Answer block: full-width text set off by an accent left rule (see QSS).
// Returns the row; *out_label receives the text label for streaming updates.
static QWidget* makeAnswerRow(const QString& text, QLabel** out_label)
{
    // Full-width block, like the Output/Terminal panes next door. The accent
    // left rule (QSS) is what separates answers from queries — not a bubble.
    auto* row = new QWidget;
    auto* rl  = makeCompactLayout<QHBoxLayout>(row);

    auto* block = new QFrame(row);
    block->setObjectName("chatAnswerBlock");
    block->setAttribute(Qt::WA_StyledBackground, true);
    auto* bl = new QVBoxLayout(block);
    bl->setContentsMargins(10, 2, 4, 2);
    bl->setSpacing(0);

    auto* lbl = new QLabel(text, block);
    lbl->setObjectName("chatAnswerText");
    lbl->setWordWrap(true);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bl->addWidget(lbl);

    rl->addWidget(block, 1);

    if (out_label) *out_label = lbl;
    return row;
}

void PanelChatWidget::appendStreamingBubble()
{
    auto* row = makeAnswerRow(QStringLiteral("▍"), &m_stream_label);
    m_stream_row = row;
    m_msg_layout->insertWidget(m_msg_layout->count() - 1, row);
    scrollToBottom();
}

void PanelChatWidget::appendMessage(const QString& text, bool is_user)
{
    if (m_msg_count == 0 && m_empty_state) clearEmptyState();
    ++m_msg_count;

    QWidget* row = nullptr;
    if (is_user) {
        // Query line: `›` prompt + monospace text + timestamp, like a log entry.
        row      = new QWidget;
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, m_msg_count > 1 ? 10 : 0, 0, 0);
        rl->setSpacing(Theme::kSpacing2);

        auto* glyph = new QLabel(QStringLiteral("›"), row);
        glyph->setObjectName("chatQueryGlyph");
        glyph->setAlignment(Qt::AlignTop);

        auto* lbl = new QLabel(text, row);
        lbl->setObjectName("chatQueryText");
        lbl->setWordWrap(true);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);

        auto* time_lbl = new QLabel(QTime::currentTime().toString(
                                        QStringLiteral("hh:mm:ss")), row);
        time_lbl->setObjectName("chatTimestamp");
        time_lbl->setAlignment(Qt::AlignTop);

        rl->addWidget(glyph);
        rl->addWidget(lbl, 1);
        rl->addWidget(time_lbl);
    } else {
        row = makeAnswerRow(text, nullptr);
    }

    m_msg_layout->insertWidget(m_msg_layout->count() - 1, row);
    scrollToBottom();
}

// -- Banner (empty) state --------------------------------------------------------

void PanelChatWidget::buildEmptyState(QVBoxLayout* into)
{
    m_empty_state = new QWidget;
    m_empty_state->setObjectName("panelChatEmpty");
    auto* vl = new QVBoxLayout(m_empty_state);
    vl->setContentsMargins(0, Theme::kSpacing2, 0, 0);
    vl->setSpacing(0);

    // Console-style banner, top-left aligned — reads like a tool's MOTD, not
    // a chat splash screen.
    auto* title_lbl = new QLabel(tr("SURVEY ASSISTANT"), m_empty_state);
    title_lbl->setObjectName("chatBannerTitle");

    auto* sub_lbl = new QLabel(
        tr("Local model via Ollama — runs entirely on this machine.\n"
           "Ask about survey data, processing steps, nav corrections, "
           "or display settings."),
        m_empty_state);
    sub_lbl->setObjectName("chatBannerSub");
    sub_lbl->setWordWrap(true);

    vl->addWidget(title_lbl);
    vl->addSpacing(4);
    vl->addWidget(sub_lbl);
    vl->addSpacing(Theme::kSpacing4);

    struct Suggestion { const char* label; const char* prompt; };
    static const Suggestion kSuggestions[] = {
        { "Describe this layer",       "Describe the current layer and what data it contains."  },
        { "Check data quality",        "What data quality issues should I look out for?"        },
        { "Explain nav corrections",   "Explain the navigation corrections shown in this file." },
        { "Compare export formats",    "What export formats are available and when to use each?" },
    };
    for (const auto& s : kSuggestions) {
        auto* btn = new QPushButton(
            QStringLiteral("›  ") + tr(s.label), m_empty_state);
        btn->setObjectName("chatSuggestBtn");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        const QString prompt = tr(s.prompt);
        connect(btn, &QPushButton::clicked, this, [this, prompt] {
            clearEmptyState();
            m_input->setPlainText(prompt);
            onSend();
        });
        vl->addWidget(btn, 0, Qt::AlignLeft);
    }

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

void PanelChatWidget::scrollToBottom()
{
    QTimer::singleShot(0, this, [this] {
        m_scroll->verticalScrollBar()->setValue(
            m_scroll->verticalScrollBar()->maximum());
    });
}

} // namespace dolphin::ui
