// MainWindow.WaterfallCoordinator.cpp — waterfall window lifecycle and state reflection.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/shell/ViewerWindow.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "ui/mainwindow/panels/GainControlPanel.h"
#include "ui/mainwindow/panels/ImagingControlPanel.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "app/corrections/SidescanCorrectionService.h"
#include "ui/shell/Features.h"
#include "ui/shared/dialogs/CrsPickerDialog.h"
#include "ui/features/metadata/SSSMetadataWindow.h"
#include "ui/features/waterfall/WaterfallSettingsDialog.h"
#include "ui/features/contacts/ContactListPanel.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/map/MapView.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "app/project/Project.h"
#include "app/project/ProjectTransaction.h"
#include "app/layers/DataLayer.h"
#include "core/Contact.h"
#include "core/SpatialRef.h"
#include "geo/EpsgDatabase.h"

#include <QMessageBox>

#include <cmath>

namespace dolphin::ui {

void MainWindow::onWaterfallOpen()
{
    if (!m_waterfall_win) {
        m_waterfall_win = new WaterfallWindow(m_app_state, nullptr);
        m_window_registry->registerViewer(m_waterfall_win, m_waterfall_win);
        connect(m_waterfall_win, &WaterfallWindow::newFileRequested,
                this, &MainWindow::onImportFile);
        connect(m_waterfall_win, &WaterfallWindow::openFileRequested,
                this, &MainWindow::onOpenProject);
        connect(m_waterfall_win, &WaterfallWindow::saveFileRequested,
                this, &MainWindow::onSaveProject);
        connect(m_waterfall_win, &WaterfallWindow::propertiesRequested,
                this, &MainWindow::toggleProperties);
        connect(m_waterfall_win, &WaterfallWindow::metadataRequested,
                this, &MainWindow::onWaterfallMetadata);
        connect(m_waterfall_win, &WaterfallWindow::settingsRequested,
                this, &MainWindow::onWaterfallSettings);
        connect(m_waterfall_win, &WaterfallWindow::prevLineRequested,
                this, &MainWindow::onWaterfallPrevLine);
        connect(m_waterfall_win, &WaterfallWindow::nextLineRequested,
                this, &MainWindow::onWaterfallNextLine);

        connect(m_waterfall_win, &WaterfallWindow::cursorUpdated,
                this, &MainWindow::onWaterfallCursorUpdated);
        connect(m_waterfall_win, &WaterfallWindow::contactCreated,
                this, &MainWindow::onWaterfallContactCreated);
        connect(m_waterfall_win, &WaterfallWindow::paramsApplied,
                this, &MainWindow::onWaterfallParamsApplied);
        connect(m_waterfall_win, &WaterfallWindow::applyToAllRequested,
                this, &MainWindow::onWaterfallParamsApplied);
        connect(m_waterfall_win, &WaterfallWindow::setCrsRequested,
                this, &MainWindow::onWaterfallSetCrs);
        connect(m_waterfall_win, &WaterfallWindow::navProcessAllLinesRequested,
                this, &MainWindow::onWaterfallNavProcessAllLines);
        connect(m_waterfall_win, &WaterfallWindow::paletteChanged,
                this, [this](int idx) {
                    onPaletteChanged(idx);
                    // Persist per-layer SSS palette so it survives project close/reopen.
                    if (!m_project || m_active_layer_id.empty()) return;
                    auto* layer = m_project->findLayer(m_active_layer_id);
                    if (layer && layer->sss_palette != idx) {
                        layer->sss_palette = idx;
                        m_project_dirty = true;
                        setWindowTitleFromProject();
                    }
                });
        connect(m_waterfall_win, &WaterfallWindow::qcViewedFractionChanged,
                this, [this](const std::string& /*layer_id*/, float /*fraction*/) {
                    // WaterfallScrollSync already writes to layer->qc_viewed_fraction
                    // before emitting this signal — just mark the project dirty so
                    // the updated fraction is included in the next save.
                    if (m_project && !m_project_dirty) {
                        m_project_dirty = true;
                        setWindowTitleFromProject();
                    }
                });
        connect(m_waterfall_win, &WaterfallWindow::dataStateChanged,
                this, [this](ViewerDataState s) {
                    refreshLoadingIndicator();
                    if (s == ViewerDataState::Loading || s == ViewerDataState::Processing) {
                        m_op_mgr->registerExternal("waterfall", m_waterfall_win->loadToken());
                    } else {
                        m_op_mgr->unregisterExternal("waterfall");
                    }
                });
        connect(m_waterfall_win, &WaterfallWindow::layerChangeRequested,
                this, [this](const std::string& id) { onLayerSelected(id); });

        // -- Control panel wiring ------------------------------------------
        // Nav correction panels push corrections to the waterfall
        connect(m_nav_panel, &NavInfoPanel::applyToLineRequested,
                m_waterfall_win, &WaterfallWindow::applyNavToLine);
        connect(m_nav_panel, &NavInfoPanel::applyToAllRequested,
                m_waterfall_win, &WaterfallWindow::applyNavToAll);

        connect(m_heading_panel, &HeadingInfoPanel::applyToLineRequested,
                m_waterfall_win, &WaterfallWindow::applyNavToLine);
        connect(m_heading_panel, &HeadingInfoPanel::applyToAllRequested,
                m_waterfall_win, &WaterfallWindow::applyNavToAll);

        // Gain / imaging panels push params back to the waterfall
        connect(m_gain_panel,    &GainControlPanel::applyToLineRequested,
                m_waterfall_win, &WaterfallWindow::applyExternalParams);
        connect(m_gain_panel,    &GainControlPanel::applyToAllRequested,
                m_waterfall_win, &WaterfallWindow::applyExternalParamsToAll);
        connect(m_imaging_panel, &ImagingControlPanel::applyToLineRequested,
                m_waterfall_win, &WaterfallWindow::applyExternalParams);
        connect(m_imaging_panel, &ImagingControlPanel::applyToAllRequested,
                m_waterfall_win, &WaterfallWindow::applyExternalParamsToAll);

        // When the waterfall applies any params, pull the latest state back into
        // the gain and imaging panels so they stay in sync with internal changes.
        // Also sync display params to the SSS map so its colours update globally.
        connect(m_waterfall_win, &WaterfallWindow::paramsApplied, this, [this]() {
            if (!m_waterfall_win) return;
            const auto& p = m_waterfall_win->currentParams();
            m_gain_panel->setParams(p);
            m_imaging_panel->setParams(p);
            if (m_sss_ctrl) m_sss_ctrl->setDisplayParams(p);

            if (m_project) {
                // Use the layer the waterfall is actually showing, which may
                // differ from m_active_layer_id when the user has navigated
                // Prev/Next inside the waterfall window.
                const std::string wf_id = m_waterfall_win->currentLayerId();
                if (!wf_id.empty()) {
                    auto* layer = m_project->findLayer(wf_id);
                    if (layer) { layer->sss_display_state.params = p; layer->sss_display_state.customized = true; }
                    if (layer && layer->slant_range_corrected != p.slant_range_correction) {
                        layer->slant_range_corrected = p.slant_range_correction;
                        if (m_sss_ctrl) m_sss_ctrl->reloadLayer(wf_id);
                        app::ProjectTransaction tx(m_project.get());
                        tx.commit();
                    }
                }
            }
        });

        // "Apply to all" — propagate full params + SRC to every layer.
        connect(m_waterfall_win, &WaterfallWindow::applyToAllRequested, this, [this]() {
            if (!m_project || !m_waterfall_win) return;
            const WaterfallParams p = m_waterfall_win->currentParams();
            app::ProjectTransaction tx(m_project.get());
            for (const auto& l : m_project->layers()) {
                if (!l) continue;
                l->slant_range_corrected = p.slant_range_correction;
                l->sss_display_state.params = p;
                l->sss_display_state.customized = true;
            }
            if (m_sss_ctrl) m_sss_ctrl->reloadCurrentLayer();
            tx.commit();
            // Bake amplitude corrections into every layer's .dlpd.
            if (m_correction_svc) m_correction_svc->applyToAll(*m_project, toCorrectionParams(p));
        });

        // -- Bake-to-dlpd wiring -----------------------------------------------
        // When the user explicitly applies gain/imaging corrections (Apply to
        // Line / Apply to All buttons in the panel), also write the corrected
        // amplitudes back into the .dlpd so exports and future sessions see the
        // corrected data — not just the current session's display preview.
        if (m_correction_svc) {
            auto bakeCurrentLine = [this](const WaterfallParams& p) {
                if (!m_project || !m_waterfall_win) return;
                const std::string wf_id = m_waterfall_win->currentLayerId();
                if (wf_id.empty()) return;
                auto* layer = m_project->findLayer(wf_id);
                if (!layer) return;
                const auto* src = m_project->findSource(layer->source_id);
                m_correction_svc->applyToLine(
                    wf_id,
                    layer->artifact_store_path,
                    layer->artifact_store_format,
                    layer->artifact_index,
                    src ? src->path : std::string{},
                    toCorrectionParams(p));
            };

            connect(m_gain_panel,    &GainControlPanel::applyToLineRequested,
                    this, bakeCurrentLine);
            connect(m_imaging_panel, &ImagingControlPanel::applyToLineRequested,
                    this, bakeCurrentLine);
            // applyToAll baking is handled by the WaterfallWindow::applyToAllRequested
            // lambda above (line 163) which calls m_correction_svc->applyToAll after
            // applyExternalParamsToAll emits the signal — no duplicate connection needed.
        }
    }

    // Populate the FILES list with every sidescan layer in the current project.
    if (m_project) {
        std::vector<std::pair<std::string, std::string>> sss_layers;
        for (const auto& l : m_project->layers())
            if (l && l->modality == app::Modality::Sidescan)
                sss_layers.emplace_back(l->id, l->label);
        m_waterfall_win->setProjectLayers(sss_layers);
    }

    if (m_project && !m_active_layer_id.empty()) {
        auto* layer = m_project->findLayer(m_active_layer_id);
        if (layer && layer->modality == app::Modality::Sidescan) {
            const auto* src    = m_project->findSource(layer->source_id);
            const std::string path = src ? src->path : std::string{};
            const uint64_t    sz   = src ? src->size_bytes : 0;
            m_waterfall_win->setLayer(layer, m_import_service, path, sz);
            applyStoredNavParams(m_active_layer_id);
            m_waterfall_win->setProjectContacts(m_project->contacts());

            // Restore per-layer display params if the user has previously adjusted them.
            if (layer->sss_display_state.customized)
                m_waterfall_win->applyExternalParams(layer->sss_display_state.params);

            // Sync mini-panels to the waterfall's current params on initial open.
            // The SSS map is synced via the paramsApplied signal from applyExternalParams
            // above; if no stored params exist, m_display_params stays nullopt so the
            // map continues to use its own per-layer auto-stretch.
            if (m_gain_panel && m_imaging_panel) {
                const auto& p = m_waterfall_win->currentParams();
                m_gain_panel->setParams(p);
                m_imaging_panel->setParams(p);
            }
        }
    }

    // Sync palette: use the per-layer saved palette if available, otherwise fall
    // back to the Properties inspector (which shows the app-wide default).
    {
        auto* layer = m_project ? m_project->findLayer(m_active_layer_id) : nullptr;
        if (layer && layer->sss_palette >= 0)
            m_waterfall_win->setPalette(layer->sss_palette);
        else if (m_inspector)
            m_waterfall_win->setPalette(m_inspector->currentPaletteIndex());
    }

    m_waterfall_win->show();
    m_waterfall_win->raise();
    m_waterfall_win->activateWindow();
}

void MainWindow::onWaterfallPrevLine(const std::string& from_layer_id)
{
    if (!m_project) return;
    const auto& layers = m_project->layers();
    if (layers.empty()) return;

    const std::string& ref_id = from_layer_id.empty() ? m_active_layer_id : from_layer_id;

    int cur = -1;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
        if (layers[i]->id == ref_id) { cur = i; break; }

    const int start = (cur >= 0) ? cur - 1 : static_cast<int>(layers.size()) - 1;
    for (int i = start; i >= 0; --i) {
        if (layers[i]->index_built && layers[i]->artifactCount() > 0) {
            onLayerSelected(layers[i]->id);
            return;
        }
    }
    appendJobMessage("Already on the first survey line.");
}

void MainWindow::onWaterfallNextLine(const std::string& from_layer_id)
{
    if (!m_project) return;
    const auto& layers = m_project->layers();
    if (layers.empty()) return;

    const std::string& ref_id = from_layer_id.empty() ? m_active_layer_id : from_layer_id;

    int cur = -1;
    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
        if (layers[i]->id == ref_id) { cur = i; break; }

    const int start = cur + 1;
    for (int i = start; i < static_cast<int>(layers.size()); ++i) {
        if (layers[i]->index_built && layers[i]->artifactCount() > 0) {
            onLayerSelected(layers[i]->id);
            return;
        }
    }
    appendJobMessage("Already on the last survey line.");
}

void MainWindow::onWaterfallCursorUpdated(float range_m, const QString& side,
                                          double lat, double lon, bool is_projected)
{
    if (!m_status_bar) return;
    if (range_m <= 0.f) {
        m_status_bar->clearCursorRange();
        m_status_bar->clearCursorPosition();
        return;
    }

    m_status_bar->setCursorRange(side, range_m);

    if (lat != 0.0 || lon != 0.0)
        m_status_bar->setCursorPosition(lat, lon, is_projected);
    else
        m_status_bar->clearCursorPosition();
}

void MainWindow::onWaterfallContactCreated(float range_m, double lat, double lon,
                                           bool is_projected,
                                           const QString& classification,
                                           const QString& line_id,
                                           uint64_t abs_row,
                                           int channel_idx)
{
    if (!m_project) return;

    const int n = static_cast<int>(m_project->contacts().size()) + 1;
    core::Contact c;
    c.label          = QString("C%1").arg(n, 3, 10, QChar('0')).toStdString();
    c.lat            = lat;
    c.lon            = lon;
    c.spatial_ref    = is_projected
                     ? core::makeUnknownProjectedSpatialRef()
                     : core::makeWgs84SpatialRef();
    c.classification = classification.toStdString();
    c.line_id        = line_id.toStdString();
    c.range_m        = range_m;
    c.artifact_id    = abs_row;
    c.sample_idx     = static_cast<uint32_t>(channel_idx);

    m_undo_stack->push(new AddContactCommand(
        m_project.get(), c,
        [this]() { m_project_dirty = true; setWindowTitleFromProject(); }));

    if constexpr (Features::kContacts)
        if (m_contact_list) m_contact_list->refresh();

    appendJobMessage(
        QString("Contact %1 placed \u2014 %2 (%3 m)")
            .arg(QString::fromStdString(c.label))
            .arg(classification)
            .arg(range_m, 0, 'f', 1));
}

void MainWindow::onWaterfallParamsApplied()
{
    if (!m_active_layer_id.empty() && m_project) {
        if (const auto* layer = m_project->findLayer(m_active_layer_id)) {
            recordActivity(ActivityKind::DisplayParams,
                tr("Display params applied to %1")
                    .arg(QString::fromStdString(layer->label)));
        }
    } else {
        recordActivity(ActivityKind::DisplayParams, tr("Display parameters applied"));
    }
}

void MainWindow::onContactSelected(uint64_t contact_id)
{
    if (m_map_view) m_map_view->setSelectedContact(contact_id);

    if (!m_project || !m_inspector) return;
    for (const auto& c : m_project->contacts())
        if (c.id == contact_id) { m_inspector->showContact(&c); return; }
}

void MainWindow::onContactPicked(double lat, double lon,
                                 uint64_t /*artifact_id*/, uint32_t /*sample_idx*/)
{
    appendJobMessage(QString("Picked  Lat %1  Lon %2")
        .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6));
}

