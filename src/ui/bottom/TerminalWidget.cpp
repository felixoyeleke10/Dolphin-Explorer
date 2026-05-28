// TerminalWidget.cpp — embedded terminal panel.
// Per-command execution via cmd.exe /C (Windows) or /bin/sh -c (Unix).
// Tracks cwd across cd commands; supports history, colored output, process kill.
#include "ui/bottom/TerminalWidget.h"
#include "ui/shell/Theme.h"

#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {

const QColor kColorPrompt  { Theme::kAccent      };  // interactive blue
const QColor kColorCommand { Theme::kTextPrimary  };  // primary white text
const QColor kColorStdout  { Theme::kIconStroke   };  // medium-grey stdout
const QColor kColorStderr  { Theme::kDangerBright  };  // terminal error red
const QColor kColorSystem  { Theme::kTextDisabled };  // dim system messages

} // namespace

// ── Constructor ───────────────────────────────────────────────────────────────

TerminalWidget::TerminalWidget(QWidget* parent)
    : QWidget(parent)
{
    buildLayout();

    m_cwd = QDir::toNativeSeparators(QDir::currentPath());
    updateCwdLabel();

    appendColored(
        "Dolphin Explorer Terminal\n"
        "Commands run via cmd.exe. "
        "cd, cls, clear, help are built-in. "
        "Up/Down for history.\n\n",
        kColorSystem);
}

TerminalWidget::~TerminalWidget()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

// ── Layout ────────────────────────────────────────────────────────────────────

void TerminalWidget::buildLayout()
{
    setObjectName("terminalWidget");

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    // Header: cwd label + kill + clear
    auto* header = new QWidget(this);
    header->setObjectName("terminalHeader");
    header->setFixedHeight(Theme::kFormBtnH);
    auto* hrow = new QHBoxLayout(header);
    hrow->setContentsMargins(Theme::kSpacing3, 0, Theme::kSpacing1, 0);
    hrow->setSpacing(Theme::kSpacing1);

    m_cwd_label = new QLabel(header);
    m_cwd_label->setObjectName("terminalCwdLabel");
    hrow->addWidget(m_cwd_label, 1);

    m_kill_btn = new QToolButton(header);
    m_kill_btn->setObjectName("terminalKillBtn");
    m_kill_btn->setText("■");   // ■ stop square
    m_kill_btn->setFixedSize(Theme::kSmallBtnSz, Theme::kSmallBtnSz);
    m_kill_btn->setToolTip(tr("Kill running process (Ctrl+C)"));
    m_kill_btn->setEnabled(false);
    hrow->addWidget(m_kill_btn);

    auto* clear_btn = new QToolButton(header);
    clear_btn->setObjectName("terminalClearBtn");
    clear_btn->setText("⌫");    // ⌫ erase
    clear_btn->setFixedSize(Theme::kSmallBtnSz, Theme::kSmallBtnSz);
    clear_btn->setToolTip(tr("Clear output"));
    hrow->addWidget(clear_btn);
    vbox->addWidget(header);

    // Output
    m_output = new QPlainTextEdit(this);
    m_output->setObjectName("terminalOutput");
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(5000);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono;
    mono.setFamilies({"Consolas", "Cascadia Code", "Courier New"});
    mono.setPointSize(Theme::kTerminalFontPt);
    m_output->setFont(mono);
    vbox->addWidget(m_output, 1);

    // Input row
    auto* input_row = new QWidget(this);
    input_row->setObjectName("terminalInputRow");
    input_row->setFixedHeight(Theme::kPanelRowH);
    auto* irow = new QHBoxLayout(input_row);
    irow->setContentsMargins(Theme::kSpacing3, 0, Theme::kSpacing3, 0);
    irow->setSpacing(Theme::kSpacing2);

    auto* arrow = new QLabel(">", input_row);
    arrow->setObjectName("terminalPromptArrow");
    irow->addWidget(arrow);

    m_input = new QLineEdit(input_row);
    m_input->setObjectName("terminalInput");
    m_input->setFrame(false);
    m_input->setFont(mono);
    m_input->setPlaceholderText(tr("enter command…"));
    m_input->installEventFilter(this);
    irow->addWidget(m_input, 1);
    vbox->addWidget(input_row);

    connect(m_input,    &QLineEdit::returnPressed, this, &TerminalWidget::onSubmit);
    connect(m_kill_btn, &QToolButton::clicked,     this, &TerminalWidget::onKill);
    connect(clear_btn,  &QToolButton::clicked,     this, &TerminalWidget::onClear);
}

