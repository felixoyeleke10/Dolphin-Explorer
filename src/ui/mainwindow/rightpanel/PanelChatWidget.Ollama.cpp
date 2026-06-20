// PanelChatWidget.Ollama.cpp — the local Ollama backend: the setup chain
// (check → start server → pull model → chat), streaming response parsing, and
// the setup/error status helpers. The widget chrome lives in PanelChatWidget.cpp.
#include "ui/mainwindow/rightpanel/PanelChatWidget.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

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

// -- Setup / error status ------------------------------------------------------

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

} // namespace dolphin::ui
