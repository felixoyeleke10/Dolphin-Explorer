#include "ui/mainwindow/rightpanel/PanelChatWidget.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int  kBubbleMaxW     = 210;
static constexpr int  kInputMinH      = 30;
static constexpr int  kInputMaxH      = 68;
static constexpr int  kPollMaxRetries = 20;   // 20 × 1 s = 20 s timeout

static constexpr char kOllamaBase[]   = "http://localhost:11434";
static constexpr char kTagsUrl[]      = "http://localhost:11434/api/tags";
static constexpr char kPullUrl[]      = "http://localhost:11434/api/pull";
static constexpr char kChatUrl[]      = "http://localhost:11434/api/chat";

static constexpr char kSystemPrompt[] =
    "You are Dolphin AI, a helpful assistant built into Dolphin Explorer "
    "marine survey software. Help users understand sonar data, processing "
    "results, nav corrections, display settings, and project management. "
    "Be concise and direct.";

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

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

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

    auto* btn_row = new QHBoxLayout;
    btn_row->setContentsMargins(0, 0, 0, 0);
    btn_row->setSpacing(0);

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

// -- Accessors -----------------------------------------------------------------

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

// -- Send entry point ----------------------------------------------------------

void PanelChatWidget::onSend()
{
    if (m_reply || m_pull_reply) return;
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty()) return;

    m_input->clear();
    setInputEnabled(false);

    if (m_msg_count == 0 && m_empty_state) clearEmptyState();
    ++m_msg_count;
    appendMessage(text, true);

    QJsonObject user_msg;
    user_msg["role"]    = "user";
    user_msg["content"] = text;
    m_history.append(user_msg);

    appendStreamingBubble();

    // Fast path: model already confirmed this session
    if (!m_confirmed_model.isEmpty() && m_confirmed_model == currentModelId()) {
        sendChatRequest();
        return;
    }

    checkOllamaAndSend();
}

// -- Setup chain: check → [start] → [pull] → send -----------------------------

void PanelChatWidget::checkOllamaAndSend()
{
    setStreamStatus(tr("⏳ Connecting to Ollama…"));

    QNetworkRequest req;
    req.setUrl(QUrl(QLatin1String(kTagsUrl)));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        handleTagsReply(reply);
    });
}

void PanelChatWidget::handleTagsReply(QNetworkReply* reply)
{
    const auto err  = reply->error();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (err == QNetworkReply::ConnectionRefusedError ||
        err == QNetworkReply::NetworkSessionFailedError) {
        tryStartOllama();
        return;
    }
    if (err != QNetworkReply::NoError) {
        failSetup(tr("Cannot reach Ollama: %1").arg(reply->errorString()));
        return;
    }
    checkModelAndSend(data);
}

// -- Ollama process management -------------------------------------------------

QString PanelChatWidget::findOllamaExe()
{
    // 1. Next to the app executable (bundled)
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& name : { QStringLiteral("ollama.exe"), QStringLiteral("ollama") }) {
        if (QFileInfo::exists(appDir + "/" + name))
            return appDir + "/" + name;
    }

    // 2. Windows install locations — official installer uses Programs\Ollama,
    //    older and portable installs may drop directly into %LOCALAPPDATA%\Ollama.
    const QString localAppData = QDir::fromNativeSeparators(
        qEnvironmentVariable("LOCALAPPDATA"));
    if (!localAppData.isEmpty()) {
        for (const QString& rel : {
                 QStringLiteral("/Programs/Ollama/ollama.exe"),
                 QStringLiteral("/Ollama/ollama.exe") }) {
            if (QFileInfo::exists(localAppData + rel))
                return localAppData + rel;
        }
    }

    // 3. macOS / Linux default install
    for (const QString& path : { QStringLiteral("/usr/local/bin/ollama"),
                                  QStringLiteral("/usr/bin/ollama") }) {
        if (QFileInfo::exists(path)) return path;
    }

    // 4. Anywhere on PATH
    return QStandardPaths::findExecutable(QStringLiteral("ollama"));
}