// ── Working directory ─────────────────────────────────────────────────────────

void TerminalWidget::setWorkingDirectory(const QString& dir)
{
    if (dir.isEmpty() || !QDir(dir).exists()) return;
    m_cwd = QDir::toNativeSeparators(QDir::cleanPath(dir));
    updateCwdLabel();
}

void TerminalWidget::updateCwdLabel()
{
    QString display = m_cwd;
    if (display.length() > 64)
        display = "…" + display.right(61);
    m_cwd_label->setText(display);
    m_cwd_label->setToolTip(m_cwd);
}

// ── Input / history ───────────────────────────────────────────────────────────

bool TerminalWidget::eventFilter(QObject* watched, QEvent* ev)
{
    if (watched == m_input && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);

        if (ke->key() == Qt::Key_Up) {
            if (m_hist_idx < static_cast<int>(m_history.size()) - 1) {
                ++m_hist_idx;
                m_input->setText(
                    m_history[static_cast<int>(m_history.size()) - 1 - m_hist_idx]);
            }
            return true;
        }
        if (ke->key() == Qt::Key_Down) {
            if (m_hist_idx > 0) {
                --m_hist_idx;
                m_input->setText(
                    m_history[static_cast<int>(m_history.size()) - 1 - m_hist_idx]);
            } else {
                m_hist_idx = -1;
                m_input->clear();
            }
            return true;
        }
        // Ctrl+C kills running process
        if (ke->key() == Qt::Key_C &&
                (ke->modifiers() & Qt::ControlModifier) && m_busy) {
            onKill();
            return true;
        }
    }
    return QWidget::eventFilter(watched, ev);
}

void TerminalWidget::onSubmit()
{
    const QString cmd = m_input->text().trimmed();
    if (cmd.isEmpty()) return;

    if (m_history.isEmpty() || m_history.last() != cmd)
        m_history.append(cmd);
    m_hist_idx = -1;
    m_input->clear();

    runCommand(cmd);
}

// ── Command execution ─────────────────────────────────────────────────────────

QString TerminalWidget::shellPrompt() const
{
    return m_cwd + "> ";
}

void TerminalWidget::runCommand(const QString& raw)
{
    // Echo command with colored prompt
    appendColored(shellPrompt(), kColorPrompt);
    appendColored(raw + "\n",   kColorCommand);

    if (tryBuiltin(raw)) return;
    if (m_busy) return;

    setBusy(true);

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_cwd);

#ifdef Q_OS_WIN
    // chcp 65001 forces UTF-8 output without printing "Active code page: 65001"
    m_process->setProgram("cmd.exe");
    m_process->setArguments({"/C", "chcp 65001 >nul 2>&1 & " + raw});
#else
    m_process->setProgram("/bin/sh");
    m_process->setArguments({"-c", raw});
#endif

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &TerminalWidget::onReadyReadStdOut);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &TerminalWidget::onReadyReadStdErr);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TerminalWidget::onProcessFinished);

    m_process->start();
    if (!m_process->waitForStarted(3000)) {
        appendColored("error: failed to start shell process\n", kColorStderr);
        setBusy(false);
        m_process->deleteLater();
        m_process = nullptr;
    }
}

