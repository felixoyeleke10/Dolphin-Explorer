// BottomDockPanel.OutputJobs.cpp — Output, Jobs, Terminal tabs + their slots.
#include "ui/bottom/BottomDockPanel.h"
#include "ui/bottom/TerminalWidget.h"
#include "ui/shell/Theme.h"

#include <QVBoxLayout>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>

namespace dolphin::ui {

using namespace Theme;

void BottomDockPanel::buildOutputTab(QWidget* parent)
{
    m_out_edit = new QPlainTextEdit;
    m_out_edit->setObjectName("panelOutput");
    m_out_edit->setReadOnly(true);
    m_out_edit->setMaximumBlockCount(2000);
    m_out_edit->setLineWrapMode(QPlainTextEdit::NoWrap);

    m_out_edit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_out_edit, &QPlainTextEdit::customContextMenuRequested,
            this, [this](const QPoint& pos) {
                const bool has_sel = m_out_edit->textCursor().hasSelection();
                QMenu menu(m_out_edit);
                auto* act_copy = menu.addAction(tr("Copy"));
                act_copy->setEnabled(has_sel);
                connect(act_copy, &QAction::triggered,
                        m_out_edit, &QPlainTextEdit::copy);
                menu.addAction(tr("Select All"), m_out_edit, &QPlainTextEdit::selectAll);
                menu.addSeparator();
                auto* act_clear = menu.addAction(tr("Clear"));
                connect(act_clear, &QAction::triggered,
                        m_out_edit, &QPlainTextEdit::clear);
                menu.exec(m_out_edit->viewport()->mapToGlobal(pos));
            });

    if (auto* s = qobject_cast<QStackedWidget*>(parent))
        s->addWidget(m_out_edit);
}

void BottomDockPanel::buildJobsTab(QWidget* parent)
{
    m_job_list = new QListWidget;
    m_job_list->setObjectName("panelJobList");
    m_job_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_job_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    if (auto* s = qobject_cast<QStackedWidget*>(parent))
        s->addWidget(m_job_list);

    auto* copy_sc = new QShortcut(QKeySequence::Copy, m_job_list);
    copy_sc->setContext(Qt::WidgetShortcut);
    connect(copy_sc, &QShortcut::activated, this, [this]() {
        if (auto* item = m_job_list->currentItem())
            QApplication::clipboard()->setText(item->text());
    });

    m_job_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_job_list, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
                auto* item = m_job_list->itemAt(pos);
                QMenu menu(m_job_list);
                auto* act_copy = menu.addAction(tr("Copy"));
                act_copy->setEnabled(item != nullptr);
                connect(act_copy, &QAction::triggered, this, [item]() {
                    if (item) QApplication::clipboard()->setText(item->text());
                });
                menu.exec(m_job_list->viewport()->mapToGlobal(pos));
            });
}

void BottomDockPanel::buildTerminalTab(QWidget* parent)
{
    m_terminal = new TerminalWidget;

    if (auto* s = qobject_cast<QStackedWidget*>(parent))
        s->addWidget(m_terminal);
}

void BottomDockPanel::setWorkingDirectory(const QString& dir)
{
    if (m_terminal)
        m_terminal->setWorkingDirectory(dir);
}

void BottomDockPanel::buildChatTab(QWidget* parent)
{
    // Empty host — MainWindow injects the PanelChatWidget via setChatWidget()
    // (the chat lives in the mainwindow lib, which this layer must not link).
    m_chat_host = new QWidget;
    auto* l = new QVBoxLayout(m_chat_host);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);

    if (auto* s = qobject_cast<QStackedWidget*>(parent))
        s->addWidget(m_chat_host);
}

void BottomDockPanel::setChatWidget(QWidget* w)
{
    if (!m_chat_host || !w) return;
    w->setParent(m_chat_host);
    m_chat_host->layout()->addWidget(w);
}

void BottomDockPanel::populateFromHub()
{
    if (!m_hub) return;

    // Problems — replay using the same slot so formatting stays in one place.
    for (const auto& p : m_hub->problems())
        onProblemPosted(p);

    // Output — use stored timestamps rather than current time.
    for (const auto& e : m_hub->outputs())
        m_out_edit->appendPlainText(e.timestamp.toString("hh:mm:ss") + "  " + e.msg);
    if (!m_hub->outputs().isEmpty()) {
        auto* sb = m_out_edit->verticalScrollBar();
        sb->setValue(sb->maximum());
    }

    // Jobs — rebuildJobsTab already iterates hub->jobs().
    rebuildJobsTab();
    updateBadge(2, m_hub->activeJobCount(), false);
}

void BottomDockPanel::onOutputLogged(const QString& msg)
{
    const QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_out_edit->appendPlainText(ts + "  " + msg);
    auto* sb = m_out_edit->verticalScrollBar();
    if (sb->value() >= sb->maximum() - 4)
        sb->setValue(sb->maximum());
}

