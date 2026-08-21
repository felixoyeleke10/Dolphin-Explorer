// MainWindow.Layout.cpp — panel toggles, window geometry, and status bar helpers.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/shell/Features.h"
#include "ui/shell/Theme.h"
#include "ui/features/geodesy/GeodesyPanel.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/SidescanPing.h"
#include "core/SubBottomTrace.h"

#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStringList>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QUndoStack>
#include <QWidget>
#include <algorithm>
#include <type_traits>
#include "ui/mainwindow/panels/InspectorPanel.h"

namespace dolphin::ui {

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    adjustPropsSplit();   // keep the upper pane hugging its content as the window grows
}

// Make the upper (Properties/Map/History) pane only as tall as the page it's
// currently showing, and hand all remaining height to the lower sensor shell.
// Short property lists no longer leave a dead gap above the sensor tabs; long
// content still scrolls inside the upper pane's own scroll area.
void MainWindow::adjustPropsSplit()
{
    if (!m_props_splitter || !m_props_stack) return;

    const int total = m_props_splitter->height();
    if (total < 120) return;   // not laid out yet — leave the provisional split

    const int handle = m_props_splitter->handleWidth();
    const int avail  = total - handle;

    // The lower sensor shell keeps a usable floor (tab bar + a couple of rows).
    const int lower_min = 160;

    int upper_want;
    const int page = m_props_stack->currentIndex();
    if (page == 0 && m_inspector) {
        // Properties tab: header (tab bar) + the current page's real content.
        int header_h = 0;
        if (m_props_tab_tools && m_props_tab_tools->parentWidget())
            header_h = m_props_tab_tools->parentWidget()->sizeHint().height();
        upper_want = header_h + m_inspector->contentHeight() + 8 /* chrome */;
    } else if (page == 1 && m_props_stack->currentWidget()) {
        // Map settings: a compact form — size to its content like Properties.
        int header_h = 0;
        if (m_props_tab_tools && m_props_tab_tools->parentWidget())
            header_h = m_props_tab_tools->parentWidget()->sizeHint().height();
        upper_want = header_h
                   + m_props_stack->currentWidget()->sizeHint().height() + 8;
    } else {
        // History wants room to work — give it the lion's share.
        upper_want = avail * 6 / 10;
    }

    const int upper_min = 64;
    const int upper_max = avail - lower_min;
    if (upper_max <= upper_min) {            // panel too short to split sensibly
        m_props_splitter->setSizes({ avail / 2, avail - avail / 2 });
        return;
    }
    upper_want = std::clamp(upper_want, upper_min, upper_max);
    m_props_splitter->setSizes({ upper_want, avail - upper_want });
}

bool MainWindow::panelUsesContextStack(int panel_id) const
{
    return panel_id == PanelExplorer;
}

int MainWindow::normalizePanelId(int panel_id) const
{
    Q_UNUSED(panel_id)
    return 0;
}

int MainWindow::rightDockWidth() const
{
    return (m_right_tool_bar && m_right_tool_bar->isVisible()) ? Theme::kToolBarW : 0;
}

void MainWindow::togglePanel(int panel_id, bool force_open)
{
    Q_UNUSED(force_open)

    if (panel_id == PanelWaterfall) {
        if (auto* btn = m_activity_btns.value(PanelWaterfall, nullptr))
            btn->setChecked(false);
        onWaterfallOpen();
        return;
    }

    if (!m_context_stack) return;

    if (panel_id == PanelGeodesy) {
        onGeodeticSettings();
        return;
    }

    if (panel_id == PanelSettings) {
        if (auto* btn = m_activity_btns.value(PanelSettings, nullptr))
            btn->setChecked(false);
        onAppSettings();
        return;
    }

    if constexpr (Features::kDataLibrary) {
        if (panel_id == PanelDataLibrary) {
            if (auto* btn = m_activity_btns.value(PanelDataLibrary, nullptr))
                btn->setChecked(false);
            onDataLibraryOpen();
            return;
        }
    }

    m_context_stack->setCurrentIndex(0);
}

void MainWindow::toggleProperties()
{
    setPropertiesOpen(!m_props_open);
}

