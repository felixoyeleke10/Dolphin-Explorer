// ProcessingWindow.cpp — dedicated window for pipeline execution and display
// cache pre-building.  Keeps all heavy processing out of the view windows.
#include "ui/features/processing/ProcessingWindow.h"
#include "ui/shell/Theme.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
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
#include <QSplitter>
#include <QTextEdit>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {

// Quality tier column indices — Columns: Name | Coverage | Low | Med | High | Full | Pipeline
constexpr int kColName     = 0;
constexpr int kColCoverage = 1;
constexpr int kColLow      = 2;
constexpr int kColMed      = 3;
constexpr int kColHigh     = 4;
constexpr int kColFull     = 5;
constexpr int kColPipeline = 6;

// Emoji-style status indicators — plain ASCII fallbacks used instead of icons
// so no resource file is needed.
const char* kStatusNone     = "–";
const char* kStatusBuilding = "…";
const char* kStatusDone     = "✓";
const char* kStatusFailed   = "✗";

MapSonarQuality qualityForColumn(int col)
{
    switch (col) {
    case kColCoverage: return MapSonarQuality::CoverageOnly;
    case kColLow:      return MapSonarQuality::Low;
    case kColMed:      return MapSonarQuality::Medium;
    case kColHigh:     return MapSonarQuality::High;
    case kColFull:     return MapSonarQuality::Full;
    default:           return MapSonarQuality::Off;
    }
}

