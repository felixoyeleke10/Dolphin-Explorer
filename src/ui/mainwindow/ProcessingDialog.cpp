// ProcessingDialog.cpp — app-wide processing modal (modern dark UI)

#include "ui/mainwindow/ProcessingDialog.h"

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

// -- Stylesheet constants ---------------------------------------------------
static const QString kFrameStyle = QStringLiteral(
    "#dlgFrame {"
    "  background-color: #161616;"
    "  border: 1px solid #2a2a2a;"
    "  border-radius: 10px;"
    "}");

static const QString kLogStyle = QStringLiteral(
    "QTextEdit {"
    "  background-color: #0f0f0f;"
    "  color: #cccccc;"
    "  border: 1px solid #222222;"
    "  border-radius: 6px;"
    "  padding: 8px;"
    "}"
    "QScrollBar:vertical {"
    "  background: #0f0f0f; width: 5px; border: none; margin: 0;"
    "}"
    "QScrollBar::handle:vertical {"
    "  background: #333333; border-radius: 2px; min-height: 20px;"
    "}"
    "QScrollBar::handle:vertical:hover { background: #4d9eff; }"
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; }");

static const QString kCancelStyle = QStringLiteral(
    "QPushButton {"
    "  color: #999; font-size: 12px; font-weight: 500;"
    "  background: transparent;"
    "  border: 1px solid #333;"
    "  border-radius: 5px;"
    "  padding: 0 20px;"
    "}"
    "QPushButton:hover { background: #1f1f1f; border-color: #555; color: #eee; }"
    "QPushButton:pressed { background: #181818; }"
    "QPushButton:disabled { color: #333; border-color: #1e1e1e; }");

static const QString kCloseBtnStyle = QStringLiteral(
    "QPushButton { color: #555; font-size: 13px; background: transparent; border: none; border-radius: 5px; }"
    "QPushButton:hover { color: #fff; background: #c0392b; }");

// --------------------------------------------------------------------------

ProcessingDialog::ProcessingDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(760, 560);

    // -- Outer frame (rounded corners + drop shadow) -----------------------
    auto* frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("dlgFrame"));
    frame->setStyleSheet(kFrameStyle);

    auto* shadow = new QGraphicsDropShadowEffect(frame);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 200));
    shadow->setOffset(0, 6);
    frame->setGraphicsEffect(shadow);

    // -- Header (transparent so frame's border-radius clips it) -----------
    auto* header = new QWidget(frame);
    header->setFixedHeight(52);
    header->setStyleSheet(QStringLiteral(
        "QWidget { background: transparent; border-bottom: 1px solid #242424; }"));

    m_spinner = new QLabel(QStringLiteral("⠋"), header);
    m_spinner->setStyleSheet(QStringLiteral(
        "color: #4d9eff; font-size: 20px; background: transparent; border: none;"));
    m_spinner->setFixedWidth(30);
    m_spinner->setAlignment(Qt::AlignCenter);

    auto* hdr_title = new QLabel(tr("Processing"), header);
    hdr_title->setStyleSheet(QStringLiteral(
        "color: #c0c0c0; font-size: 12px; font-weight: 600; letter-spacing: 1px;"
        "background: transparent; border: none; text-transform: uppercase;"));

    auto* close_btn = new QPushButton(QStringLiteral("✕"), header);
    close_btn->setFixedSize(28, 28);
    close_btn->setFlat(true);
    close_btn->setCursor(Qt::PointingHandCursor);
    close_btn->setFocusPolicy(Qt::NoFocus);
    close_btn->setStyleSheet(kCloseBtnStyle);
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
    m_bar->setRange(0, 0);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(3);
    m_bar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: #1a1a1a; border: none; }"
        "QProgressBar::chunk { background: qlineargradient("
        "  x1:0, y1:0, x2:1, y2:0,"
        "  stop:0 #2d6cdf, stop:1 #4d9eff); }"));

    // -- Status label ------------------------------------------------------
    m_status = new QLabel(tr("Initializing…"), frame);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral(
        "color: #f0f0f0; font-size: 13px; font-weight: 600; background: transparent;"));

    // -- Log area ----------------------------------------------------------
    m_log = new QTextEdit(frame);
    m_log->setReadOnly(true);
    m_log->setFocusPolicy(Qt::NoFocus);
    m_log->setMinimumHeight(380);
    m_log->document()->setDefaultStyleSheet(
        QStringLiteral("body { color: #cccccc; margin: 0; padding: 0; }"));
    QFont lf;
    lf.setFamily(QStringLiteral("Consolas"));
    lf.setPointSize(9);
    lf.setStyleHint(QFont::Monospace);
    m_log->setFont(lf);
    m_log->setStyleSheet(kLogStyle);

    // -- Cancel button -----------------------------------------------------
    m_cancel = new QPushButton(tr("Cancel"), frame);
    m_cancel->setFixedHeight(34);
    m_cancel->setMinimumWidth(96);
    m_cancel->setCursor(Qt::PointingHandCursor);
    m_cancel->setFocusPolicy(Qt::NoFocus);
    m_cancel->setStyleSheet(kCancelStyle);
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

    auto* frame_lay = new QVBoxLayout(frame);
    frame_lay->setContentsMargins(0, 0, 0, 0);
    frame_lay->setSpacing(0);
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
        QStringLiteral("<span style='color:#333333'>[%1]</span>&nbsp;&nbsp;%2")
        .arg(ts, msgHtml));
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
    appendLog(QStringLiteral("<span style='color:#d0d0d0'>") + label.toHtmlEscaped() + QStringLiteral("</span>"));
}

