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
#include "ui/shared/LineNavigation.h"
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
                        win->setProject(currentProject(), activeLayerId());
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
                    if (!currentProject() || !m_sbp_win) return;
                    const auto& layers = currentProject()->layers();
                    // The SBP viewer's own loaded line is the source of truth — not the
                    // app's active layer (which may be an SSS line).
                    const std::string ref = !from_id.empty() ? from_id
                                                             : m_sbp_win->currentLayerId();
                    int cur = -1;
                    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
                        if (layers[i]->id == ref) { cur = i; break; }
                    for (int i = cur - 1; i >= 0; --i) {
                        if (layers[i] && layers[i]->modality == app::Modality::SubBottom) {
                            onLayerSelected(layers[i]->id);
                            onSubBottomOpen();   // reload the open viewer to the new line
                            return;
                        }
                    }
                    appendJobMessage(tr("Already on the first sub-bottom line."));
                });

        connect(m_sbp_win, &SubBottomWindow::nextLineRequested,
                this, [this](const std::string& from_id) {
                    if (!currentProject() || !m_sbp_win) return;
                    const auto& layers = currentProject()->layers();
                    const std::string ref = !from_id.empty() ? from_id
                                                             : m_sbp_win->currentLayerId();
                    int cur = -1;
                    for (int i = 0; i < static_cast<int>(layers.size()); ++i)
                        if (layers[i]->id == ref) { cur = i; break; }
                    for (int i = cur + 1; i < static_cast<int>(layers.size()); ++i) {
                        if (layers[i] && layers[i]->modality == app::Modality::SubBottom) {
                            onLayerSelected(layers[i]->id);
                            onSubBottomOpen();   // reload the open viewer to the new line
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

        // Annotation tools — contact + feature picks create project entities.
        connect(m_sbp_win, &SubBottomWindow::contactCreated,
                this, &MainWindow::onSbpContactCreated);
        connect(m_sbp_win, &SubBottomWindow::featureCreated,
                this, &MainWindow::onWaterfallFeatureCreated);  // identical handling
        connect(m_sbp_win, &SubBottomWindow::clearAllContactsRequested,
                this, &MainWindow::onClearContacts);
        connect(m_sbp_win, &SubBottomWindow::contactEditRequested,
                this, &MainWindow::onContactEditRequested);
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

        // (The right-panel SBP Display section was removed — its view controls
        // live in the left Views panel's SBP tab. SBP gain/signal Apply is the
        // shared bottom Apply bar; Navigation/Geometry are wired at construction.)
    }

    // Populate the LINES list with every SBP layer in the current project.
    if (currentProject()) {
        std::vector<std::pair<std::string, std::string>> sbp_layers;
        for (const auto& l : currentProject()->layers())
            if (l && l->modality == app::Modality::SubBottom)
                sbp_layers.emplace_back(l->id, l->label);
        m_sbp_win->setProjectLayers(sbp_layers);
    }

    // Resolve which SBP line to show: the active layer if it is sub-bottom, else the
    // first indexed sub-bottom line. Opening the viewer from the toolbar/menu while an
    // SSS (or other) layer is active must still land on a real SBP line.
    std::string sbp_id;
    if (currentProject()) {
        if (auto* al = currentProject()->findLayer(activeLayerId());
            al && al->modality == app::Modality::SubBottom)
            sbp_id = activeLayerId();
        else
            for (const auto& l : currentProject()->layers())
                if (l && l->modality == app::Modality::SubBottom
                        && l->index_built && l->subBottomCount() > 0) {
                    sbp_id = l->id;
                    break;
                }
    }

    if (!sbp_id.empty()) {
        // Sync app state/map/inspector/tab to the SBP line (no-op if already active;
        // does not re-enter onSubBottomOpen).
        if (sbp_id != activeLayerId()) onLayerSelected(sbp_id);
        auto* layer = currentProject()->findLayer(sbp_id);
        if (layer && layer->modality == app::Modality::SubBottom) {
            const auto* src = currentProject()->findSource(layer->source_id);
            m_sbp_win->setLayer(layer,
                                src ? src->path : std::string{},
                                src ? src->size_bytes : 0);
            m_sbp_win->setProjectContacts(currentProject()->contacts());
            // Restore per-layer SBP display params; palette always wins if set.
            if (layer->sbp_display_state.display_customized)
                m_sbp_win->restoreDisplayParams(layer->sbp_display_state.display);
            if (layer->sbp_palette >= 0)
                m_sbp_win->setPalette(layer->sbp_palette);
            // Restore per-layer processing params; sync right-panel modules.
            if (layer->sbp_display_state.gain_customized)
                m_sbp_win->applyGainParams(layer->sbp_display_state.gain);
            if (layer->sbp_display_state.signal_customized)
                m_sbp_win->applySignalParams(layer->sbp_display_state.signal);
            applyStoredSbpNavParams(layer->id);  // stored nav corrections
            refreshViewsPanel();   // Views ▸ SBP mirrors this line's display params
        }
    }

    // Reflect Prev/Next availability for the VIEWER's current line (its loaded line is
    // the source of truth, not the app's active layer).
    {
        // Match the LINES list (populated by modality) so the buttons reflect the SBP
        // lines the user actually sees — not only those whose index is currently loaded.
        const auto nav = computeLineNav(currentProject(), m_sbp_win->currentLayerId(),
            [](const app::DataLayer& l) { return l.modality == app::Modality::SubBottom; });
        m_sbp_win->setLineNavEnabled(nav.has_prev, nav.has_next);
    }

    m_sbp_win->show();
    m_sbp_win->raise();
    m_sbp_win->activateWindow();
}

} // namespace dolphin::ui
