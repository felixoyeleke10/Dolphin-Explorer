// WaterfallWindow.Repipe.cpp — contact overlay, cache invalidation, repipe, refresh.

#include "ui/features/waterfall/WaterfallWindow.h"
#include "app/layers/DataLayer.h"
#include "app/tasks/OperationManager.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/panels/WaterfallInspectorPanel.h"
#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"

#include <QFutureWatcher>
#include <QSettings>
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
        if (c.visible && (c.line_id.empty() || c.line_id == m_layer->id))
            filtered.push_back(c);
    m_view->refreshExternalContacts(filtered, m_window_first_row);
}

// -----------------------------------------------------------------------------
//  Raw ping access
// -----------------------------------------------------------------------------

std::vector<core::SidescanPing> WaterfallWindow::currentRawPings() const
{
    if (!m_view) return {};
    auto pings = m_view->rawPings();           // copy raw pings from disk-load
    m_view->applySeabedPicksToPings(pings);   // overlay viewer seabed detection results
    return pings;
}

// -----------------------------------------------------------------------------
//  Cache invalidation and repipe
// -----------------------------------------------------------------------------

void WaterfallWindow::invalidateProcessedCache()
{
    if (!m_view || m_view->rawPings().empty()) return;

    // No explicit cancel — the keyed "wf:pipeline" op supersedes any in-flight
    // run when onRepipeDebounce launches the next one.
    setDataState(ViewerDataState::Processing);
    startProgress();

    // Defer the actual launch so rapid param changes (slider drags) collapse
    // into a single pipeline run once the user settles.
    m_repipe_debounce->start();
}

void WaterfallWindow::onRepipeDebounce()
{
    if (!m_view || m_view->rawPings().empty()) return;

    if (!m_op_mgr) return;

    const WaterfallParams  params         = m_view->params();
    const SeabedAutoParams seabed_params  = m_analysis ? m_analysis->currentSeabedAutoParams()
                                                        : m_view->seabedAutoParams();
    const bool             seabed_enabled = m_view->seabedEnabled();
    auto raw = m_view->rawPings();   // copy raw pings for the background task

    struct Repipe { std::vector<core::SidescanPing> raw_pings;
                    WaterfallView::WfPipelineResult  pipeline;
                    bool ok = false; };
    m_op_mgr->run<Repipe>(
        tr("Waterfall reprocessing"),
        [r = std::move(raw), params, seabed_params, seabed_enabled]
        (app::CancellationToken cancel) mutable -> Repipe {
            Repipe out;
            try {
                if (cancel.isCancelled()) return out;
                out.pipeline  = WaterfallView::runPipeline(r, params, seabed_params, seabed_enabled);
                out.raw_pings = std::move(r);
                out.ok = true;
            } catch (...) { out.ok = false; }
            return out;
        },
        [this](Repipe r) {
            finishProgress();
            if (!r.ok) { setDataState(ViewerDataState::Failed); return; }
            m_view->setPreassembledRows(std::move(r.raw_pings),
                                        std::move(r.pipeline),
                                        /*preserve_view=*/true);
            pushParams();
            setDataState(ViewerDataState::Ready);
        },
        "wf:pipeline",
        /*heavy=*/false);
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
        // Sync palette to the global SSS palette, then push all visual params to
        // the view. No disk I/O — sound velocity is handled separately via
        // AppState::soundVelocityChanged → full reload.
        if (m_inspector)
            m_inspector->setPalette(QSettings().value(QStringLiteral("sss/paletteIdx"),
                                                      PaletteIndex::Greyscale).toInt());
        pushParams();
        break;
    case ViewerRefreshReason::ProjectReplaced:
        clearLayer();
        break;
    }
}

} // namespace dolphin::ui