void ProcessingDialog::taskStep(const QString& id, const QString& label)
{
    if (!m_tasks.contains(id)) return;
    m_tasks[id].label = label;
    m_status->setText(label);
    appendLog(
        QStringLiteral("<span style='color:#666666'>&nbsp;&nbsp;↳&nbsp;</span>")
        + QStringLiteral("<span style='color:#999999'>") + label.toHtmlEscaped() + QStringLiteral("</span>"));
}

void ProcessingDialog::taskDone(const QString& id)
{
    if (!m_tasks.contains(id) || !m_tasks[id].active) return;
    m_tasks[id].active = false;
    --m_active_count;

    appendLog(QStringLiteral("<span style='color:#4caf50'>✓&nbsp;&nbsp;Done</span>"));

    if (m_active_count == 0) {
        m_status->setText(tr("Complete"));
        m_bar->setVisible(false);
        m_cancel->setEnabled(false);
        m_spin_timer->stop();
        m_spinner->setText(QStringLiteral("✓"));
        m_spinner->setStyleSheet(QStringLiteral(
            "color: #4caf50; font-size: 18px; background: transparent; border: none;"));
        QTimer::singleShot(400, this, &QDialog::accept);
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
        ? QStringLiteral("<span style='color:#f44336'>✗&nbsp;&nbsp;Failed</span>")
        : QStringLiteral("<span style='color:#f44336'>✗&nbsp;&nbsp;Failed:&nbsp;</span>")
          + QStringLiteral("<span style='color:#e57373'>") + error.toHtmlEscaped() + QStringLiteral("</span>");
    appendLog(msg);

    if (m_active_count == 0) {
        m_status->setText(tr("Failed"));
        m_bar->setVisible(false);
        m_cancel->setEnabled(false);
        m_spin_timer->stop();
        m_spinner->setText(QStringLiteral("✗"));
        m_spinner->setStyleSheet(QStringLiteral(
            "color: #f44336; font-size: 18px; background: transparent; border: none;"));
        QTimer::singleShot(2000, this, &QDialog::accept);
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
    m_spinner->setStyleSheet(QStringLiteral(
        "color: #ff9800; font-size: 18px; background: transparent; border: none;"));
    appendLog(QStringLiteral("<span style='color:#ff9800'>⊘&nbsp;&nbsp;Cancelled by user</span>"));
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
        appendLog(QStringLiteral("<span style='color:#ff9800'>⊘&nbsp;&nbsp;Cancelled by user</span>"));
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
