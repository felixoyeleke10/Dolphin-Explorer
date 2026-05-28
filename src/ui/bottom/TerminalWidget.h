#pragma once
#include <QProcess>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;

namespace dolphin::ui {

// Embedded terminal panel.
// Executes commands via cmd.exe (Windows) or /bin/sh (Unix).
// Tracks the current working directory across cd commands.
// Supports command history (Up/Down), colored output, and process kill.
class TerminalWidget : public QWidget {
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget* parent = nullptr);
    ~TerminalWidget() override;

public slots:
    void setWorkingDirectory(const QString& dir);

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override;

private slots:
    void onSubmit();
    void onReadyReadStdOut();
    void onReadyReadStdErr();
    void onProcessFinished(int exit_code, QProcess::ExitStatus status);
    void onKill();
    void onClear();

private:
    void buildLayout();
    void updateCwdLabel();
    void runCommand(const QString& raw);
    bool tryBuiltin(const QString& cmd);
    void appendColored(const QString& text, const QColor& color);
    static QString stripAnsi(const QString& text);
    void setBusy(bool busy);
    QString shellPrompt() const;

    QPlainTextEdit* m_output    = nullptr;
    QLineEdit*      m_input     = nullptr;
    QLabel*         m_cwd_label = nullptr;
    QToolButton*    m_kill_btn  = nullptr;
    QProcess*       m_process   = nullptr;

    QString         m_cwd;
    QStringList     m_history;
    int             m_hist_idx  = -1;
    bool            m_busy      = false;
};

} // namespace dolphin::ui
