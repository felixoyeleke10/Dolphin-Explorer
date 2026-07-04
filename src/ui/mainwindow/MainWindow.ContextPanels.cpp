// MainWindow.ContextPanels.cpp — buildContextPanel, makeContextPlaceholder,
//   refreshSidebarSections.
#include "ui/mainwindow/MainWindow.h"
#include "app/project/Project.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/AppInfo.h"
#include "ui/shell/Theme.h"
#include "ui/mainwindow/panels/ViewsPanel.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/widgets/CollapsibleSection.h"
#include "ui/shared/widgets/SidePanelShell.h"
#include "ui/systems/DisplayStateManager.h"
#include "app/layers/DataLayer.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPoint>
#include <QSettings>
#include <QTimer>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace dolphin::ui {

void MainWindow::buildContextPanel(QWidget* parent)
{
    m_context_stack = new QStackedWidget(parent);
    m_context_stack->setObjectName("contextPanel");
    m_context_stack->setFixedWidth(Theme::kContextPanelW);

    auto* page = new SidePanelShell(m_context_stack);

    // -- Panel header ----------------------------------------------------------
    auto* hdr   = new QFrame(page);
    hdr->setObjectName("panelHdr");
    auto* hdr_l = new QHBoxLayout(hdr);
    hdr_l->setContentsMargins(Theme::kSpacing4, 10, Theme::kSpacing3, 10);
    auto* hdr_icon = new QLabel(hdr);
    hdr_icon->setFixedSize(14, 14);
    hdr_icon->setScaledContents(true);
    hdr_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    hdr_icon->setPixmap(Theme::icon(QStringLiteral(":/icons/explorer.svg")).pixmap(14, 14));
    hdr_l->addWidget(hdr_icon);
    hdr_l->addSpacing(4);
    m_context_title = new QLabel(tr("File Explorer"), hdr);
    m_context_title->setObjectName("panelTitle");
    hdr_l->addWidget(m_context_title, 1);
    page->setHeader(hdr);

    // -- Body — project tree + collapsible sections ---------------------------
    auto* body   = new QWidget(page);
    auto* layout = makeCompactLayout<QVBoxLayout>(body);

    // -- Project tree ----------------------------------------------------------
    m_line_list = new LineListPanel(body, LineListPanel::ContentMode::Explorer);
    layout->addWidget(m_line_list, 1);

    // -- Views section (SeaView-style per-viewer display settings) --------------
    // Recent Projects moved to the empty-map launcher (MapViewportHost); this
    // slot now hosts MAP | SSS | SBP display controls, incl. the 3D draping
    // surface — so the 3D view no longer has to beg for terrain data.
    auto* views_sec = new CollapsibleSection(tr("Views"), body);
    views_sec->setIcon(QStringLiteral(":/icons/layers.svg"));
    m_views_panel = new ViewsPanel(views_sec);

    connect(m_views_panel, &ViewsPanel::mapPaletteSelected, this, [this](int idx) {
        if (m_display_state) m_display_state->setMapPalette(idx);
    });
    connect(m_views_panel, &ViewsPanel::mapQualitySelected, this, [this](int q) {
        onMapSonarQuality(static_cast<MapSonarQuality>(q));
    });
    connect(m_views_panel, &ViewsPanel::sssPaletteSelected, this, [this](int idx) {
        if (!m_display_state || !currentProject()) return;
        const auto* l = currentProject()->findLayer(activeLayerId());
        if (l && l->modality == app::Modality::Sidescan)
            m_display_state->setLayerSssPalette(l->id, idx);
    });
    connect(m_views_panel, &ViewsPanel::sbpPaletteSelected, this, [this](int idx) {
        if (!m_display_state || !currentProject()) return;
        const auto* l = currentProject()->findLayer(activeLayerId());
        if (l && l->modality == app::Modality::SubBottom)
            m_display_state->setLayerSbpPalette(l->id, idx);
    });
    connect(m_views_panel, &ViewsPanel::drapingBrowseRequested,
            this, &MainWindow::onChooseDrapingSurface);
    connect(m_views_panel, &ViewsPanel::drapingClearRequested,
            this, &MainWindow::onClearDrapingSurface);

    views_sec->setContent(m_views_panel);
    layout->addWidget(views_sec);

    // -- Recycle Bin section ---------------------------------------------------
    // Soft-deleted contacts (the same project recycle bin the Contact Manager
    // shows). Right-click to Restore / Delete Forever; double-click to restore.
    auto* recycle_sec = new CollapsibleSection(tr("Recycle Bin"), body);
    recycle_sec->setIcon(QStringLiteral(":/icons/recycle_bin.svg"));
    recycle_sec->setExpanded(false);
    m_recycle_list = new QListWidget(recycle_sec);
    m_recycle_list->setObjectName("propsHistoryList");
    m_recycle_list->setFrameShape(QFrame::NoFrame);
    m_recycle_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_recycle_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* it) {
                if (currentProject() && it)
                    currentProject()->restoreContact(it->data(Qt::UserRole).toULongLong());
            });
    connect(m_recycle_list, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                if (!currentProject()) return;
                auto* it = m_recycle_list->itemAt(pos);
                QMenu menu(m_recycle_list);
                auto* restore = menu.addAction(tr("Restore"));
                restore->setEnabled(it != nullptr);
                connect(restore, &QAction::triggered, this, [this, it]() {
                    if (it) currentProject()->restoreContact(it->data(Qt::UserRole).toULongLong());
                });
                auto* purge = menu.addAction(tr("Delete Forever"));
                purge->setEnabled(it != nullptr);
                connect(purge, &QAction::triggered, this, [this, it]() {
                    if (it) currentProject()->purgeContact(it->data(Qt::UserRole).toULongLong());
                });
                menu.addSeparator();
                auto* empty = menu.addAction(tr("Empty Recycle Bin"));
                empty->setEnabled(!currentProject()->recycledContacts().empty());
                connect(empty, &QAction::triggered, this, [this]() {
                    currentProject()->emptyRecycleBin();
                });
                menu.exec(m_recycle_list->viewport()->mapToGlobal(pos));
            });
    recycle_sec->setContent(m_recycle_list);
    layout->addWidget(recycle_sec);
    refreshRecycleBin();

    page->setBody(body);

    m_context_stack->addWidget(page);  // index 0 — File Explorer
    m_context_stack->setCurrentIndex(0);

    QSettings s_init(AppInfo::kOrgName, AppInfo::kSettingsApp);
    refreshSidebarSections(s_init.value(QStringLiteral("recentProjects")).toStringList());
}

