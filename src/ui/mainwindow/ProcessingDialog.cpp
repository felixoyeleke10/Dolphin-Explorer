// ProcessingDialog.cpp — app-wide processing modal (modern dark UI)

#include "ui/mainwindow/ProcessingDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QCloseEvent>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <iterator>

namespace dolphin::ui {

static const char* kSpinFrames[] = {
    "⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"
};
static constexpr int kSpinCount = static_cast<int>(std::size(kSpinFrames));

// HTML colour strings for rich-text log entries — values from Theme tokens.
namespace {
constexpr const char* kHtmlTs   = Theme::kTextDisabled;  // timestamp brackets
constexpr const char* kHtmlMsg  = Theme::kTextSecond;    // task-begin body text
constexpr const char* kHtmlSep  = Theme::kTextMuted;     // "  ↳ " separator glyph
constexpr const char* kHtmlStep = Theme::kTextSoft;      // step body text
constexpr const char* kHtmlOk   = Theme::kSuccess;       // ✓
constexpr const char* kHtmlErr  = Theme::kDangerBright;  // ✗ header
constexpr const char* kHtmlErrD = Theme::kDanger;        // ✗ detail line
constexpr const char* kHtmlCanc = Theme::kWarning;       // ⊘
} // namespace

// --------------------------------------------------------------------------

ProcessingDialog::ProcessingDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(760, 560);

    // -- Outer frame (rounded corners + drop shadow) -----------------------
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("dlgProcessingFrame"));

    auto* shadow = new QGraphicsDropShadowEffect(frame);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 200));
    shadow->setOffset(0, 6);
    frame->setGraphicsEffect(shadow);

    // -- Header (transparent so frame's border-radius clips it) -----------
    auto* header = new QWidget(frame);
    header->setObjectName(QStringLiteral("procHeader"));
    header->setFixedHeight(52);

    m_spinner = new QLabel(QStringLiteral("⠋"), header);
    m_spinner->setObjectName(QStringLiteral("procSpinner"));
    m_spinner->setFixedWidth(30);
    m_spinner->setAlignment(Qt::AlignCenter);

    auto* hdr_title = new QLabel(tr("Processing"), header);
    hdr_title->setObjectName(QStringLiteral("procTitle"));

    auto* close_btn = new QPushButton(QStringLiteral("✕"), header);
    close_btn->setObjectName(QStringLiteral("procCloseBtn"));
    close_btn->setFixedSize(28, 28);
    close_btn->setFlat(true);
    close_btn->setCursor(Qt::PointingHandCursor);
    close_btn->setFocusPolicy(Qt::NoFocus);
    connect(close_btn, &QPushButton::clicked, this, &ProcessingDialog::onCancelClicked);

    auto* hdr_lay = new QHBoxLayout(header);
    hdr_lay->setContentsMargins(14, 0, 10, 0);
    hdr_lay->setSpacing(10);
    hdr_lay->addWidget(m_spinner);
    hdr_lay->addWidget(hdr_title);
    hdr_lay->addStretch();
    hdr_lay->addWidget(close_btn);

    // -- Thin accent progress strip -----------------------------------------
    m_bar = new QProgressBar(frame);
    m_bar->setObjectName(QStringLiteral("procBar"));
    m_bar->setRange(0, 0);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(3);

    // -- Status label ------------------------------------------------------
    m_status = new QLabel(tr("Initializing…"), frame);
    m_status->setObjectName(QStringLiteral("procStatus"));
    m_status->setWordWrap(true);

    // -- Log area ----------------------------------------------------------
    m_log = new QTextEdit(frame);
    m_log->setObjectName(QStringLiteral("procLog"));
    m_log->setReadOnly(true);
    m_log->setFocusPolicy(Qt::NoFocus);
    m_log->setMinimumHeight(380);
    m_log->document()->setDefaultStyleSheet(
        QStringLiteral("body { color: %1; margin: 0; padding: 0; }").arg(QLatin1String(kHtmlMsg)));
    QFont lf;
    lf.setFamily(QStringLiteral("Consolas"));
    lf.setPointSize(9);
    lf.setStyleHint(QFont::Monospace);
    m_log->setFont(lf);

    // -- Cancel button -----------------------------------------------------
    m_cancel = new QPushButton(tr("Cancel"), frame);
    m_cancel->setObjectName(QStringLiteral("procCancel"));
    m_cancel->setFixedHeight(34);
    m_cancel->setMinimumWidth(96);
    m_cancel->setCursor(Qt::PointingHandCursor);
    m_cancel->setFocusPolicy(Qt::NoFocus);
    connect(m_cancel, &QPushButton::clicked, this, &ProcessingDialog::onCancelClicked);

    auto* btn_row = new QHBoxLayout;
    btn_row->addStretch();
    btn_row->addWidget(m_cancel);

    // -- Frame layout ------------------------------------------------------
    auto* content = new QVBoxLayout;
    content->setContentsMargins(16, 14, 16, 14);
    content->setSpacing(12);
    content->addWidget(m_status);
    content->addWidget(m_log);
    content->addLayout(btn_row);

    auto* frame_lay = makeCompactLayout<QVBoxLayout>(frame);
    frame_lay->addWidget(header);
    frame_lay->addWidget(m_bar);
    frame_lay->addLayout(content);

    // -- Dialog outer (margin = shadow bleed space) ------------------------
    // Must be >= blurRadius on L/R/Top and >= blurRadius + |offsetY| on Bottom.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 28, 28, 34);
    outer->addWidget(frame);

    // -- Spinner timer -----------------------------------------------------
    m_spin_timer = new QTimer(this);
    m_spin_timer->setInterval(80);
    connect(m_spin_timer, &QTimer::timeout, this, &ProcessingDialog::tickSpinner);
    m_spin_timer->start();
}

