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
#include "ui/mainwindow/coordinators/SidescanProcessingCoordinator.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/MapView.h"
#include "app/display/SubBottomDisplayParams.h"
#include "app/display/WaterfallParams.h"
#include "app/layers/DataLayer.h"

#include <algorithm>
#include <cmath>

#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPoint>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
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

    // File Explorer and the display sections share a user-adjustable boundary.
    // Let File Explorer take most of the sidebar when needed while the lower
    // display pane retains a compact, scrollable floor. Remember the user's
    // preferred split across sessions.
    auto* sidebar_splitter = new QSplitter(Qt::Vertical, body);
    sidebar_splitter->setObjectName(QStringLiteral("explorerViewsSplitter"));
    sidebar_splitter->setHandleWidth(5);
    layout->addWidget(sidebar_splitter, 1);

    // -- Project tree ----------------------------------------------------------
    m_line_list = new LineListPanel(sidebar_splitter, LineListPanel::ContentMode::Explorer);
    sidebar_splitter->addWidget(m_line_list);

    // Views has many controls, but its content height must not become the
    // splitter's minimum height.  A scroll area gives the lower pane an
    // independent viewport so File Explorer can keep the majority of the dock.
    auto* display_pane = new QWidget(sidebar_splitter);
    auto* display_layout = makeCompactLayout<QVBoxLayout>(display_pane);

    auto* display_scroll = new QScrollArea(display_pane);
    display_scroll->setObjectName(QStringLiteral("explorerViewsScroll"));
    display_scroll->setWidgetResizable(true);
    display_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    display_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    display_scroll->setFrameShape(QFrame::NoFrame);
    display_scroll->setMinimumHeight(Theme::kFormBtnH);

    auto* display_sections = new QWidget();
    auto* sections_layout = makeCompactLayout<QVBoxLayout>(display_sections);
    display_scroll->setWidget(display_sections);
    display_layout->addWidget(display_scroll, 1);
    sidebar_splitter->addWidget(display_pane);
    sidebar_splitter->setChildrenCollapsible(false);

    // -- Views section (SeaView-style per-viewer display settings) --------------
    // Recent Projects moved to the empty-map launcher (MapViewportHost); this
    // slot now hosts MAP | SSS | SBP display controls, incl. the 3D draping
    // surface — so the 3D view no longer has to beg for terrain data.
    auto* views_sec = new CollapsibleSection(tr("Views"), display_sections);
    views_sec->setIcon(QStringLiteral(":/icons/layers.svg"));
    m_views_panel = new ViewsPanel(views_sec);

    connect(m_views_panel, &ViewsPanel::mapPaletteSelected, this, [this](int idx) {
        if (m_display_state) m_display_state->setMapPalette(idx);
    });
    connect(m_views_panel, &ViewsPanel::mapQualitySelected, this, [this](int q) {
        onMapSonarQuality(static_cast<MapSonarQuality>(q));
    });
    connect(m_views_panel, &ViewsPanel::sssPaletteSelected, this, [this](int idx) {
        // SSS uses one universal palette across the map, waterfall, inspector,
        // and this panel. Route every picker through the same global authority.
        onPaletteChanged(idx);
    });
    connect(m_views_panel, &ViewsPanel::sbpPaletteSelected, this, [this](int idx) {
        if (!m_display_state || !currentProject()) return;
        const auto* l = currentProject()->findLayer(activeLayerId());
        if (l && l->modality == app::Modality::SubBottom) {
            m_display_state->setLayerSbpPalette(l->id, idx);
            if (m_sbp_win) m_sbp_win->setPalette(idx);
        }
    });
    // SBP display controls (moved here from the right panel's Display section):
    // live-apply to the active sub-bottom line + persist via the authority.
    connect(m_views_panel, &ViewsPanel::sbpDisplayEdited, this,
            [this](double gain, double contrast, bool invert) {
        if (!m_display_state || !currentProject()) return;
        auto* l = currentProject()->findLayer(activeLayerId());
        if (!l || l->modality != app::Modality::SubBottom) return;
        SubBottomDisplayParams p = l->sbp_display_state.display;
        p.gain            = static_cast<float>(gain);
        p.contrast        = static_cast<float>(contrast);
        p.polarity_invert = invert;
        if (l->sbp_palette >= 0) p.palette_index = l->sbp_palette;
        if (m_sbp_win) m_sbp_win->applyDisplayParams(p);
        m_display_state->setLayerSbpDisplay(l->id, p);
    });
    // SSS gain/contrast intentionally are NOT here — they live in the right
    // panel's Tools (the full imaging chain + Apply). The left Dynamic-range
    // control below covers brightness via black/white points.
    // Per-layer map transparency — cheap, applies instantly (no re-raster).
    connect(m_views_panel, &ViewsPanel::sssOpacityEdited, this, [this](int pct) {
        if (!m_display_state || !currentProject()) return;
        const auto* l = currentProject()->findLayer(activeLayerId());
        if (l && l->modality == app::Modality::Sidescan)
            m_display_state->setLayerOpacity(l->id, pct / 100.f);
    });
    // Mosaic blend mode — cheap repaint (composition of overlapping lines).
    connect(m_views_panel, &ViewsPanel::sssBlendSelected, this, [this](int mode) {
        if (!m_display_state || !currentProject()) return;
        const auto* l = currentProject()->findLayer(activeLayerId());
        if (l && l->modality == app::Modality::Sidescan)
            m_display_state->setLayerBlendMode(l->id, mode);
    });
    // Clip mosaic to drawn polygons (show inside) — cheap repaint.
    connect(m_views_panel, &ViewsPanel::sssClipPolygonsToggled, this, [this](bool on) {
        if (!m_display_state || !currentProject()) return;
        const auto* l = currentProject()->findLayer(activeLayerId());
        if (l && l->modality == app::Modality::Sidescan)
            m_display_state->setLayerClipPolygons(l->id, on);
    });
    // Across-track beam fan overlay — cheap repaint.
    connect(m_views_panel, &ViewsPanel::sssShowBeamsToggled, this, [this](bool on) {
        if (!m_display_state || !currentProject()) return;
        const auto* l = currentProject()->findLayer(activeLayerId());
        if (l && l->modality == app::Modality::Sidescan)
            m_display_state->setLayerShowBeams(l->id, on);
    });
    connect(m_views_panel, &ViewsPanel::sssBeamSpacingChanged, this, [this](int spacing) {
        if (!m_display_state || !currentProject()) return;
        const auto* l = currentProject()->findLayer(activeLayerId());
        if (l && l->modality == app::Modality::Sidescan)
            m_display_state->setLayerBeamSpacing(l->id, spacing);
    });
    // Dynamic range — black/white points (display_low/high). Committed on drag
    // release, so re-raster once here (heavier than gain/contrast's debounce
    // because it fires per gesture, not per tick).
    connect(m_views_panel, &ViewsPanel::sssDynamicRangeCommitted, this,
            [this](double low, double high) {
        if (!m_display_state || !currentProject()) return;
        auto* l = currentProject()->findLayer(activeLayerId());
        if (!l || l->modality != app::Modality::Sidescan) return;
        WaterfallParams p = l->sss_display_state.params;
        p.display_low  = static_cast<float>(low);
        p.display_high = static_cast<float>(high);
        if (m_sss_processing)
            m_sss_processing->commit(currentProject(), {l->id}, p);
        if (m_sss_ctrl) m_sss_ctrl->applyLiveCorrections({l->id});
    });
    connect(m_views_panel, &ViewsPanel::sbpOpacityEdited, this, [this](int pct) {
        if (!m_display_state || !currentProject()) return;
        const auto* l = currentProject()->findLayer(activeLayerId());
        if (l && l->modality == app::Modality::SubBottom)
            m_display_state->setLayerOpacity(l->id, pct / 100.f);
    });
    connect(m_views_panel, &ViewsPanel::drapingBrowseRequested,
            this, &MainWindow::onChooseDrapingSurface);
    connect(m_views_panel, &ViewsPanel::drapingClearRequested,
            this, &MainWindow::onClearDrapingSurface);

    views_sec->setContent(m_views_panel);
    sections_layout->addWidget(views_sec);

    // -- Recycle Bin section ---------------------------------------------------
    // Soft-deleted contacts (the same project recycle bin the Contact Manager
    // shows). Right-click to Restore / Delete Forever; double-click to restore.
    auto* recycle_sec = new CollapsibleSection(tr("Recycle Bin"), display_pane);
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
    display_layout->addWidget(recycle_sec);
    refreshRecycleBin();

    sidebar_splitter->setStretchFactor(0, 1);
    sidebar_splitter->setStretchFactor(1, 0);
    sidebar_splitter->setSizes({520, 150});

    QSettings sidebar_settings(AppInfo::kOrgName, AppInfo::kSettingsApp);
    const QByteArray saved_split = sidebar_settings.value(
        QStringLiteral("layout/explorerViewsSplitterV2")).toByteArray();
    if (!saved_split.isEmpty())
        sidebar_splitter->restoreState(saved_split);
    connect(sidebar_splitter, &QSplitter::splitterMoved, this,
            [sidebar_splitter](int, int) {
                QSettings(AppInfo::kOrgName, AppInfo::kSettingsApp).setValue(
                    QStringLiteral("layout/explorerViewsSplitterV2"),
                    sidebar_splitter->saveState());
            });

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