int columnForQuality(MapSonarQuality q)
{
    switch (q) {
    case MapSonarQuality::CoverageOnly: return kColCoverage;
    case MapSonarQuality::Low:          return kColLow;
    case MapSonarQuality::Medium:       return kColMed;
    case MapSonarQuality::High:         return kColHigh;
    case MapSonarQuality::Full:         return kColFull;
    default:                            return -1;
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

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

    // ── Toolbar ───────────────────────────────────────────────────────────────
    auto* toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setIconSize({20, 20});
    addToolBar(Qt::TopToolBarArea, toolbar);

    m_btn_build   = new QPushButton(tr("Build Previews"), toolbar);
    m_btn_run     = new QPushButton(tr("Run Pipeline"),   toolbar);
    m_btn_run_all = new QPushButton(tr("Run All"),        toolbar);

    m_btn_build->setToolTip(
        tr("Pre-build map preview images at all quality tiers for the selected "
           "layers so palette and quality changes become instant."));
    m_btn_run->setToolTip(tr("Run the node graph pipeline on the selected layers."));
    m_btn_run_all->setToolTip(tr("Run the node graph pipeline on all indexed layers."));

    toolbar->addWidget(m_btn_build);
    toolbar->addWidget(m_btn_run);
    toolbar->addWidget(m_btn_run_all);

    connect(m_btn_build,   &QPushButton::clicked, this, &ProcessingWindow::onBuildPreviews);
    connect(m_btn_run,     &QPushButton::clicked, this, &ProcessingWindow::onRunPipeline);
    connect(m_btn_run_all, &QPushButton::clicked, this, &ProcessingWindow::onRunAll);

    // ── Layer list ────────────────────────────────────────────────────────────
    m_layer_list = new QTreeWidget(this);
    m_layer_list->setRootIsDecorated(false);
    m_layer_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_layer_list->setAlternatingRowColors(true);
    m_layer_list->setColumnCount(7);
    m_layer_list->setHeaderLabels(
        {tr("Layer"), tr("Cov"), tr("Low"), tr("Med"), tr("High"), tr("Full"), tr("Pipeline")});

    // Stretch Name column; fixed width for status columns.
    m_layer_list->header()->setSectionResizeMode(kColName,     QHeaderView::Stretch);
    m_layer_list->header()->setSectionResizeMode(kColCoverage, QHeaderView::Fixed);
    m_layer_list->header()->setSectionResizeMode(kColLow,      QHeaderView::Fixed);
    m_layer_list->header()->setSectionResizeMode(kColMed,      QHeaderView::Fixed);
    m_layer_list->header()->setSectionResizeMode(kColHigh,     QHeaderView::Fixed);
    m_layer_list->header()->setSectionResizeMode(kColFull,     QHeaderView::Fixed);
    m_layer_list->header()->setSectionResizeMode(kColPipeline, QHeaderView::Fixed);
    for (int c = kColCoverage; c <= kColPipeline; ++c)
        m_layer_list->header()->resizeSection(c, 48);

    // ── Log area ──────────────────────────────────────────────────────────────
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(140);
    QFont mono = m_log->font();
    mono.setFamily("Consolas");
    mono.setPointSize(9);
    m_log->setFont(mono);

    // ── Status bar ────────────────────────────────────────────────────────────
    m_progress  = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    m_progress->setMaximumHeight(14);

    m_status_lbl = new QLabel(tr("Ready"), this);

    auto* status_row = new QHBoxLayout;
    status_row->addWidget(m_status_lbl, 1);
    status_row->addWidget(m_progress,   2);

    // ── Layout ────────────────────────────────────────────────────────────────
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(Theme::kSpacing2, Theme::kSpacing1, Theme::kSpacing2, Theme::kSpacing2);
    layout->setSpacing(Theme::kSpacing1);
    layout->addWidget(m_layer_list, 3);
    layout->addWidget(m_log,        1);
    layout->addLayout(status_row);
}

// ─────────────────────────────────────────────────────────────────────────────

void ProcessingWindow::setProject(std::shared_ptr<app::Project> project,
                                   app::ProcessingService*         proc_service,
                                   SidescanViewController*         sss_ctrl)
{
    // Disconnect old connections.
    if (m_proc_service) {
        disconnect(m_proc_service, nullptr, this, nullptr);
    }
    if (m_sss_ctrl) {
        disconnect(m_sss_ctrl, nullptr, this, nullptr);
    }

    m_project      = std::move(project);
    m_proc_service = proc_service;
    m_sss_ctrl     = sss_ctrl;

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

    if (m_sss_ctrl) {
        connect(m_sss_ctrl, &SidescanViewController::prebuildTierComplete,
                this, &ProcessingWindow::onPrebuildTierComplete);
    }

    refreshLayerList();
}

// ─────────────────────────────────────────────────────────────────────────────

void ProcessingWindow::refreshLayerList()
{
    m_layer_list->clear();
    if (!m_project) return;

    for (const auto& layer : m_project->layers()) {
        if (!layer) continue;

        auto* item = new QTreeWidgetItem(m_layer_list);
        item->setText(kColName, QString::fromStdString(layer->label));
        item->setData(kColName, Qt::UserRole, QString::fromStdString(layer->id));

        // Show "–" for all quality tier columns by default.
        for (int c = kColCoverage; c <= kColPipeline; ++c)
            item->setText(c, kStatusNone);
        item->setTextAlignment(kColCoverage, Qt::AlignCenter);
        item->setTextAlignment(kColLow,      Qt::AlignCenter);
        item->setTextAlignment(kColMed,      Qt::AlignCenter);
        item->setTextAlignment(kColHigh,     Qt::AlignCenter);
        item->setTextAlignment(kColFull,     Qt::AlignCenter);
        item->setTextAlignment(kColPipeline, Qt::AlignCenter);

        if (m_sss_ctrl)
            updateLayerTierStatus(layer->id);
    }
}

void ProcessingWindow::updateLayerTierStatus(const std::string& layer_id)
{
    if (!m_sss_ctrl) return;

    // Find the tree item for this layer.
    const QString qid = QString::fromStdString(layer_id);
    QTreeWidgetItem* item = nullptr;
    for (int i = 0; i < m_layer_list->topLevelItemCount(); ++i) {
        auto* it = m_layer_list->topLevelItem(i);
        if (it->data(kColName, Qt::UserRole).toString() == qid) {
            item = it;
            break;
        }
    }
    if (!item) return;

    const MapSonarQuality tiers[] = {
        MapSonarQuality::CoverageOnly,
        MapSonarQuality::Low,
        MapSonarQuality::Medium,
        MapSonarQuality::High,
        MapSonarQuality::Full
    };
    for (MapSonarQuality q : tiers) {
        const int col = columnForQuality(q);
        if (col < 0) continue;
        item->setText(col,
            m_sss_ctrl->hasCachedTier(layer_id, q) ? kStatusDone : kStatusNone);
    }
}

// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
//  Button slots
// ─────────────────────────────────────────────────────────────────────────────

void ProcessingWindow::onBuildPreviews()
{
    if (!m_sss_ctrl || !m_project) return;

    const auto ids = selectedLayerIds();
    if (ids.empty()) {
        appendLog(tr("Select one or more layers before building previews."));
        return;
    }

    // Mark selected rows as "building".
    const QString qbuild = kStatusBuilding;
    for (int i = 0; i < m_layer_list->topLevelItemCount(); ++i) {
        auto* item = m_layer_list->topLevelItem(i);
        const std::string id = item->data(kColName, Qt::UserRole).toString().toStdString();
        const bool selected = std::find(ids.begin(), ids.end(), id) != ids.end();
        if (!selected) continue;
        for (int c = kColCoverage; c <= kColFull; ++c)
            if (item->text(c) == kStatusNone)
                item->setText(c, qbuild);
    }

    const MapSonarQuality all_tiers[] = {
        MapSonarQuality::CoverageOnly,
        MapSonarQuality::Low,
        MapSonarQuality::Medium,
        MapSonarQuality::High,
        MapSonarQuality::Full
    };

    int tasks = 0;
    for (const auto& id : ids) {
        for (MapSonarQuality q : all_tiers) {
            if (!m_sss_ctrl->hasCachedTier(id, q)) {
                m_sss_ctrl->prebuildTier(id, q, m_project.get());
                ++tasks;
            }
        }
    }

    if (tasks == 0) {
        appendLog(tr("All selected layers are already fully cached."));
    } else {
        appendLog(tr("Building %1 tier(s) in the background…").arg(tasks));
        m_status_lbl->setText(tr("Building…"));
        m_progress->setVisible(true);
        m_progress->setRange(0, 0);  // indeterminate
    }
}

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

// ─────────────────────────────────────────────────────────────────────────────
//  Signal handlers
// ─────────────────────────────────────────────────────────────────────────────

void ProcessingWindow::onPrebuildTierComplete(const std::string& layer_id,
                                               MapSonarQuality    quality)
{
    updateLayerTierStatus(layer_id);

    // Check if any "building" status cells remain — hide progress when all done.
    bool any_building = false;
    for (int i = 0; i < m_layer_list->topLevelItemCount(); ++i) {
        auto* item = m_layer_list->topLevelItem(i);
        for (int c = kColCoverage; c <= kColFull; ++c) {
            if (item->text(c) == kStatusBuilding) { any_building = true; break; }
        }
        if (any_building) break;
    }
    if (!any_building) {
        m_progress->setVisible(false);
        m_progress->setRange(0, 100);
        m_status_lbl->setText(tr("Ready"));
        appendLog(tr("Preview cache complete."));
    }
}

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