bool TerminalWidget::tryBuiltin(const QString& raw)
{
    const QString lower = raw.trimmed().toLower();

    // cls / clear
    if (lower == "cls" || lower == "clear") {
        m_output->clear();
        return true;
    }

    // help
    if (lower == "help") {
        appendColored(
            "Built-in:  cd <path>  cls  clear  help\n"
            "History:   Up / Down arrow keys\n"
            "Kill:      ■ button or Ctrl+C\n"
            "All other commands run via cmd.exe /C\n\n",
            kColorSystem);
        return true;
    }

    // cd
    const bool is_cd = (lower == "cd") ||
                       raw.trimmed().startsWith("cd ", Qt::CaseInsensitive);
    if (is_cd) {
        if (lower == "cd") {
            // Print cwd (Unix behaviour)
            appendColored(m_cwd + "\n", kColorStdout);
            return true;
        }

        // Extract path — skip optional /D flag used by cmd.exe
        QString target = raw.trimmed().mid(3).trimmed();
        if (target.startsWith("/d ", Qt::CaseInsensitive) ||
            target.startsWith("/D "))
            target = target.mid(3).trimmed();
        // Strip surrounding quotes
        if (target.length() >= 2 &&
            target.front() == '"' && target.back() == '"')
            target = target.mid(1, target.length() - 2);

        QDir d(m_cwd);
        if (d.cd(target)) {
            m_cwd = QDir::toNativeSeparators(d.absolutePath());
            updateCwdLabel();
        } else {
            appendColored(
                "The system cannot find the path specified: " + target + "\n",
                kColorStderr);
        }
        return true;
    }

    return false;
}

// ── Process I/O ───────────────────────────────────────────────────────────────

void TerminalWidget::onReadyReadStdOut()
{
    if (!m_process) return;
    const QString text = stripAnsi(
        QString::fromUtf8(m_process->readAllStandardOutput()));
    if (!text.isEmpty())
        appendColored(text, kColorStdout);
}

void TerminalWidget::onReadyReadStdErr()
{
    if (!m_process) return;
    const QString text = stripAnsi(
        QString::fromUtf8(m_process->readAllStandardError()));
    if (!text.isEmpty())
        appendColored(text, kColorStderr);
}

void TerminalWidget::onProcessFinished(int exit_code, QProcess::ExitStatus status)
{
    if (m_process) {
        const QByteArray out = m_process->readAllStandardOutput();
        if (!out.isEmpty())
            appendColored(stripAnsi(QString::fromUtf8(out)), kColorStdout);
        const QByteArray err = m_process->readAllStandardError();
        if (!err.isEmpty())
            appendColored(stripAnsi(QString::fromUtf8(err)), kColorStderr);
    }

    if (status == QProcess::CrashExit) {
        appendColored("\nProcess killed.\n", kColorSystem);
    } else if (exit_code != 0) {
        appendColored(
            QString("\nExited with code %1\n").arg(exit_code), kColorSystem);
    }

    setBusy(false);
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void TerminalWidget::onKill()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        m_process->kill();
}

void TerminalWidget::onClear()
{
    m_output->clear();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void TerminalWidget::appendColored(const QString& text, const QColor& color)
{
    if (text.isEmpty()) return;

    QTextCursor cur = m_output->textCursor();
    cur.movePosition(QTextCursor::End);

    QTextCharFormat fmt;
    fmt.setForeground(color);
    cur.setCharFormat(fmt);
    cur.insertText(text);

    // Auto-scroll when already near bottom
    QScrollBar* sb = m_output->verticalScrollBar();
    if (sb->value() >= sb->maximum() - 20)
        sb->setValue(sb->maximum());
}

QString TerminalWidget::stripAnsi(const QString& text)
{
    // Matches ESC [ … m  and other CSI sequences
    static const QRegularExpression kAnsiRe(
        "\x1b(?:[@-Z\\\\-_]|\\[[0-?]*[ -/]*[@-~])");
    QString s = text;
    s.remove(kAnsiRe);
    return s;
}

void TerminalWidget::setBusy(bool busy)
{
    m_busy = busy;
    m_input->setEnabled(!busy);
    m_kill_btn->setEnabled(busy);
    if (!busy) {
        m_input->setFocus();
        // Blank separator + prompt line so output from successive commands
        // is visually distinct (single gap, matching real terminal behaviour).
        appendColored("\n" + shellPrompt() + "\n", kColorPrompt);
    }
}

} // namespace dolphin::ui
