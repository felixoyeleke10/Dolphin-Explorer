// ProcessingWindow.cpp — dedicated window for running the node-graph pipeline.
#include "ui/features/processing/ProcessingWindow.h"
#include "ui/shell/Theme.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/services/ProcessingService.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QTextEdit>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {

constexpr int kColName     = 0;
constexpr int kColPipeline = 1;

const char* kStatusNone     = "–";
const char* kStatusBuilding = "…";
const char* kStatusDone     = "✓";
const char* kStatusFailed   = "✗";

} // namespace

// -----------------------------------------------------------------------------

static constexpr int kMinW = 700;
static constexpr int kMinH = 480;

ProcessingWindow::ProcessingWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Processing"));
    setMinimumSize(kMinW, kMinH);
    buildUi();
}

void ProcessingWindow::buildUi()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);

    // -- Toolbar ---------------------------------------------------------------
    auto* toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setIconSize({20, 20});
    addToolBar(Qt::TopToolBarArea, toolbar);

    m_btn_run     = new QPushButton(tr("Run Pipeline"), toolbar);
    m_btn_run_all = new QPushButton(tr("Run All"),      toolbar);

    m_btn_run->setToolTip(tr("Run the node graph pipeline on the selected layers."));
    m_btn_run_all->setToolTip(tr("Run the node graph pipeline on all indexed layers."));

    toolbar->addWidget(m_btn_run);
    toolbar->addWidget(m_btn_run_all);

    connect(m_btn_run,     &QPushButton::clicked, this, &ProcessingWindow::onRunPipeline);
    connect(m_btn_run_all, &QPushButton::clicked, this, &ProcessingWindow::onRunAll);

    // -- Layer list ------------------------------------------------------------
    m_layer_list = new QTreeWidget(this);
    m_layer_list->setRootIsDecorated(false);
    m_layer_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_layer_list->setAlternatingRowColors(true);
    m_layer_list->setColumnCount(2);
    m_layer_list->setHeaderLabels({tr("Layer"), tr("Pipeline")});

    m_layer_list->header()->setSectionResizeMode(kColName,     QHeaderView::Stretch);
    m_layer_list->header()->setSectionResizeMode(kColPipeline, QHeaderView::Fixed);
    m_layer_list->header()->resizeSection(kColPipeline, 64);

    // -- Log area --------------------------------------------------------------
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(140);
    QFont mono = m_log->font();
    mono.setFamily("Consolas");
    mono.setPointSize(9);
    m_log->setFont(mono);

    // -- Status bar ------------------------------------------------------------
    m_progress  = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    m_progress->setMaximumHeight(14);

    m_status_lbl = new QLabel(tr("Ready"), this);

    auto* status_row = new QHBoxLayout;
    status_row->addWidget(m_status_lbl, 1);
    status_row->addWidget(m_progress,   2);

    // -- Layout ----------------------------------------------------------------
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(Theme::kSpacing2, Theme::kSpacing1, Theme::kSpacing2, Theme::kSpacing2);
    layout->setSpacing(Theme::kSpacing1);
    layout->addWidget(m_layer_list, 3);
    layout->addWidget(m_log,        1);
    layout->addLayout(status_row);
}

// -----------------------------------------------------------------------------

void ProcessingWindow::setProject(std::shared_ptr<app::Project> project,
                                   app::ProcessingService*         proc_service)
{
    if (m_proc_service)
        disconnect(m_proc_service, nullptr, this, nullptr);

    m_project      = std::move(project);
    m_proc_service = proc_service;

    if (m_proc_service) {
        connect(m_proc_service, &app::ProcessingService::runStarted,
                this, &ProcessingWindow::onRunStarted);
        connect(m_proc_service, &app::ProcessingService::runComplete,
                this, &ProcessingWindow::onRunComplete);
        connect(m_proc_service, &app::ProcessingService::runFailed,
                this, &ProcessingWindow::onRunFailed);
        connect(m_proc_service, &app::ProcessingService::batchProgress,
                this, &ProcessingWindow::onBatchProgress);
        connect(m_proc_service, &app::ProcessingService::batchComplete,
                this, &ProcessingWindow::onBatchComplete);
    }

    refreshLayerList();
}

