#pragma once
#include <QJsonArray>
#include <QWidget>

class QComboBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QPushButton;
class QScrollArea;
class QTextEdit;
class QTimer;
class QVBoxLayout;

namespace dolphin::ui {

// Offline AI chat panel — manages its own Ollama process lifecycle.
// On first send: finds ollama.exe (app dir → %LOCALAPPDATA% → PATH),
// starts 'ollama serve' if needed, pulls the model if missing, then chats.
// Subsequent messages skip the check once Ollama is confirmed running.
class PanelChatWidget : public QWidget {
    Q_OBJECT
public:
    explicit PanelChatWidget(QWidget* parent = nullptr);
    ~PanelChatWidget() override;

    void    clearChat();
    QString currentModelId() const;

protected:
    bool eventFilter(QObject* obj, QEvent* evt) override;

private slots:
    void onSend();
    void onChatReadyRead();
    void onChatFinished();

private:
    // Setup chain: check → [start] → [pull] → send
    void checkOllamaAndSend();
    void handleTagsReply(QNetworkReply* reply);
    void tryStartOllama();
    void pollForReady();
    void checkModelAndSend(const QByteArray& tags_json);
    void pullModelAndSend();
    void onPullReadyRead();
    void onPullFinished();
    void sendChatRequest();

    void appendMessage(const QString& text, bool is_user);
    void appendStreamingBubble();
    void setStreamStatus(const QString& text);
    void failSetup(const QString& reason);
    void buildEmptyState(QVBoxLayout* into);
    void clearEmptyState();
    void scrollToBottom();
    void setInputEnabled(bool enabled);

    static QString findOllamaExe();

    QScrollArea* m_scroll       = nullptr;
    QVBoxLayout* m_msg_layout   = nullptr;
    QTextEdit*   m_input        = nullptr;
    QPushButton* m_send_btn     = nullptr;
    QComboBox*   m_model_combo  = nullptr;
    QWidget*     m_empty_state  = nullptr;
    int          m_msg_count    = 0;

    // Ollama lifecycle
    QProcess*      m_ollama_proc    = nullptr;
    QTimer*        m_poll_timer     = nullptr;
    int            m_poll_retries   = 0;
    QString        m_confirmed_model;          // model ID confirmed ready; skip checks when set
    QNetworkReply* m_pull_reply     = nullptr;

    // Chat request
    QNetworkAccessManager* m_nam           = nullptr;
    QNetworkReply*         m_reply         = nullptr;
    QString                m_stream_buf;
    QString                m_full_response;
    QLabel*                m_stream_label  = nullptr;
    QWidget*               m_stream_row    = nullptr;
    QJsonArray             m_history;
};

} // namespace dolphin::ui