void MainWindow::setPropertiesOpen(bool open)
{
    if (!m_props_panel) return;
    m_props_open = open;
    m_props_panel->setVisible(open);
}

void MainWindow::setRightToolBarVisible(bool visible)
{
    if (m_right_tool_bar) m_right_tool_bar->setVisible(visible);
}

bool MainWindow::rightToolBarVisible() const
{
    return m_right_tool_bar && m_right_tool_bar->isVisible();
}

void MainWindow::applyWorkspaceState(int panel_id, bool props_open, bool toolbar_visible)
{
    setRightToolBarVisible(toolbar_visible);
    setPropertiesOpen(props_open);

    if (panel_id == PanelWaterfall)
        onWaterfallOpen();

    if (m_context_stack)
        m_context_stack->setCurrentIndex(0);
}

void MainWindow::updateContextInfo()
{
    if (!m_status_bar) return;

    if (!currentProject()) {
        m_status_bar->clearContext();
        m_status_bar->setViewCrs({});
        return;
    }

    // Status bar shows only the project name — the active line is already visible in
    // the tree (selected) and on the map, so repeating it here was just noise.
    const QString project = QString::fromStdString(currentProject()->name());
    m_status_bar->setProjectContext(project);

    // Status-bar CRS shows the project's survey/working grid (the projected CRS
    // the data is in) — not the map's internal WGS84 render ref — so it matches
    // the per-layer "Source CRS" in the inspector instead of contradicting it.
    // A project spanning multiple projected CRSes is flagged "(mixed)" so the
    // single badge doesn't imply a uniform grid (the dominant CRS is shown).
    const core::SpatialRef sr = currentProject()->workingCrs();
    QString crs_text = sr.id.empty() ? QStringLiteral("WGS 84")
                                     : QString::fromStdString(sr.id);
    if (currentProject()->hasMixedProjectedSources())
        crs_text += tr(" (mixed)");
    m_status_bar->setViewCrs(crs_text);
}

void MainWindow::appendJobMessage(const QString& message)
{
    if (m_status_bar) m_status_bar->showJobMessage(message);
    if (m_diag_hub)   m_diag_hub->logOutput(message);
}

void MainWindow::onToggleContextPanel()
{
    m_context_collapsed = !m_context_collapsed;

    if (m_context_stack)
        m_context_stack->setVisible(!m_context_collapsed);
    if (m_context_divider)
        m_context_divider->setVisible(!m_context_collapsed);

    // \u2039 = panel open (click to collapse); \u203a = panel hidden (click to expand)
    if (m_context_collapse_btn)
        m_context_collapse_btn->setText(m_context_collapsed ? "\u203a" : "\u2039");

    if (m_btn_primary_sidebar)
        m_btn_primary_sidebar->setChecked(!m_context_collapsed);
}

void MainWindow::onTogglePropertiesPanel()
{
    m_props_collapsed = !m_props_collapsed;

    if (m_props_panel)
        m_props_panel->setVisible(!m_props_collapsed);

    // › = panel open (click to collapse); ‹ = panel hidden (click to expand)
    if (m_props_collapse_btn)
        m_props_collapse_btn->setText(m_props_collapsed ? "‹" : "›");

    if (m_btn_secondary_sidebar)
        m_btn_secondary_sidebar->setChecked(!m_props_collapsed);
}

void MainWindow::onPropsTabChanged(int tab)
{
    if (m_props_stack) m_props_stack->setCurrentIndex(tab);
    if (tab == 2) rebuildHistoryList();  // refresh on every open
    adjustPropsSplit();                  // resize the pane to the newly shown tab
}