void MainWindow::refreshSidebarSections(const QStringList& paths)
{
    // Recent projects live on the empty-map launcher now.
    if (!m_viewport_host) return;
    QStringList names;
    names.reserve(paths.size());
    for (const QString& path : paths)
        names << recentDisplayName(path);
    m_viewport_host->setRecentProjects(names, paths);
}

void MainWindow::refreshRecycleBin()
{
    if (!m_recycle_list) return;
    m_recycle_list->clear();
    if (!currentProject()) return;
    for (const auto& c : currentProject()->recycledContacts()) {
        const QString label = c.label.empty()
            ? (c.line_id.empty() ? tr("(contact)") : QString::fromStdString(c.line_id))
            : QString::fromStdString(c.label);
        auto* item = new QListWidgetItem(label, m_recycle_list);
        item->setData(Qt::UserRole, static_cast<qulonglong>(c.id));
        QString tip = label;
        if (!c.classification.empty()) tip += QStringLiteral(" · ") + QString::fromStdString(c.classification);
        item->setToolTip(tip);
    }
}

QWidget* MainWindow::makeContextPlaceholder(const QString& title, const QString& body)
{
    auto* page = new QWidget(m_context_stack);
    auto* layout = makeCompactLayout<QVBoxLayout>(page);

    auto* hdr = new QFrame(page);
    hdr->setObjectName("panelHdr");
    auto* hdr_l = new QHBoxLayout(hdr);
    hdr_l->setContentsMargins(Theme::kSpacing4, 10, Theme::kSpacing4, 10);
    auto* ttl = new QLabel(title, hdr);
    ttl->setObjectName("panelTitle");
    hdr_l->addWidget(ttl);
    layout->addWidget(hdr);

    auto* body_lbl = new QLabel(body, page);
    body_lbl->setObjectName("panelPlaceholder");
    body_lbl->setWordWrap(true);
    body_lbl->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(body_lbl);
    layout->addStretch();
    return page;
}

// -- Views panel sync + draping surface -----------------------------------------

void MainWindow::refreshViewsPanel()
{
    if (!m_views_panel) return;

    if (m_display_state) {
        m_views_panel->setMapPalette(m_display_state->mapPalette());
        m_views_panel->setMapQuality(static_cast<int>(m_display_state->mapQuality()));
    }

    const app::DataLayer* active = currentProject()
        ? currentProject()->findLayer(activeLayerId()) : nullptr;
    const bool sss = active && active->modality == app::Modality::Sidescan;
    const bool sbp = active && active->modality == app::Modality::SubBottom;
    m_views_panel->setSssLayer(sss, sss ? active->sss_palette : -1);
    m_views_panel->setSbpLayer(sbp, sbp ? active->sbp_palette : -1);

    const QString drape = currentProject()
        ? QString::fromStdString(currentProject()->drapingSurface()) : QString{};
    m_views_panel->setDrapingSurface(
        drape.isEmpty() ? QString{} : QFileInfo(drape).fileName());
}

void MainWindow::onChooseDrapingSurface()
{
    if (!currentProject()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose Draping Surface"), {},
        tr("XYZ / CSV files (*.xyz *.csv *.txt);;All files (*)"));
    if (!path.isEmpty()) applyDrapingSurface(path);
}

void MainWindow::onClearDrapingSurface()
{
    applyDrapingSurface({});
}

void MainWindow::applyDrapingSurface(const QString& path, bool already_loaded)
{
    if (m_viewport_host && m_loaded_draping != path) {
        if (!m_loaded_draping.isEmpty())
            m_viewport_host->removeTerrainPath(m_loaded_draping);
        if (!path.isEmpty() && !already_loaded)
            m_viewport_host->loadTerrainPath(path);
    }
    m_loaded_draping = path;

    if (currentProject())
        currentProject()->setDrapingSurface(path.toStdString());
    if (m_views_panel)
        m_views_panel->setDrapingSurface(
            path.isEmpty() ? QString{} : QFileInfo(path).fileName());
}

} // namespace dolphin::ui
