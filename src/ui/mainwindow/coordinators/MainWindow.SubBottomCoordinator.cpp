// MainWindow.SubBottomCoordinator.cpp — SubBottomWindow lifecycle and state reflection.

#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/AppState.h"
#include "ui/shell/ViewerWindow.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpGain.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpSignal.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/subbottom/SubBottomSettingsDialog.h"
#include "ui/features/subbottom/SubBottomViewStyle.h"
#include "ui/features/map/MapView.h"
#include "ui/features/metadata/SBPMetadataWindow.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

namespace dolphin::ui {

void MainWindow::onSubBottomOpen()
{
    if (!m_sbp_win) {
        m_sbp_win = new SubBottomWindow(m_app_state, nullptr);
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
                        win->setProject(m_project.get(), m_import_service,
                                        m_active_layer_id);
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
                                if (m_inspector)
                                    m_inspector->rightPanelHost()->setSbpParams(m_sbp_win->displayParams());
                            });
                    dlg->show();
                });

        connect(m_sbp_win, &SubBottomWindow::prevLineRequested,
                this, [this](const std::string& from_id) {
                    if (!m_project) return;
                    const auto& layers = m_project->layers();
                    const std::string& ref = from_id.empty() ? m_active_layer_id : from_id;
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
                    if (!m_project) return;
                    const auto& layers = m_project->layers();
                    const std::string& ref = from_id.empty() ? m_active_layer_id : from_id;
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
                    if (depth_m < 0.f) {
                        m_status_bar->clearCursorDepth();
                        m_status_bar->clearCursorPosition();
                        return;
                    }
                    m_status_bar->setCursorDepth(depth_m);
                    if (lat != 0.0 || lon != 0.0)
                        m_status_bar->setCursorPosition(lat, lon, is_projected);
                });

        connect(m_sbp_win, &SubBottomWindow::layerChangeRequested,
                this, [this](const std::string& id) { onLayerSelected(id); });
        connect(m_sbp_win, &SubBottomWindow::dataStateChanged,
                this, [this](ViewerDataState s) {
                    refreshLoadingIndicator();
                    if (s == ViewerDataState::Loading)
                        m_op_mgr->registerExternal("sbp", m_sbp_win->loadToken());
                    else if (s == ViewerDataState::Processing)
                        m_op_mgr->registerExternal("sbp", m_sbp_win->procToken());
                    else
                        m_op_mgr->unregisterExternal("sbp");
                });

        // Propagate global sound velocity so the depth axis stays correct
        // without requiring a full disk reload.
        if (m_app_state) {
            connect(m_app_state, &AppState::soundVelocityChanged,
                    this, [this](double sv) {
                        if (m_sbp_win) m_sbp_win->setSoundVelocity(sv);
                    });
        }

        // Right-panel Display module → SubBottomWindow: push param changes live.
        if (m_inspector) {
            auto* host = m_inspector->rightPanelHost();
            connect(host, &RightPanelHost::sbpParamsChanged,
                    this, [this](SubBottomDisplayParams p) {
                        if (m_sbp_win) m_sbp_win->applyDisplayParams(p);
                    });

            if (auto* gain_mod = host->sbpGainModule()) {
                connect(gain_mod, &SbpGainModule::applyToLineRequested,
                        this, [this](SbpGainParams p) {
                            if (m_sbp_win) m_sbp_win->applyGainParams(p);
                        });
                connect(gain_mod, &SbpGainModule::applyToAllRequested,
                        this, [this](SbpGainParams p) {
                            if (m_sbp_win) m_sbp_win->applyGainParams(p);
                        });
            }
            if (auto* sig_mod = host->sbpSignalModule()) {
                connect(sig_mod, &SbpSignalModule::applyToLineRequested,
                        this, [this](SbpSignalParams p) {
                            if (m_sbp_win) m_sbp_win->applySignalParams(p);
                        });
                connect(sig_mod, &SbpSignalModule::applyToAllRequested,
                        this, [this](SbpSignalParams p) {
                            if (m_sbp_win) m_sbp_win->applySignalParams(p);
                        });
            }
        }
    }

    // Populate the LINES list with every SBP layer in the current project.
    if (m_project) {
        std::vector<std::pair<std::string, std::string>> sbp_layers;
        for (const auto& l : m_project->layers())
            if (l && l->modality == app::Modality::SubBottom)
                sbp_layers.emplace_back(l->id, l->label);
        m_sbp_win->setProjectLayers(sbp_layers);
    }

    if (m_project && !m_active_layer_id.empty()) {
        auto* layer = m_project->findLayer(m_active_layer_id);
        if (layer && layer->modality == app::Modality::SubBottom) {
            const auto* src = m_project->findSource(layer->source_id);
            m_sbp_win->setLayer(layer, m_import_service,
                                src ? src->path : std::string{},
                                src ? src->size_bytes : 0);
            // Sync right panel to reflect SBP window's current display settings.
            if (m_inspector)
                m_inspector->rightPanelHost()->setSbpParams(m_sbp_win->displayParams());
        }
    }

    m_sbp_win->show();
    m_sbp_win->raise();
    m_sbp_win->activateWindow();
}

} // namespace dolphin::ui