void MainWindow::rebuildHistoryList()
{
    if (!m_props_history_list) return;
    m_props_history_list->clear();

    auto* project = currentProject();
    auto* layer = project && !activeLayerId().empty()
        ? project->findLayer(activeLayerId()) : nullptr;
    if (!layer) {
        auto* hint = new QListWidgetItem(
            tr("Select a data layer to review its applied processing history."),
            m_props_history_list);
        hint->setFlags(Qt::NoItemFlags);
        hint->setData(Qt::UserRole + 4, true);
        return;
    }

    const QString layer_id = QString::fromStdString(layer->id);
    auto addSection = [this](const QString& title) {
        auto* hdr = new QListWidgetItem(title, m_props_history_list);
        hdr->setFlags(Qt::NoItemFlags);
        hdr->setData(Qt::UserRole + 2, true);
    };
    auto addEntry = [this, &layer_id](ActivityKind kind, const QString& title,
                                      const QString& detail) {
        auto* item = new QListWidgetItem(title, m_props_history_list);
        item->setFlags(Qt::ItemIsEnabled);
        item->setData(Qt::UserRole, layer_id);
        item->setData(Qt::UserRole + 1, detail);
        item->setData(Qt::UserRole + 2, false);
        item->setData(Qt::UserRole + 3, static_cast<int>(kind));
    };

    addSection(tr("Data record — %1").arg(QString::fromStdString(layer->label)));
    QString source_detail = tr("Indexed project data");
    if (const auto* source = project->findSource(layer->source_id)) {
        const QString file = QFileInfo(QString::fromStdString(source->path)).fileName();
        source_detail = tr("%1 • %2").arg(file,
            QString::fromStdString(source->format).toUpper());
    }
    const bool imported = layer->state == app::LayerState::Ready && layer->index_built;
    addEntry(ActivityKind::Import,
             imported ? tr("Imported") : tr("Import incomplete"), source_detail);

    using BT = app::BottomTrackKind;
    if (layer->bottom_track_kind == BT::Auto)
        addEntry(ActivityKind::Processing, tr("Bottom tracking"),
                 tr("Automatic bottom picks stored with this line"));
    else if (layer->bottom_track_kind == BT::Manual)
        addEntry(ActivityKind::Processing, tr("Bottom tracking"),
                 tr("Manual bottom picks stored with this line"));
    else if (layer->bottom_track_kind == BT::Mixed)
        addEntry(ActivityKind::Processing, tr("Bottom tracking"),
                 tr("Automatic bottom picks with manual edits"));

    addSection(tr("Applied settings"));
    bool any_applied = false;
    uint32_t described_baked_flags = 0;
    const auto baked = [layer](uint32_t flag) {
        return (layer->baked_correction_flags & flag) != 0;
    };
    const auto persisted = [this](bool is_baked) {
        return is_baked ? tr(" • persisted in processed data") : QString{};
    };
    pipeline::NodeGraph applied_graph;
    const bool has_applied_snapshot = !layer->applied_graph_json.empty()
        && applied_graph.fromJson(layer->applied_graph_json);
    const auto* graph_worker = project->findWorker(layer->id);
    const pipeline::NodeGraph& editable_graph = graph_worker ? graph_worker->graph
        : (layer->uses_project_graph ? project->processing_graph : layer->node_graph);
    const pipeline::NodeGraph& effective_graph = has_applied_snapshot
        ? applied_graph : editable_graph;
    const bool graph_has_processing = std::any_of(
        effective_graph.nodes().cbegin(), effective_graph.nodes().cend(),
        [](const auto& node) {
            if (!node) return false;
            const auto category = node->schema().category;
            return category != "Input" && category != "Output"
                && category != "Merge";
        });
    const bool node_graph_result =
        layer->processing_origin == app::ProcessingOrigin::NodeGraph
        || (layer->processing_origin == app::ProcessingOrigin::LegacyUnknown
            && layer->pipeline_applied && graph_has_processing);
    if (node_graph_result) {
        for (const auto& node : effective_graph.nodes()) {
            if (!node) continue;
            const auto schema = node->schema();
            if (schema.category == "Input" || schema.category == "Output"
                    || schema.category == "Merge")
                continue;
            QStringList settings;
            for (const auto& [key, definition] : schema.params) {
                const auto configured = node->params.find(key);
                const pipeline::Value& value = configured != node->params.end()
                    ? configured->second : definition.default_value;
                const QString text = std::visit([](const auto& item) -> QString {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, bool>)
                        return item ? QObject::tr("On") : QObject::tr("Off");
                    else if constexpr (std::is_same_v<T, std::string>)
                        return QString::fromStdString(item);
                    else if constexpr (std::is_floating_point_v<T>)
                        return QString::number(item, 'g', 5);
                    else
                        return QString::number(item);
                }, value);
                settings << tr("%1: %2")
                    .arg(QString::fromStdString(definition.label), text);
            }
            any_applied = true;
            addEntry(ActivityKind::Processing,
                     QString::fromStdString(schema.label),
                     settings.isEmpty()
                         ? tr("Persisted by node-based processing • %1")
                               .arg(QString::fromStdString(schema.category))
                         : settings.join(QStringLiteral(" • ")));
        }
    }
    if (layer->modality == app::Modality::Sidescan
            && layer->sss_display_state.customized && !node_graph_result) {
        const auto& p = layer->sss_display_state.params;
        auto applied = [&](const QString& name, const QString& detail) {
            any_applied = true;
            addEntry(ActivityKind::Processing, name, detail);
        };
        if (p.slant_range_correction || layer->slant_range_corrected) {
            const uint32_t flag = static_cast<uint32_t>(core::CorrectionFlag::SlantRange);
            if (baked(flag)) described_baked_flags |= flag;
            applied(tr("Slant-range correction"), tr("Ground-range geometry applied")
                + persisted(baked(flag)));
        }
        if (p.tvg.enabled) {
            const uint32_t flag = static_cast<uint32_t>(core::CorrectionFlag::Tvg);
            if (baked(flag)) described_baked_flags |= flag;
            applied(tr("TVG"), tr("Spreading %1 dB/dec • absorption %2 dB/m")
                .arg(p.tvg.spreading, 0, 'f', 0).arg(p.tvg.absorption, 0, 'f', 2)
                + persisted(baked(flag)));
        }
        if (p.agc.enabled) {
            const uint32_t flag = static_cast<uint32_t>(core::CorrectionFlag::GainNormalized);
            if (baked(flag)) described_baked_flags |= flag;
            applied(tr("AGC"), tr("%1 • %2% strength • %3 dB cap")
                .arg(p.agc.mode == app::AgcMode::Global ? tr("Global") : tr("Variable"))
                .arg(p.agc.strength * 100.f, 0, 'f', 0).arg(p.agc.gain_cap_db, 0, 'f', 0)
                + persisted(baked(flag)));
        }
        if (p.arc.enabled) {
            const uint32_t flag = static_cast<uint32_t>(core::CorrectionFlag::Arc);
            if (baked(flag)) described_baked_flags |= flag;
            applied(tr("ARC"), tr("Exponent %1 • gain cap %2 dB")
                .arg(p.arc.exponent, 0, 'f', 1).arg(p.arc.gain_cap_db, 0, 'f', 0)
                + persisted(baked(flag)));
        }
        if (p.arn.enabled) applied(tr("ARN"), tr("Strength %1 • gain cap %2 dB • smoothing %3 samples")
            .arg(p.arn.strength, 0, 'f', 2).arg(p.arn.gain_cap_db, 0, 'f', 0)
            .arg(p.arn.column_smooth));
        if (p.destripe.enabled) applied(tr("Destripe"), tr("Window %1 pings • %2 subdivisions • threshold %3 dB")
            .arg(p.destripe.window).arg(p.destripe.subdivision)
            .arg(p.destripe.threshold_db, 0, 'f', 2));
        if (p.beam_pattern.enabled) applied(tr("Beam pattern normalization"),
            tr("Strength %1 • smoothing %2 samples • gain cap %3 dB")
                .arg(p.beam_pattern.strength, 0, 'f', 2)
                .arg(p.beam_pattern.smooth_radius).arg(p.beam_pattern.gain_cap_db, 0, 'f', 0));
        if (p.ml_enhance.enabled) applied(tr("Adaptive contrast (CLAHE)"), tr("Tiles %1 × %2 • clip %3")
            .arg(p.ml_enhance.tile_pings).arg(p.ml_enhance.tile_samps)
            .arg(p.ml_enhance.clip_limit, 0, 'f', 1));
    } else if (layer->modality == app::Modality::SubBottom && !node_graph_result) {
        const auto& d = layer->sbp_display_state;
        if (d.gain_customized) {
            if (d.gain.static_gain_en) { any_applied = true; addEntry(ActivityKind::Processing,
                tr("Static gain"), tr("%1 dB").arg(d.gain.static_gain_db, 0, 'f', 1)
                    + persisted(baked(static_cast<uint32_t>(core::SbpCorrectionFlag::StaticGain)))); }
            if (d.gain.agc_en) { any_applied = true; addEntry(ActivityKind::Processing,
                tr("AGC"), tr("Window %1 traces").arg(d.gain.agc_window)
                    + persisted(baked(static_cast<uint32_t>(core::SbpCorrectionFlag::Agc)))); }
            if (d.gain.normalize_en) { any_applied = true; addEntry(ActivityKind::Processing,
                tr("Trace normalization"), tr("Per-trace peak normalization")
                    + persisted(baked(static_cast<uint32_t>(core::SbpCorrectionFlag::Normalize)))); }
        }
        if (d.signal_customized) {
            if (d.signal.envelope_en) { any_applied = true; addEntry(ActivityKind::Processing,
                tr("Envelope"), tr("Full-wave amplitude envelope")
                    + persisted(baked(static_cast<uint32_t>(core::SbpCorrectionFlag::Envelope)))); }
            if (d.signal.dc_removal_en) { any_applied = true; addEntry(ActivityKind::Processing,
                tr("DC removal"), tr("Per-trace mean removed")
                    + persisted(baked(static_cast<uint32_t>(core::SbpCorrectionFlag::DcRemoval)))); }
            if (d.signal.bandpass_en) { any_applied = true; addEntry(ActivityKind::Processing,
                tr("Bandpass"), tr("%1–%2 Hz").arg(d.signal.bp_lo_hz, 0, 'f', 0)
                    .arg(d.signal.bp_hi_hz, 0, 'f', 0)
                    + persisted(baked(static_cast<uint32_t>(core::SbpCorrectionFlag::BandPass)))); }
        }
    }

    if (layer->nav_customized) {
        const auto& n = layer->nav_state;
        if (n.smooth_enabled) { any_applied = true; addEntry(ActivityKind::NavCorrection,
            tr("Navigation smoothing"), tr("Window %1 pings").arg(n.smooth_window)); }
        if (n.layback_enabled) { any_applied = true; addEntry(ActivityKind::NavCorrection,
            tr("Towfish layback"), tr("%1 m").arg(n.layback_m, 0, 'f', 1)); }
        if (n.heading_offset_deg != 0.f || n.pitch_offset_deg != 0.f || n.roll_offset_deg != 0.f) {
            any_applied = true;
            addEntry(ActivityKind::NavCorrection, tr("Attitude offsets"),
                tr("Heading %1° • pitch %2° • roll %3°")
                    .arg(n.heading_offset_deg, 0, 'f', 1)
                    .arg(n.pitch_offset_deg, 0, 'f', 1)
                    .arg(n.roll_offset_deg, 0, 'f', 1));
        }
    }
    if (layer->modality == app::Modality::Sidescan && !node_graph_result) {
        const auto addBaked = [&](core::CorrectionFlag flag, const QString& title,
                                  const QString& detail) {
            const uint32_t bit = static_cast<uint32_t>(flag);
            if (!baked(bit) || (described_baked_flags & bit) != 0) return;
            any_applied = true;
            addEntry(ActivityKind::Processing, title, detail);
        };
        addBaked(core::CorrectionFlag::Tvg, tr("TVG"), tr("Persisted in processed data"));
        addBaked(core::CorrectionFlag::Arc, tr("ARC"), tr("Persisted in processed data"));
        addBaked(core::CorrectionFlag::GainNormalized, tr("AGC / gain normalization"),
                 tr("Persisted in processed data"));
        addBaked(core::CorrectionFlag::SlantRange, tr("Slant-range correction"),
                 tr("Ground-range geometry persisted in processed data"));
        addBaked(core::CorrectionFlag::BeamPattern, tr("Beam pattern normalization"),
                 tr("Persisted by the processing graph"));
        addBaked(core::CorrectionFlag::Destriping, tr("Destripe"),
                 tr("Persisted by the processing graph"));
        addBaked(core::CorrectionFlag::Arn, tr("ARN"),
                 tr("Persisted by the processing graph"));
        addBaked(core::CorrectionFlag::AdaptiveContrast, tr("Adaptive contrast"),
                 tr("Persisted by the processing graph"));
    } else if (layer->modality == app::Modality::SubBottom && !node_graph_result) {
        const auto addBaked = [&](core::SbpCorrectionFlag flag, const QString& title,
                                  bool already_described) {
            if (already_described || !baked(static_cast<uint32_t>(flag))) return;
            any_applied = true;
            addEntry(ActivityKind::Processing, title, tr("Persisted in processed trace data"));
        };
        const auto& d = layer->sbp_display_state;
        addBaked(core::SbpCorrectionFlag::StaticGain, tr("Static gain"),
                 d.gain_customized && d.gain.static_gain_en);
        addBaked(core::SbpCorrectionFlag::Agc, tr("AGC"),
                 d.gain_customized && d.gain.agc_en);
        addBaked(core::SbpCorrectionFlag::Normalize, tr("Trace normalization"),
                 d.gain_customized && d.gain.normalize_en);
        addBaked(core::SbpCorrectionFlag::Envelope, tr("Envelope"),
                 d.signal_customized && d.signal.envelope_en);
        addBaked(core::SbpCorrectionFlag::DcRemoval, tr("DC removal"),
                 d.signal_customized && d.signal.dc_removal_en);
        addBaked(core::SbpCorrectionFlag::BandPass, tr("Bandpass"),
                 d.signal_customized && d.signal.bandpass_en);
    }
    if (layer->pipeline_applied && layer->baked_correction_flags == 0) {
        any_applied = true;
        addEntry(ActivityKind::Processing, tr("Processing graph executed"),
                 tr("Legacy project does not contain per-operation provenance"));
    }
    if (!any_applied)
        addEntry(ActivityKind::DisplayParams, tr("No processing applied"),
                 tr("Layer is using imported/default data settings"));
}

