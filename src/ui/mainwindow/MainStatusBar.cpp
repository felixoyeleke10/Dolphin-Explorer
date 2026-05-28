// MainStatusBar.cpp — modular status bar implementation.
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shell/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStyle>
#include <QStyleFactory>
#include <QTimer>
#include <QWidget>

namespace dolphin::ui {

static constexpr int kAiDotSz = 7;  // AI status indicator dot size

MainStatusBar::MainStatusBar(QWidget* parent)
    : QStatusBar(parent)
{
    // ── Progress bar ──────────────────────────────────────────────────────────
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setFixedWidth(Theme::kMainProgressBarW);
    m_progress->setFixedHeight(Theme::kProgressBarH);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);
    // Fusion style required for indeterminate animation on Windows (native style ignores QSS).
    m_progress->setStyle(QStyleFactory::create("Fusion"));

    // ── Context label (project · active layer) ────────────────────────────────
    m_context = new QLabel(tr("No project"), this);
    m_context->setObjectName("statusChrome");

    // ── Transient job message ─────────────────────────────────────────────────
    m_job = new QLabel(this);
    m_job->setObjectName("statusChrome");

    m_job_timer = new QTimer(this);
    m_job_timer->setSingleShot(true);
    m_job_timer->setInterval(8000);
    connect(m_job_timer, &QTimer::timeout, this, [this]() { m_job->clear(); });

    // ── Permanent cursor data ─────────────────────────────────────────────────
    m_range = new QLabel(this);
    m_range->setObjectName("statusChrome");

    m_depth = new QLabel(this);
    m_depth->setObjectName("statusChrome");

    m_pos = new QLabel(this);
    m_pos->setObjectName("statusData");

    // ── AI provider indicator ─────────────────────────────────────────────────
    m_ai_widget = new QWidget(this);
    m_ai_widget->setObjectName("statusAiSection");
    auto* ai_layout = new QHBoxLayout(m_ai_widget);
    ai_layout->setContentsMargins(Theme::kSpacing1, 0, Theme::kSpacing1, 0);
    ai_layout->setSpacing(Theme::kSpacing1);

    m_ai_icon = new QLabel(m_ai_widget);
    m_ai_icon->setObjectName("aiIcon");

    m_ai_dot = new QLabel(m_ai_widget);
    m_ai_dot->setObjectName("aiDot");
    m_ai_dot->setFixedSize(kAiDotSz, kAiDotSz);

    ai_layout->addWidget(m_ai_icon);
    ai_layout->addWidget(m_ai_dot);
    m_ai_widget->hide();

    // ── Assemble left → right ─────────────────────────────────────────────────
    addWidget(m_progress);
    addWidget(m_context, 1);
    addWidget(m_job);

    addPermanentWidget(m_range);
    addPermanentWidget(m_depth);
    addPermanentWidget(m_pos);
    addPermanentWidget(m_ai_widget);
}

// ── Context ───────────────────────────────────────────────────────────────────

void MainStatusBar::setProjectContext(const QString& project, const QString& layer)
{
    QString text = project;
    if (!layer.isEmpty())
        text += "  ·  " + layer;
    m_context->setText(text);
}

void MainStatusBar::clearContext()
{
    m_context->setText(tr("No project"));
}

// ── Job messages ──────────────────────────────────────────────────────────────

void MainStatusBar::showJobMessage(const QString& msg, int timeout_ms)
{
    m_job->setText(msg);
    m_job_timer->setInterval(timeout_ms);
    m_job_timer->start();
}

// ── Cursor data ───────────────────────────────────────────────────────────────

void MainStatusBar::setCursorRange(const QString& side_label, float metres)
{
    m_range->setText(QString("%1  %2 m").arg(side_label).arg(metres, 0, 'f', 1));
}

void MainStatusBar::clearCursorRange()
{
    m_range->clear();
}

void MainStatusBar::setCursorDepth(float metres)
{
    m_depth->setText(QString("~%1 m depth").arg(metres, 0, 'f', 0));
}

void MainStatusBar::clearCursorDepth()
{
    m_depth->clear();
}

void MainStatusBar::setCursorPosition(double lat_or_y, double lon_or_x, bool is_projected)
{
    m_pos->setText(formatPosition(lat_or_y, lon_or_x, is_projected));
}

void MainStatusBar::clearCursorPosition()
{
    m_pos->clear();
}

void MainStatusBar::clearCursorData()
{
    m_range->clear();
    m_depth->clear();
    m_pos->clear();
}

// ── Progress ──────────────────────────────────────────────────────────────────

void MainStatusBar::setProgressIndeterminate()
{
    m_progress->setRange(0, 0);
    m_progress->setVisible(true);
}

void MainStatusBar::setProgress(int percent, bool visible)
{
    m_progress->setRange(0, 100);
    m_progress->setValue(percent);
    m_progress->setVisible(visible);
}

void MainStatusBar::hideProgress()
{
    m_progress->setRange(0, 100);
    m_progress->setVisible(false);
}

// ── AI indicator ──────────────────────────────────────────────────────────────

void MainStatusBar::setAiProvider(AiProvider provider)
{
    if (m_ai_provider == provider) return;
    m_ai_provider = provider;
    rebuildAiSection();
}

void MainStatusBar::setAiStatus(AiStatus status)
{
    if (m_ai_status == status) return;
    m_ai_status = status;
    rebuildAiSection();
}

void MainStatusBar::rebuildAiSection()
{
    if (m_ai_provider == AiProvider::None) {
        m_ai_widget->hide();
        return;
    }

    if (m_ai_provider == AiProvider::Primary) {
        m_ai_icon->setText(QStringLiteral("✦"));   // ✦ BLACK FOUR POINTED STAR
        m_ai_icon->setProperty("aiProvider", QStringLiteral("primary"));
    } else {
        m_ai_icon->setText(QStringLiteral("⊚"));   // ⊚ CIRCLED RING OPERATOR
        m_ai_icon->setProperty("aiProvider", QStringLiteral("integration"));
    }
    m_ai_icon->setToolTip(tr("AI Assistant"));

    const char* status_str;
    const char* dot_tip;
    switch (m_ai_status) {
        case AiStatus::Offline: status_str = "offline"; dot_tip = QT_TR_NOOP("Offline"); break;
        case AiStatus::Ready:   status_str = "ready";   dot_tip = QT_TR_NOOP("Ready");   break;
        case AiStatus::Active:  status_str = "active";  dot_tip = QT_TR_NOOP("Active");  break;
    }
    m_ai_dot->setProperty("aiStatus", QLatin1String(status_str));
    m_ai_dot->setToolTip(tr(dot_tip));

    // Force QSS re-evaluation after dynamic property changes.
    auto repolish = [](QWidget* w) {
        w->style()->unpolish(w);
        w->style()->polish(w);
    };
    repolish(m_ai_icon);
    repolish(m_ai_dot);

    m_ai_widget->show();
}

} // namespace dolphin::ui