// -----------------------------------------------------------------------------

void ProcessingWindow::refreshLayerList()
{
    m_layer_list->clear();
    if (!m_project) return;

    for (const auto& layer : m_project->layers()) {
        if (!layer) continue;

        auto* item = new QTreeWidgetItem(m_layer_list);
        item->setText(kColName, QString::fromStdString(layer->label));
        item->setData(kColName, Qt::UserRole, QString::fromStdString(layer->id));
        item->setText(kColPipeline, kStatusNone);
        item->setTextAlignment(kColPipeline, Qt::AlignCenter);
    }
}

// -----------------------------------------------------------------------------

std::vector<std::string> ProcessingWindow::selectedLayerIds() const
{
    std::vector<std::string> ids;
    for (auto* item : m_layer_list->selectedItems())
        ids.push_back(item->data(kColName, Qt::UserRole).toString().toStdString());
    return ids;
}

void ProcessingWindow::appendLog(const QString& msg)
{
    m_log->append(msg);
}

// -----------------------------------------------------------------------------
//  Button slots
// -----------------------------------------------------------------------------

void ProcessingWindow::onRunPipeline()
{
    if (!m_proc_service || !m_project) return;
    const auto ids = selectedLayerIds();
    if (ids.empty()) {
        appendLog(tr("Select one or more layers first."));
        return;
    }
    for (const auto& id : ids) {
        auto* layer = m_project->findLayer(id);
        if (!layer) continue;
        auto* src = m_project->findSource(layer->source_id);
        const std::string path = src ? src->path : std::string{};
        m_proc_service->runLayer(*m_project, layer, path);
    }
}

void ProcessingWindow::onRunAll()
{
    if (!m_proc_service || !m_project) return;
    m_proc_service->runAll(*m_project);
}

// -----------------------------------------------------------------------------
//  Signal handlers
// -----------------------------------------------------------------------------

void ProcessingWindow::onRunStarted(const std::string& layer_id)
{
    const QString qid = QString::fromStdString(layer_id);
    for (int i = 0; i < m_layer_list->topLevelItemCount(); ++i) {
        auto* item = m_layer_list->topLevelItem(i);
        if (item->data(kColName, Qt::UserRole).toString() == qid)
            item->setText(kColPipeline, kStatusBuilding);
    }
    appendLog(tr("Running pipeline: %1").arg(qid));
    m_progress->setVisible(true);
    m_progress->setRange(0, 0);
}

void ProcessingWindow::onRunComplete(const std::string& layer_id,
                                      const std::string& summary)
{
    const QString qid = QString::fromStdString(layer_id);
    for (int i = 0; i < m_layer_list->topLevelItemCount(); ++i) {
        auto* item = m_layer_list->topLevelItem(i);
        if (item->data(kColName, Qt::UserRole).toString() == qid)
            item->setText(kColPipeline, kStatusDone);
    }
    appendLog(tr("Complete: %1  —  %2")
                  .arg(qid, QString::fromStdString(summary)));
}

void ProcessingWindow::onRunFailed(const std::string& layer_id,
                                    const std::string& error)
{
    const QString qid = QString::fromStdString(layer_id);
    for (int i = 0; i < m_layer_list->topLevelItemCount(); ++i) {
        auto* item = m_layer_list->topLevelItem(i);
        if (item->data(kColName, Qt::UserRole).toString() == qid)
            item->setText(kColPipeline, kStatusFailed);
    }
    appendLog(tr("Failed: %1  —  %2")
                  .arg(qid, QString::fromStdString(error)));
}

void ProcessingWindow::onBatchProgress(int done, int total)
{
    m_progress->setVisible(true);
    m_progress->setRange(0, total);
    m_progress->setValue(done);
    m_status_lbl->setText(tr("Processing %1 / %2…").arg(done).arg(total));
}

void ProcessingWindow::onBatchComplete(int succeeded, int total)
{
    m_progress->setVisible(false);
    m_progress->setRange(0, 100);
    m_status_lbl->setText(tr("Ready"));
    appendLog(tr("Batch complete: %1 / %2 succeeded.").arg(succeeded).arg(total));
}

} // namespace dolphin::ui