void MainWindow::refreshViewsPanel(bool follow_active)
{
    if (!m_views_panel) return;

    if (m_display_state) {
        m_views_panel->setMapPalette(m_display_state->mapPalette());
        m_views_panel->setMapQuality(static_cast<int>(m_display_state->mapQuality()));
    }

    // Mirror the right panel's sensor tabs: only modalities the project
    // contains are offered (MAP always).
    bool has_sss = false, has_sbp = false;
    if (currentProject()) {
        for (const auto& l : currentProject()->layers()) {
            if (!l) continue;
            has_sss |= l->modality == app::Modality::Sidescan;
            has_sbp |= l->modality == app::Modality::SubBottom;
        }
    }
    m_views_panel->setModalities(has_sss, has_sbp);

    const app::DataLayer* active = currentProject()
        ? currentProject()->findLayer(activeLayerId()) : nullptr;
    const bool sss = active && active->modality == app::Modality::Sidescan;
    const bool sbp = active && active->modality == app::Modality::SubBottom;
    if (sss) {
        const auto& p = active->sss_display_state.params;
        const int palette = m_display_state
            ? m_display_state->mapPalette()
            : PaletteIndex::Greyscale;
        m_views_panel->setSssLayer(true, palette,
                                   static_cast<int>(active->map_opacity * 100.f + 0.5f),
                                   active->map_blend_mode,
                                   active->map_clip_polygons, active->map_show_beams,
                                   active->map_beam_spacing);
        // Dynamic-range handles + histogram come from the SSS controller (it
        // owns the intensity pixels; the map never receives them). Present only
        // when the layer is rasterised (Low+ tier). display_low/high default
        // 0/1 = "use auto-stretch"; when identity, seat the handles on the
        // effective auto-stretch bounds so they show where the actual black/
        // white points sit rather than pinned to the edges.
        double dlow = p.display_low, dhigh = p.display_high;
        if (dlow == 0.f && dhigh == 1.f && m_sss_ctrl) {
            float alo = 0.f, ahi = 1.f;
            if (m_sss_ctrl->autoStretch(active->id, alo, ahi)) { dlow = alo; dhigh = ahi; }
        }
        m_views_panel->setSssDynamicRange(dlow, dhigh);
        m_views_panel->setSssHistogram(
            m_sss_ctrl ? m_sss_ctrl->amplitudeHistogram(active->id)
                       : std::vector<float>{});
    } else {
        m_views_panel->setSssLayer(false, -1);
    }
    if (sbp) {
        const auto& d = active->sbp_display_state.display;
        m_views_panel->setSbpLayer(true, active->sbp_palette,
                                   d.gain, d.contrast, d.polarity_invert,
                                   static_cast<int>(active->map_opacity * 100.f + 0.5f));
    } else {
        m_views_panel->setSbpLayer(false, -1);
    }

    // Follow the active layer's modality on selection changes only — palette
    // and quality refreshes must not yank the user off a tab they browsed to.
    if (follow_active)
        m_views_panel->setCurrentTab(sbp ? 2 : sss ? 1 : 0);

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