// Returns active batches + running standalone jobs (batch_id==0).
// Avoids double-counting batch child jobs that are also active jobs.
static int activeDisplayCount(const DiagnosticsHub* hub)
{
    int n = hub->activeBatchCount();
    for (const auto& j : hub->jobs())
        if ((j.status == DiagnosticsHub::JobStatus::Running
             || j.status == DiagnosticsHub::JobStatus::Queued) && j.batch_id == 0)
            ++n;
    return n;
}

void BottomDockPanel::onJobChanged(uint32_t /*id*/)
{
    scheduleJobsRefresh();
}

void BottomDockPanel::onBatchChanged(uint32_t /*id*/)
{
    scheduleJobsRefresh();
}

void BottomDockPanel::scheduleJobsRefresh()
{
    // Collapse a burst of job/batch changes (e.g. 12 lines flipping queued→running→
    // done, plus staged upgrades) into a single rebuild on the next tick, so the
    // O(jobs) rebuild can't monopolise the UI thread and freeze the panel/dialog.
    if (m_jobs_refresh_pending) return;
    m_jobs_refresh_pending = true;
    QTimer::singleShot(40, this, [this]() {
        m_jobs_refresh_pending = false;
        updateBadge(2, activeDisplayCount(m_hub), false);
        if (m_active_tab == 2 && !m_collapsed)
            rebuildJobsTab();
    });
}

void BottomDockPanel::rebuildJobsTab()
{
    m_job_list->clear();

    // Batch summaries — shown above standalone jobs so in-progress batches are visible.
    for (const auto& b : m_hub->batches()) {
        QString prefix;
        QColor  col;
        switch (b.state) {
            case DiagnosticsHub::BatchState::Running:
                prefix = QStringLiteral("[BATCH] ");
                col    = QColor(kAccent);
                break;
            case DiagnosticsHub::BatchState::Completed:
                prefix = QStringLiteral("[BATCH DONE] ");
                col    = QColor(kSuccess);
                break;
            case DiagnosticsHub::BatchState::CompletedWithErrors:
                prefix = QStringLiteral("[BATCH PARTIAL] ");
                col    = QColor(kWarning);
                break;
            case DiagnosticsHub::BatchState::Failed:
                prefix = QStringLiteral("[BATCH FAILED] ");
                col    = QColor(kDanger);
                break;
            case DiagnosticsHub::BatchState::Cancelled:
                prefix = QStringLiteral("[BATCH CANCELLED] ");
                col    = QColor(kTextMuted);
                break;
        }
        QString text = prefix + b.name;
        if (b.state == DiagnosticsHub::BatchState::Running)
            text += QString("  %1/%2").arg(b.done).arg(b.total);
        else
            text += QString("  %1/%2").arg(b.succeeded).arg(b.total);

        auto* item = new QListWidgetItem(text, m_job_list);
        item->setForeground(col);
        if (b.started.isValid())
            item->setToolTip("Started: " + b.started.toString("hh:mm:ss"));
    }

    // Individual jobs (standalone and batch children interleaved by insertion order).
    for (const auto& j : m_hub->jobs()) {
        const char* status_label =
            (j.status == DiagnosticsHub::JobStatus::Queued)     ? "[QUEUED] "  :
            (j.status == DiagnosticsHub::JobStatus::Running)    ? "[RUNNING] " :
            (j.status == DiagnosticsHub::JobStatus::Completed)  ? "[DONE] "    :
            (j.status == DiagnosticsHub::JobStatus::Failed)     ? "[FAILED] "  :
                                                                   "[CANCELLED] ";
        const QColor status_col =
            (j.status == DiagnosticsHub::JobStatus::Queued)     ? QColor(kTextMuted) :
            (j.status == DiagnosticsHub::JobStatus::Running)    ? QColor(kAccent)  :
            (j.status == DiagnosticsHub::JobStatus::Completed)  ? QColor(kSuccess) :
            (j.status == DiagnosticsHub::JobStatus::Failed)     ? QColor(kDanger)  :
                                                                   QColor(kTextMuted);

        QString text = QString::fromLatin1(status_label) + j.name;
        if (!j.layer_id.isEmpty())
            text += "  -  " + j.layer_id;
        if (!j.detail.isEmpty())
            text += "  - " + j.detail;
        if (j.status == DiagnosticsHub::JobStatus::Running && j.progress >= 0.f)
            text += QString("  %1%").arg(static_cast<int>(j.progress * 100));

        auto* item = new QListWidgetItem(text, m_job_list);
        item->setForeground(status_col);
        const QString ts = j.started.isValid()
            ? j.started.toString("hh:mm:ss")
            : QString{};
        item->setToolTip("Started: " + ts);
    }
}

} // namespace dolphin::ui