// -- Spinner ----------------------------------------------------------------

void ProcessingDialog::tickSpinner()
{
    m_spin_frame = (m_spin_frame + 1) % kSpinCount;
    m_spinner->setText(QString::fromUtf8(kSpinFrames[m_spin_frame]));
}

// -- Log --------------------------------------------------------------------

void ProcessingDialog::appendLog(const QString& msgHtml)
{
    const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    m_log->append(
        QStringLiteral("<span style='color:%1'>[%2]</span>&nbsp;&nbsp;%3")
        .arg(QLatin1String(kHtmlTs), ts, msgHtml));
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

// -- Task lifecycle ---------------------------------------------------------

void ProcessingDialog::taskBegin(const QString& id, const QString& label)
{
    if (m_tasks.contains(id)) {
        if (!m_tasks[id].active) {
            m_tasks[id].active = true;
            ++m_active_count;
        }
        m_tasks[id].label = label;
    } else {
        m_tasks[id] = {label, true};
        ++m_active_count;
    }
    m_status->setText(label);
    m_bar->setVisible(true);
    m_cancel->setEnabled(true);
    m_spin_timer->start();
    appendLog(QStringLiteral("<span style='color:%1'>%2</span>")
        .arg(QLatin1String(kHtmlMsg), label.toHtmlEscaped()));
}

void ProcessingDialog::taskDone(const QString& id)
{
    if (!m_tasks.contains(id) || !m_tasks[id].active) return;
    m_tasks[id].active = false;
    --m_active_count;

    appendLog(QStringLiteral("<span style='color:%1'>✓&nbsp;&nbsp;Done</span>")
        .arg(QLatin1String(kHtmlOk)));

    if (m_active_count == 0) {
        m_status->setText(tr("Complete"));
        m_bar->setVisible(false);
        m_cancel->setEnabled(false);
        m_spin_timer->stop();
        m_spinner->setText(QStringLiteral("✓"));
        m_spinner->setStyleSheet(
            QStringLiteral("color: %1; font-size: 18px; background: transparent; border: none;")
                .arg(QLatin1String(Theme::kSuccess)));
        // Guard against a new task starting within the 400ms window: only
        // accept() if m_active_count is still 0 when the timer fires.
        QTimer::singleShot(400, this, [this] { if (m_active_count == 0) accept(); });
    } else {
        for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it)
            if (it.value().active) { m_status->setText(it.value().label); break; }
    }
}

void ProcessingDialog::taskFail(const QString& id, const QString& error)
{
    if (!m_tasks.contains(id) || !m_tasks[id].active) return;
    m_tasks[id].active = false;
    --m_active_count;

    const QString msg = error.isEmpty()
        ? QStringLiteral("<span style='color:%1'>✗&nbsp;&nbsp;Failed</span>")
              .arg(QLatin1String(kHtmlErr))
        : QStringLiteral("<span style='color:%1'>✗&nbsp;&nbsp;Failed:&nbsp;</span>"
                         "<span style='color:%2'>%3</span>")
              .arg(QLatin1String(kHtmlErr), QLatin1String(kHtmlErrD), error.toHtmlEscaped());
    appendLog(msg);

    if (m_active_count == 0) {
        m_status->setText(tr("Failed"));
        m_bar->setVisible(false);
        m_cancel->setEnabled(false);
        m_spin_timer->stop();
        m_spinner->setText(QStringLiteral("✗"));
        m_spinner->setStyleSheet(
            QStringLiteral("color: %1; font-size: 18px; background: transparent; border: none;")
                .arg(QLatin1String(Theme::kDangerBright)));
        QTimer::singleShot(2000, this, [this] { if (m_active_count == 0) accept(); });
    } else {
        for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it)
            if (it.value().active) { m_status->setText(it.value().label); break; }
    }
}

// -- Cancel / close ---------------------------------------------------------

void ProcessingDialog::onCancelClicked()
{
    m_cancel->setEnabled(false);
    m_spin_timer->stop();
    m_spinner->setText(QStringLiteral("⊘"));
    m_spinner->setStyleSheet(
        QStringLiteral("color: %1; font-size: 18px; background: transparent; border: none;")
            .arg(QLatin1String(Theme::kWarning)));
    appendLog(QStringLiteral("<span style='color:%1'>⊘&nbsp;&nbsp;Cancelled by user</span>")
        .arg(QLatin1String(kHtmlCanc)));
    emit cancelRequested();
    accept();
}

void ProcessingDialog::closeEvent(QCloseEvent* ev)
{
    if (m_active_count > 0) {
        m_spin_timer->stop();
        m_cancel->setEnabled(false);
        for (auto& task : m_tasks) task.active = false;
        m_active_count = 0;
        appendLog(QStringLiteral("<span style='color:%1'>⊘&nbsp;&nbsp;Cancelled by user</span>")
            .arg(QLatin1String(kHtmlCanc)));
        emit cancelRequested();
    }
    ev->accept();
}

// -- Drag to move (frameless window) ---------------------------------------

void ProcessingDialog::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        m_dragging = true;
        m_drag_pos = ev->globalPosition().toPoint() - frameGeometry().topLeft();
        ev->accept();
    }
}

void ProcessingDialog::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_dragging && (ev->buttons() & Qt::LeftButton)) {
        move(ev->globalPosition().toPoint() - m_drag_pos);
        ev->accept();
    }
}

void ProcessingDialog::mouseReleaseEvent(QMouseEvent* ev)
{
    m_dragging = false;
    ev->accept();
}

} // namespace dolphin::ui
