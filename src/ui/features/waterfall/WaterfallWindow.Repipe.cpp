// WaterfallWindow.Repipe.cpp — contact overlay, cache invalidation, repipe, refresh.

#include "ui/features/waterfall/WaterfallWindow.h"
#include "app/layers/DataLayer.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/panels/WaterfallInspectorPanel.h"
#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/systems/AppState.h"

#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Navigation slots
// -----------------------------------------------------------------------------

void WaterfallWindow::onPrevFix()
{
    emit prevLineRequested(m_layer ? m_layer->id : std::string{});
}

void WaterfallWindow::onNextFix()
{
    emit nextLineRequested(m_layer ? m_layer->id : std::string{});
}

// -----------------------------------------------------------------------------
//  Contact overlay sync
// -----------------------------------------------------------------------------

void WaterfallWindow::setProjectContacts(std::vector<core::Contact> contacts)
{
    m_project_contacts = std::move(contacts);
    refreshContactOverlay();
}

void WaterfallWindow::refreshContactOverlay()
{
    if (!m_view || !m_layer) return;
    std::vector<core::Contact> filtered;
    for (const auto& c : m_project_contacts)
        if (c.line_id.empty() || c.line_id == m_layer->id)
            filtered.push_back(c);
    m_view->refreshExternalContacts(filtered, m_window_first_row);
}

// -----------------------------------------------------------------------------
//  Cache invalidation and repipe
// -----------------------------------------------------------------------------

void WaterfallWindow::invalidateProcessedCache()
{
    if (!m_view || m_view->rawPings().empty()) return;

    // Cancel any in-flight task immediately — no point finishing stale params.
    m_load_cancel.cancel();
    setDataState(ViewerDataState::Processing);
    startProgress();

    // Defer the actual launch so rapid param changes (slider drags) collapse
    // into a single pipeline run once the user settles.
    m_repipe_debounce->start();
}

void WaterfallWindow::onRepipeDebounce()
{
    if (!m_view || m_view->rawPings().empty()) return;

    m_load_cancel.reset();
    auto cancel = m_load_cancel;
    const int gen = ++m_load_gen;

    const WaterfallParams  params         = m_view->params();
    const SeabedAutoParams seabed_params  = m_analysis ? m_analysis->currentSeabedAutoParams()
                                                        : m_view->seabedAutoParams();
    const bool             seabed_enabled = m_view->seabedEnabled();
    auto raw = m_view->rawPings();   // copy raw pings for the background task

    struct Repipe { std::vector<core::SidescanPing> raw_pings;
                    WaterfallView::WfPipelineResult  pipeline; };
    auto* watcher = new QFutureWatcher<Repipe>(this);
    connect(watcher, &QFutureWatcher<Repipe>::finished,
            this, [this, watcher, gen]() {
                watcher->deleteLater();
                if (gen != m_load_gen) return;
                finishProgress();
                try {
                    auto r = watcher->result();
                    m_view->setPreassembledRows(std::move(r.raw_pings),
                                                std::move(r.pipeline),
                                                /*preserve_view=*/true);
                    pushParams();
                    setDataState(ViewerDataState::Ready);
                } catch (...) {
                    setDataState(ViewerDataState::Failed);
                }
            });
    watcher->setFuture(QtConcurrent::run(
        [r = std::move(raw), params, seabed_params, seabed_enabled, cancel]() mutable -> Repipe {
            if (cancel.isCancelled()) return {};
            Repipe out;
            out.pipeline  = WaterfallView::runPipeline(r, params, seabed_params, seabed_enabled);
            out.raw_pings = std::move(r);
            return out;
        }));
}

// -----------------------------------------------------------------------------
//  Layer reload and viewer refresh
// -----------------------------------------------------------------------------

void WaterfallWindow::reloadCurrentLayer()
{
    if (!m_layer) return;
    setLayer(m_layer, m_import_service, m_source_path, m_source_size_bytes);
}

void WaterfallWindow::onViewerRefresh(ViewerRefreshReason reason,
                                      const std::string&  layer_id)
{
    switch (reason) {
    case ViewerRefreshReason::LayerDataChanged:
    case ViewerRefreshReason::CrsChanged:
        if (layer_id.empty() || (m_layer && m_layer->id == layer_id))
            reloadCurrentLayer();
        break;
    case ViewerRefreshReason::DisplaySettingsChanged:
        // Sync palette to AppState's authoritative value, then push all visual
        // params to the view. No disk I/O — sound velocity is handled separately
        // via AppState::soundVelocityChanged → full reload.
        if (m_app_state && m_inspector)
            m_inspector->setPalette(m_app_state->current().default_palette);
        pushParams();
        break;
    case ViewerRefreshReason::ProjectReplaced:
        clearLayer();
        break;
    }
}

} // namespace dolphin::ui
