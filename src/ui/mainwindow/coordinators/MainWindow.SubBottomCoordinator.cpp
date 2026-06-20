// MainWindow.SubBottomCoordinator.cpp — SubBottomWindow lifecycle and state reflection.

#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/coordinators/CorrectionBatchOperator.h"
#include "ui/systems/AppState.h"
#include "ui/shell/ViewerWindow.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
// m_modal_host (ModalOnly RightPanelHost) owns all SBP and SSS panel modules.
#include "ui/mainwindow/rightpanel/RightPanel.SbpGain.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpSignal.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/subbottom/SubBottomSettingsDialog.h"
#include "ui/features/subbottom/SubBottomViewStyle.h"
#include "ui/features/map/MapView.h"
#include "ui/features/metadata/SBPMetadataWindow.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <algorithm>

namespace dolphin::ui {

void MainWindow::onSubBottomOpen()
{
    // Guard: require at least one sub-bottom layer in the project so the
    // viewer never opens into a permanently-empty/invalid state.
    if (!currentProject()) {
        appendJobMessage(tr("Open a project first."));
        return;
    }
    const bool has_sbp = std::any_of(currentProject()->layers().begin(),
                                     currentProject()->layers().end(),
                                     [](const auto& l) {
                                         return l
                                             && l->modality == app::Modality::SubBottom
                                             && l->index_built
                                             && l->subBottomCount() > 0;
                                     });
    if (!has_sbp) {
        appendJobMessage(tr("No indexed sub-bottom data in the current project."));
        return;
    }

    if (!m_sbp_win) {
        m_sbp_win = new SubBottomWindow(m_app_state, nullptr);
        m_sbp_win->setOperationManager(m_op_mgr);  // owns load/process ops (keyed)
        m_window_registry->registerViewer(m_sbp_win, m_sbp_win);
        connect(m_sbp_win, &SubBottomWindow::newFileRequested,
                this, &MainWindow::onImportFile);
        connect(m_sbp_win, &SubBottomWindow::openFileRequested,
                this, &MainWindow::onOpenProject);
        connect(m_sbp_win, &SubBottomWindow::saveFileRequested,
                this, &MainWindow::onSaveProject);

        connect(m_sbp_win, &SubBottomWindow::metadataRequested,
                this, [this] {
                    if (!m_sbp_metadata_win) {
                        auto* win = new SBPMetadataWindow(nullptr);
                        win->setAttribute(Qt::WA_DeleteOnClose);
                        m_sbp_metadata_win = win;
                    }
                    auto* win = qobject_cast<SBPMetadataWindow*>(m_sbp_metadata_win);
                    if (win)
                        win->setProject(currentProject(), m_import_service,
                                        activeLayerId());
                    m_sbp_metadata_win->show();
                    m_sbp_metadata_win->raise();
                    m_sbp_metadata_win->activateWindow();
                });

        connect(m_sbp_win, &SubBottomWindow::settingsRequested,
                this, [this] {
                    if (!m_sbp_win) return;
                    SubBottomSettingsDialog::ViewScale scale;
                    scale.px_per_trace  = m_sbp_win->pxPerTrace();
                    scale.px_per_sample = m_sbp_win->pxPerSample();
                    auto* dlg = new SubBottomSettingsDialog(
                        m_sbp_win->displayParams(), scale,
                        m_sbp_win->viewStyle(),
                        static_cast<QWidget*>(m_sbp_win));
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    connect(dlg, &SubBottomSettingsDialog::applied,
                            this, [this](SubBottomDisplayParams p, int pw, float ps,
                                         SubBottomViewStyle style) {
                                if (!m_sbp_win) return;
                                m_sbp_win->applySettings(p, pw, ps, style);
                                // Keep right panel in sync — it only syncs on window open otherwise.
                                if (m_modal_host)
                                    m_modal_host->setSbpParams(m_sbp_win->displayParams());
                                // Persist per-layer SBP palette from settings dialog
                                // through the display-state authority (marks dirty on the bus).
                                if (!currentProject() || activeLayerId().empty()) return;
                                if (m_display_state)
                                    m_display_state->setLayerSbpPalette(activeLayerId(),
                                                                        p.palette_index);
                            });
                    dlg->show();
                });

        connect(m_sbp_win, &SubBottomWindow::prevLineRequested,
                this, [this](const std::string& from_id) {
                    if (!currentProject()) return;
                    const auto& layers = currentProject()->layers();
                    const std::string& ref = from_id.empty() ? activeLayerId() : from_id;
                    int cur = -1;
                    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
                        if (layers[i]->id == ref) { cur = i; break; }
                    for (int i = cur - 1; i >= 0; --i) {
                        if (layers[i]->index_built && layers[i]->subBottomCount() > 0) {
                            onLayerSelected(layers[i]->id);
                            return;
                        }
                    }
                    appendJobMessage(tr("Already on the first sub-bottom line."));
                });

        connect(m_sbp_win, &SubBottomWindow::nextLineRequested,
                this, [this](const std::string& from_id) {
                    if (!currentProject()) return;
                    const auto& layers = currentProject()->layers();
                    const std::string& ref = from_id.empty() ? activeLayerId() : from_id;
                    int cur = -1;
                    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
                        if (layers[i]->id == ref) { cur = i; break; }
                    for (int i = cur + 1; i < static_cast<int>(layers.size()); ++i) {
                        if (layers[i]->index_built && layers[i]->subBottomCount() > 0) {
                            onLayerSelected(layers[i]->id);
                            return;
                        }
                    }
                    appendJobMessage(tr("Already on the last sub-bottom line."));
                });

        connect(m_sbp_win, &SubBottomWindow::cursorUpdated,
                this, [this](float depth_m, double lat, double lon, bool is_projected) {
                    if (!m_status_bar) return;
                    // Only the live coordinate is shown; the cursor-depth readout was
                    // removed (it floated over the project name).
                    if (depth_m < 0.f) {
                        m_status_bar->clearCursorPosition();
                        return;
                    }
                    if (lat != 0.0 || lon != 0.0)
                        showCursorPosition(lat, lon, is_projected);
                });

        connect(m_sbp_win, &SubBottomWindow::layerChangeRequested,
                this, [this](const std::string& id) { onLayerSelected(id); });
        // Busy-state → status-bar spinner. Cancellation is owned by
        // OperationManager (the window's load/process ops are keyed), so no
        // external-token registration is needed.
        connect(m_sbp_win, &SubBottomWindow::dataStateChanged,
                this, [this](ViewerDataState) { refreshLoadingIndicator(); });

        // Propagate global sound velocity so the depth axis stays correct
        // without requiring a full disk reload.
        if (m_app_state) {
            connect(m_app_state, &AppState::soundVelocityChanged,
                    this, [this](double sv) {
                        if (m_sbp_win) m_sbp_win->setSoundVelocity(sv);
                    });
        }

        // Right-panel Display module → SubBottomWindow: push param changes live.
        if (m_modal_host) {
            auto* host = m_modal_host;
            connect(host, &RightPanelHost::sbpParamsChanged,
                    this, [this](SubBottomDisplayParams p) {
                        if (m_sbp_win) m_sbp_win->applyDisplayParams(p);
                        // Persist per-layer SBP display params through the display-state
                        // authority (writes the model + palette, emits, marks dirty).
                        if (!currentProject() || activeLayerId().empty()) return;
                        if (m_display_state)
                            m_display_state->setLayerSbpDisplay(activeLayerId(), p);
                    });

            if (auto* gain_mod = host->sbpGainModule()) {
                connect(gain_mod, &SbpGainModule::applyToLineRequested,
                        this, [this](SbpGainParams p) {
                            // Display-state only (SeaView model): store on the active
                            // layer and push live to the SBP window. No .dlpd bake —
                            // committing to disk is Processing → "Bake Corrections".
                            std::string lid = activeLayerId();
                            if (lid.empty() && m_sbp_win) lid = m_sbp_win->currentLayerId();
                            if (lid.empty() || !currentProject()) return;
                            if (m_display_state)
                                m_display_state->setLayerSbpGain(lid, p);
                            if (m_sbp_win && m_sbp_win->currentLayerId() == lid)
                                m_sbp_win->applyGainParams(p);
                        });
                connect(gain_mod, &SbpGainModule::applyToAllRequested,
                        this, [this](SbpGainParams p) {
                            if (!currentProject()) return;
                            // Display-state only: store on every SBP layer + refresh the
                            // open window live. No .dlpd bake (see Bake Corrections).
                            if (m_display_state) m_display_state->setAllSbpGain(p);
                            if (m_sbp_win) m_sbp_win->applyGainParams(p);
                        });
            }
            if (auto* sig_mod = host->sbpSignalModule()) {
                connect(sig_mod, &SbpSignalModule::applyToLineRequested,
                        this, [this](SbpSignalParams p) {
                            // Display-state only: store on the active layer + push live.
                            std::string lid = activeLayerId();
                            if (lid.empty() && m_sbp_win) lid = m_sbp_win->currentLayerId();
                            if (lid.empty() || !currentProject()) return;
                            if (m_display_state)
                                m_display_state->setLayerSbpSignal(lid, p);
                            if (m_sbp_win && m_sbp_win->currentLayerId() == lid)
                                m_sbp_win->applySignalParams(p);
                        });
                connect(sig_mod, &SbpSignalModule::applyToAllRequested,
                        this, [this](SbpSignalParams p) {
                            if (!currentProject()) return;
                            // Display-state only: store on every SBP layer + refresh the
                            // open window live. No .dlpd bake (see Bake Corrections).
                            if (m_display_state) m_display_state->setAllSbpSignal(p);
                            if (m_sbp_win) m_sbp_win->applySignalParams(p);
                        });
            }

            // SBP Navigation / Geometry panels are wired once at construction (see
            // MainWindow.MainArea.cpp) so Apply works from the main view even when
            // this window is closed; the handlers store per-layer params, rebuild
            // the map profile(s), and refresh this window only if it is open.
        }
    }

    // Populate the LINES list with every SBP layer in the current project.
    if (currentProject()) {
        std::vector<std::pair<std::string, std::string>> sbp_layers;
        for (const auto& l : currentProject()->layers())
            if (l && l->modality == app::Modality::SubBottom)
                sbp_layers.emplace_back(l->id, l->label);
        m_sbp_win->setProjectLayers(sbp_layers);
    }

    if (currentProject() && !activeLayerId().empty()) {
        auto* layer = currentProject()->findLayer(activeLayerId());
        if (layer && layer->modality == app::Modality::SubBottom) {
            const auto* src = currentProject()->findSource(layer->source_id);
            m_sbp_win->setLayer(layer, m_import_service,
                                src ? src->path : std::string{},
                                src ? src->size_bytes : 0);
            // Restore per-layer SBP display params; palette always wins if set.
            if (layer->sbp_display_state.display_customized)
                m_sbp_win->applyDisplayParams(layer->sbp_display_state.display);
            if (layer->sbp_palette >= 0)
                m_sbp_win->setPalette(layer->sbp_palette);
            // Restore per-layer processing params; sync right-panel modules.
            if (layer->sbp_display_state.gain_customized)
                m_sbp_win->applyGainParams(layer->sbp_display_state.gain);
            if (layer->sbp_display_state.signal_customized)
                m_sbp_win->applySignalParams(layer->sbp_display_state.signal);
            applyStoredSbpNavParams(layer->id);  // stored nav corrections
            if (m_modal_host) {
                if (layer->sbp_display_state.display_customized)
                    m_modal_host->setSbpParams(layer->sbp_display_state.display);
                if (auto* gm = m_modal_host->sbpGainModule(); layer->sbp_display_state.gain_customized)
                    gm->setParams(layer->sbp_display_state.gain);
                if (auto* sm = m_modal_host->sbpSignalModule(); layer->sbp_display_state.signal_customized)
                    sm->setParams(layer->sbp_display_state.signal);
                // Sync to SBP window's current display settings.
                m_modal_host->setSbpParams(m_sbp_win->displayParams());
            }
        }
    }

    m_sbp_win->show();
    m_sbp_win->raise();
    m_sbp_win->activateWindow();
}

} // namespace dolphin::ui