void MainWindow::onWaterfallMetadata()
{
    if (!m_metadata_win) {
        auto* win = new SSSMetadataWindow(nullptr);
        win->setAttribute(Qt::WA_DeleteOnClose);
        m_metadata_win = win;
    }

    auto* win = qobject_cast<SSSMetadataWindow*>(m_metadata_win);
    if (win)
        win->setProject(m_project.get(), m_import_service, m_active_layer_id);

    m_metadata_win->show();
    m_metadata_win->raise();
    m_metadata_win->activateWindow();
}

void MainWindow::onWaterfallSettings()
{
    if (!m_waterfall_win) return;
    auto* dlg = new WaterfallSettingsDialog(
        m_waterfall_win->wfSettings(),
        static_cast<QWidget*>(m_waterfall_win));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &WaterfallSettingsDialog::applied,
            this, [this](WaterfallSettingsDialog::Settings s) {
                if (m_waterfall_win) m_waterfall_win->applyWfSettings(s);
            });
    dlg->show();
}

void MainWindow::onWaterfallSetCrs(const std::string& from_layer_id)
{
    if (!m_project) return;

    // Use the layer the waterfall is actually showing, which may differ from
    // m_active_layer_id when the user has used Prev/Next inside the waterfall.
    const std::string ref_id = from_layer_id.empty() ? m_active_layer_id : from_layer_id;
    auto* layer = m_project->findLayer(ref_id);
    if (!layer) return;

    // Open picker pre-seeded with the current (possibly unconfirmed) CRS
    QWidget* parent = m_waterfall_win ? static_cast<QWidget*>(m_waterfall_win) : this;
    CrsPickerDialog dlg(layer->source_spatial_ref, parent);
    if (dlg.exec() != QDialog::Accepted) return;

    const core::SpatialRef new_ref = dlg.selectedRef();
    if (new_ref.kind == core::SpatialRefKind::Unknown) return;

    const core::SpatialRef old_ref = layer->source_spatial_ref;

    // Source CRS is a source-file property — apply to every layer and source in
    // the project.  The project display CRS (map target) is left unchanged; the
    // normalisation step reprojects source coordinates into it on reload.
    auto apply_crs = [this, ref_id](const core::SpatialRef& ref) {
        if (!m_project) return;
        {
            app::ProjectTransaction tx(m_project.get());
            for (const auto& l : m_project->layers())
                if (l) l->source_spatial_ref = ref;
            for (const auto& l : m_project->layers()) {
                if (!l) continue;
                if (auto* src = m_project->findSource(l->source_id))
                    src->source_spatial_ref = ref;
            }
            tx.commit();
        }
        auto* lyr = m_project->findLayer(ref_id);
        if (!ref_id.empty() && ref_id != m_active_layer_id) {
            m_active_layer_id = ref_id;
            m_app_state->setSelection({ref_id, lyr ? lyr->modality : app::Modality::Unknown});
            if (m_inspector && lyr) m_inspector->showLayer(lyr);
        }
        if (m_sss_ctrl) m_sss_ctrl->reloadCurrentLayer();
        if (m_waterfall_win && lyr) {
            const auto* src = m_project->findSource(lyr->source_id);
            m_waterfall_win->setLayer(lyr, m_import_service,
                                      src ? src->path : std::string{},
                                      src ? src->size_bytes : 0);
            applyStoredNavParams(lyr->id);
            if (lyr->sss_display_state.customized)
                m_waterfall_win->applyExternalParams(lyr->sss_display_state.params);
            // Restore per-layer palette after CRS-triggered reload.
            if (lyr->sss_palette >= 0)
                m_waterfall_win->setPalette(lyr->sss_palette);
            else if (m_inspector)
                m_waterfall_win->setPalette(m_inspector->currentPaletteIndex());
        }
    };

    m_undo_stack->push(new SetSourceCrsCommand(old_ref, new_ref, apply_crs));

    const std::string display = geo::epsgDisplayName(new_ref);
    const int n = static_cast<int>(m_project->layers().size());
    appendJobMessage(tr("Source CRS set to %1 — applied to %2 layer(s)")
        .arg(QString::fromStdString(display)).arg(n));
    recordActivity(ActivityKind::CrsChange,
        tr("Source CRS → %1").arg(QString::fromStdString(display)));
}