void PanelChatWidget::tryStartOllama()
{
    // Use detected path; fall back to bare "ollama" so the OS resolves via PATH.
    QString exe = findOllamaExe();
    if (exe.isEmpty()) exe = QStringLiteral("ollama");

    setStreamStatus(tr("▶ Starting Ollama…"));

    if (!m_ollama_proc) {
        m_ollama_proc = new QProcess(this);
        m_ollama_proc->setProgram(exe);
        m_ollama_proc->setArguments({ QStringLiteral("serve") });

        // Only report failure when the OS cannot launch the process at all.
        connect(m_ollama_proc, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError err) {
                    if (err == QProcess::FailedToStart) {
                        if (m_poll_timer) m_poll_timer->stop();
                        failSetup(tr("Ollama not found.\n\n"
                                     "Download it from ollama.com/download\n"
                                     "or place ollama.exe next to DolphinExplorer.exe.\n\n"
                                     "Then pull a model:\n"
                                     "  ollama pull %1").arg(currentModelId()));
                    }
                });

        m_ollama_proc->start();
    }

    m_poll_retries = 0;
    if (!m_poll_timer) {
        m_poll_timer = new QTimer(this);
        m_poll_timer->setInterval(1000);
        connect(m_poll_timer, &QTimer::timeout, this, &PanelChatWidget::pollForReady);
    }
    m_poll_timer->start();
}

void PanelChatWidget::pollForReady()
{
    if (m_poll_retries++ > kPollMaxRetries) {
        m_poll_timer->stop();
        failSetup(tr("Ollama did not start within %1 seconds.\n"
                     "Try running 'ollama serve' manually.")
                  .arg(kPollMaxRetries));
        return;
    }

    QNetworkRequest req;
    req.setUrl(QUrl(QLatin1String(kTagsUrl)));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto err  = reply->error();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        if (err == QNetworkReply::NoError) {
            m_poll_timer->stop();
            checkModelAndSend(data);
        }
        // else: still starting — timer will fire again
    });
}

// -- Model pull ----------------------------------------------------------------

void PanelChatWidget::checkModelAndSend(const QByteArray& tags_json)
{
    const QString model = currentModelId();
    const QJsonArray models = QJsonDocument::fromJson(tags_json)
                                  .object()["models"].toArray();
    for (const auto& v : models) {
        if (v.toObject()["name"].toString() == model) {
            // Model already pulled — go straight to chat
            m_confirmed_model = model;
            setStreamStatus(QStringLiteral("▍"));
            sendChatRequest();
            return;
        }
    }

    // Model not found — need to pull it first
    pullModelAndSend();
}

