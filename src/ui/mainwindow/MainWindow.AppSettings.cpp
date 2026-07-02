// MainWindow.AppSettings.cpp — applyLiveSettings() and onAppSettings().
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/systems/AppState.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/systems/WindowRegistry.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/shell/ViewerWindow.h"

#include <cstddef>

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QThreadPool>
#include <QTimer>
#include <algorithm>

namespace dolphin::ui {

namespace {
// Swallows QEvent::ToolTip at the application level when tooltips are disabled.
struct TooltipBlocker : QObject {
    bool eventFilter(QObject*, QEvent* e) override {
        return e->type() == QEvent::ToolTip;
    }
};
static TooltipBlocker* s_tooltip_blocker = nullptr;
} // namespace

void MainWindow::applyLiveSettings(const AppSettingsDialog::Settings& s)
{
    // Worker thread pool — takes effect for the next job submitted.
    QThreadPool::globalInstance()->setMaxThreadCount(s.worker_threads);

    // Tooltips — install/remove an app-level event filter that swallows ToolTip events.
    if (!s.show_tooltips) {
        if (!s_tooltip_blocker) {
            s_tooltip_blocker = new TooltipBlocker;
            qApp->installEventFilter(s_tooltip_blocker);
        }
    } else {
        if (s_tooltip_blocker) {
            qApp->removeEventFilter(s_tooltip_blocker);
            delete s_tooltip_blocker;
            s_tooltip_blocker = nullptr;
        }
    }

    if (m_viewport_host) {
        m_viewport_host->setShowGrid(s.show_grid);
        m_viewport_host->setMapBgColor(s.map_bg_color);
        const int gi = std::clamp(s.map_grid_preset, 0,
            int(std::size(AppSettingsDialog::kMapGridPresets)) - 1);
        const auto& gp = AppSettingsDialog::kMapGridPresets[gi];
        m_viewport_host->setGridColors(QColor(gp.line2d), QColor(gp.minor3d), QColor(gp.major3d));
        m_viewport_host->setGratLabelSize(s.grid_label_size);
        m_viewport_host->setGratLabelRotated(s.grid_label_rotated);
        m_viewport_host->setGratCoordFormat(s.graticule_coord);
    }

    // GPU acceleration — enable/disable the waterfall's OpenGL rendering path.
    if (m_waterfall_win) m_waterfall_win->setGpuAccel(s.gpu_accel);

    // Autosave timer — create on first call, restart or stop on subsequent calls.
    // The timeout routes to PSC which owns the project and dirty state.
    if (!m_autosave_timer) {
        m_autosave_timer = new QTimer(this);
        m_autosave_timer->setSingleShot(false);
        connect(m_autosave_timer, &QTimer::timeout,
                m_session_ctrl, &ProjectSessionController::autoSave);
    }
    if (s.autosave_min > 0)
        m_autosave_timer->start(s.autosave_min * 60 * 1000);
    else
        m_autosave_timer->stop();

    // Broadcast so every registered viewer can react to display-setting changes
    // (e.g. SidescanViewController repaletteAllLayers) without each knowing about
    // AppSettingsDialog.  Fine-grained signals (soundVelocityChanged, etc.) carry
    // the actual delta; DisplaySettingsChanged is the catch-all broadcast.
    if (m_window_registry)
        m_window_registry->broadcast(ViewerRefreshReason::DisplaySettingsChanged);
}

void MainWindow::refreshLoadingIndicator()
{
    if (!m_status_bar) return;
    const bool busy = m_window_registry && m_window_registry->anyViewerBusy();
    if (!busy) {
        m_map_progress_active = false;
        m_status_bar->hideProgress();
        m_status_bar->clearBusyText();
        return;
    }
    // Busy: if the active map build is reporting real 0→100 progress, leave its
    // determinate bar + "Building map…" label alone; otherwise (waterfall/sub-bottom
    // load) show the spinner with a generic label.
    if (!m_map_progress_active) {
        m_status_bar->setProgressIndeterminate();
        m_status_bar->setBusyText(tr("Loading…"));
    }
}

void MainWindow::onAppSettings()
{
    if (!m_app_settings_win) {
        auto* dlg = new AppSettingsDialog(m_app_state->current(), this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);

        // Route through AppState so fine-grained signals fire alongside the broad one.
        connect(dlg, &AppSettingsDialog::applied,
                m_app_state, &AppState::apply);

        connect(dlg, &QObject::destroyed,
                this, [this]() { m_app_settings_win = nullptr; });
        m_app_settings_win = dlg;

        // Apply persisted settings immediately so threads/timer are correct
        // even if the user never clicks Apply. Direct call — no signal needed.
        applyLiveSettings(m_app_state->current());
    }
    m_app_settings_win->show();
    m_app_settings_win->raise();
    m_app_settings_win->activateWindow();
}

} // namespace dolphin::ui