void MainWindow::recordActivity(ActivityKind kind, const QString& description)
{
    m_activity_log.record(kind, description);
    // Refresh the list only if the History tab is currently visible.
    if (m_props_stack && m_props_stack->currentIndex() == 2)
        rebuildHistoryList();
}

void MainWindow::onLayerVisibilityChanged(const std::string& layer_id, bool visible)
{
    if (!currentProject()) {
        if (m_viewport_host) m_viewport_host->setLayerVisible(layer_id, visible);
        else if (m_map_view) m_map_view->setLayerVisible(layer_id, visible);
        return;
    }

    // Determine previous state from the layer so undo knows what to restore.
    bool old_visible = visible;
    if (const auto* layer = currentProject()->findLayer(layer_id))
        old_visible = layer->visible;

    auto apply = [this](const std::string& lid, bool v) {
        // Single mutate point: DisplayStateManager writes the model and emits
        // displayStateChanged(lid, Visibility); MainWindow's bus handler fans
        // out to viewport / line list / layer picker and marks the project
        // dirty. Undo/redo replays land here too, so they stay in sync.
        if (m_display_state) m_display_state->setLayerVisible(lid, v);
    };

    m_undo_stack->push(new SetLayerVisibleCommand(
        layer_id, old_visible, visible, std::move(apply)));

    if (currentProject()) {
        if (const auto* layer = currentProject()->findLayer(layer_id)) {
            recordActivity(ActivityKind::Visibility,
                tr("%1 %2").arg(QString::fromStdString(layer->label),
                               visible ? tr("shown") : tr("hidden")));
        }
    }
}

void MainWindow::updateActionStates()
{
    const bool has_project = currentProject() != nullptr;
    const bool has_layer   = has_project && !activeLayerId().empty();

    if (m_act_save)      m_act_save->setEnabled(has_project);
    if (m_act_run_all)   m_act_run_all->setEnabled(has_project);
    if (m_export_btn)    m_export_btn->setEnabled(has_project);
    // m_act_run_layer is capability-based; managed by updateControlsForModality.
}

} // namespace dolphin::ui