void PanelChatWidget::pullModelAndSend()
{
    const QString model = currentModelId();
    setStreamStatus(tr("⬇ Downloading %1 (first time only)…\n"
                       "This may take a few minutes.").arg(model));

    QJsonObject body;
    body["name"]   = model;
    body["stream"] = true;

    QNetworkRequest req;
    req.setUrl(QUrl(QLatin1String(kPullUrl)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(0);

    m_stream_buf.clear();
    m_pull_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_pull_reply, &QNetworkReply::readyRead, this, &PanelChatWidget::onPullReadyRead);
    connect(m_pull_reply, &QNetworkReply::finished,  this, &PanelChatWidget::onPullFinished);
}

void PanelChatWidget::onPullReadyRead()
{
    if (!m_pull_reply) return;
    m_stream_buf += QString::fromUtf8(m_pull_reply->readAll());

    int nl;
    while ((nl = m_stream_buf.indexOf('\n')) != -1) {
        const QString line = m_stream_buf.left(nl).trimmed();
        m_stream_buf = m_stream_buf.mid(nl + 1);
        if (line.isEmpty()) continue;

        const QJsonObject obj = QJsonDocument::fromJson(line.toUtf8()).object();
        const QString status  = obj["status"].toString();
        if (status.isEmpty()) continue;

        // Show pull progress (e.g. "pulling manifest", "50% …", "success")
        const QString model = currentModelId();
        if (obj.contains("completed") && obj.contains("total")) {
            const double pct = 100.0 * obj["completed"].toDouble()
                             / std::max(obj["total"].toDouble(), 1.0);
            setStreamStatus(tr("⬇ %1  %2%")
                .arg(model).arg(static_cast<int>(pct)));
        } else {
            setStreamStatus(tr("⬇ %1  %2").arg(model, status));
        }
    }
}

void PanelChatWidget::onPullFinished()
{
    if (!m_pull_reply) return;
    onPullReadyRead();   // drain remainder

    const auto err = m_pull_reply->error();
    m_pull_reply->deleteLater();
    m_pull_reply = nullptr;
    m_stream_buf.clear();

    if (err != QNetworkReply::NoError) {
        failSetup(tr("Model download failed.\n"
                     "Check your connection or try:\n"
                     "  ollama pull %1").arg(currentModelId()));
        return;
    }

    m_confirmed_model = currentModelId();
    setStreamStatus(QStringLiteral("▍"));
    sendChatRequest();
}

// -- Chat request --------------------------------------------------------------

void PanelChatWidget::sendChatRequest()
{
    QJsonArray messages;
    QJsonObject sys;
    sys["role"]    = "system";
    sys["content"] = QLatin1String(kSystemPrompt);
    messages.append(sys);
    for (const auto& v : std::as_const(m_history))
        messages.append(v);

    QJsonObject body;
    body["model"]    = currentModelId();
    body["messages"] = messages;
    body["stream"]   = true;

    QNetworkRequest req;
    req.setUrl(QUrl(QLatin1String(kChatUrl)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(0);

    m_stream_buf.clear();
    m_full_response.clear();

    m_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &PanelChatWidget::onChatReadyRead);
    connect(m_reply, &QNetworkReply::finished,  this, &PanelChatWidget::onChatFinished);
}

void PanelChatWidget::onChatReadyRead()
{
    if (!m_reply) return;
    m_stream_buf += QString::fromUtf8(m_reply->readAll());

    int nl;
    while ((nl = m_stream_buf.indexOf('\n')) != -1) {
        const QString line = m_stream_buf.left(nl).trimmed();
        m_stream_buf = m_stream_buf.mid(nl + 1);
        if (line.isEmpty()) continue;

        const QJsonObject obj   = QJsonDocument::fromJson(line.toUtf8()).object();
        const QString     chunk = obj["message"].toObject()["content"].toString();
        if (!chunk.isEmpty()) {
            m_full_response += chunk;
            if (m_stream_label) {
                m_stream_label->setText(m_full_response + QStringLiteral("▍"));
                scrollToBottom();
            }
        }
    }
}

void PanelChatWidget::onChatFinished()
{
    if (!m_reply) return;
    onChatReadyRead();

    const auto    err  = m_reply->error();
    const QString emsg = m_reply->errorString();
    m_reply->deleteLater();
    m_reply = nullptr;

    if (err != QNetworkReply::NoError) {
        if (m_stream_row) {
            m_msg_layout->removeWidget(m_stream_row);
            m_stream_row->deleteLater();
            m_stream_row  = nullptr;
            m_stream_label = nullptr;
        }
        appendMessage(tr("Error: %1").arg(emsg), false);
        if (!m_history.isEmpty()) m_history.removeLast();
        m_confirmed_model.clear();
    } else {
        if (m_stream_label) {
            const QString final_text = m_full_response.isEmpty()
                ? tr("(no response — model may still be loading)")
                : m_full_response;
            m_stream_label->setText(final_text);
            m_stream_label = nullptr;
            m_stream_row   = nullptr;
        }
        if (!m_full_response.isEmpty()) {
            QJsonObject asst;
            asst["role"]    = "assistant";
            asst["content"] = m_full_response;
            m_history.append(asst);
        }
    }

    m_full_response.clear();
    m_stream_buf.clear();
    setInputEnabled(true);
}

// -- Helpers -------------------------------------------------------------------

void PanelChatWidget::setStreamStatus(const QString& text)
{
    if (m_stream_label) {
        m_stream_label->setText(text);
        scrollToBottom();
    }
}

void PanelChatWidget::failSetup(const QString& reason)
{
    if (m_stream_row) {
        m_msg_layout->removeWidget(m_stream_row);
        m_stream_row->deleteLater();
        m_stream_row  = nullptr;
        m_stream_label = nullptr;
    }
    if (!m_history.isEmpty()) m_history.removeLast();
    appendMessage(reason, false);
    setInputEnabled(true);
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
    auto* rl  = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

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
    auto* gl = new QGridLayout(chips);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(Theme::kSpacing2);

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