void MainWindow::onWaterfallNavProcessAllLines(NavProcessingParams params)
{
    if (!m_project) return;

    auto old_state = m_layer_nav_params;
    auto new_state = old_state;
    int n = 0;
    for (const auto& layer : m_project->layers()) {
        if (!layer || layer->modality != app::Modality::Sidescan) continue;
        new_state[layer->id] = params;
        ++n;
    }
    if (n == 0) return;

    auto apply = [this](const std::unordered_map<std::string, NavProcessingParams>& map) {
        m_layer_nav_params = map;
        m_project_dirty = true;
        setWindowTitleFromProject();
    };
    m_undo_stack->push(new SetNavParamsAllCommand(
        std::move(old_state), std::move(new_state), std::move(apply)));

    appendJobMessage(tr("Nav corrections stored for %1 sidescan line(s) — applied on next open").arg(n));
    recordActivity(ActivityKind::NavCorrection,
        tr("Nav corrections stored for %1 line(s)").arg(n));
}

void MainWindow::applyStoredNavParams(const std::string& layer_id)
{
    if (!m_waterfall_win || layer_id.empty()) return;
    const auto it = m_layer_nav_params.find(layer_id);
    if (it == m_layer_nav_params.end()) return;
    m_waterfall_win->applyNavToLine(it->second);
}

void MainWindow::onPaletteChanged(int idx)
{
    // Sync the Properties inspector (may be the source — setPalette uses QSignalBlocker).
    if (m_inspector)
        m_inspector->setPalette(idx);

    // Sync the waterfall (may be the source — setPalette checks current index first).
    if (m_waterfall_win)
        m_waterfall_win->setPalette(idx);

    // Sync the SBP window and keep the right panel in sync.
    if (m_sbp_win) {
        m_sbp_win->setPalette(idx);
        if (m_inspector)
            m_inspector->rightPanelHost()->setSbpParams(m_sbp_win->displayParams());
    }

    // Rebuild the map swath preview with the new palette.
    if (m_sss_ctrl)
        m_sss_ctrl->setPaletteIndex(idx);

    static const char* kPaletteNames[] = {
        "Thermal", "Greyscale", "Ocean", "Copper", "Inverted",
        "Viridis", "Plasma", "Midnight", "Sand", "Spectrum"
    };
    const char* name = (idx >= 0 && idx < 10) ? kPaletteNames[idx] : "Unknown";
    recordActivity(ActivityKind::Palette,
        tr("Palette changed to %1").arg(QLatin1String(name)));
}

} // namespace dolphin::ui
