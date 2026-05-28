// SubBottomWindow.Load.cpp — layer management and async data loading.

#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/mainwindow/AppState.h"
#include "ui/features/subbottom/SubBottomView.h"
#include "ui/features/subbottom/panels/SubBottomInspectorPanel.h"
#include "ui/features/subbottom/panels/SubBottomDisplayPanel.h"
#include "app/layers/DataLayer.h"
#include "app/services/ImportService.h"
#include "core/SubBottomTrace.h"
#include "geo/GeoUtils.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QProgressBar>
#include <QScrollBar>
#include <QtConcurrent/QtConcurrent>

#include <vector>

namespace dolphin::ui {

void SubBottomWindow::setLayer(app::DataLayer*     layer,
                               app::ImportService* import_service,
                               const std::string&  source_path,
                               uint64_t            source_size_bytes)
{
    m_layer             = layer;
    m_import_service    = import_service;
    m_source_path       = source_path;
    m_source_size_bytes = source_size_bytes;
    m_total_traces      = layer ? layer->subBottomCount() : 0;

    if (m_inspector && layer)
        m_inspector->setActiveLine(layer->id);

    m_view->clear();
    if (m_hscroll)   m_hscroll->setRange(0, 0);
    if (m_inspector) m_inspector->refresh(nullptr, {}, 0, 0, 0, 0.f, 0.f, 0.f, 0.f);

    if (!layer || !import_service || m_total_traces == 0) {
        if (m_status_left)
            m_status_left->setText(layer ? tr("No sub-bottom traces in this layer.")
                                         : tr("No layer loaded."));
        return;
    }

    if (m_status_left) m_status_left->setText(tr("⋯"));
    startProgress();

    m_load_cancel.cancel();
    m_load_cancel.reset();
    auto cancel = m_load_cancel;

    const int gen = ++m_load_gen;
    setDataState(ViewerDataState::Loading);

    // Snapshot fields — DataLayer must not be accessed on the bg thread.
    const std::string store_path   = layer->artifact_store_path;
    const std::string store_format = layer->artifact_store_format;
    const core::ArtifactIndex idx  = layer->artifact_index;
    auto* svc                      = import_service;

    struct LoadResult {
        std::vector<core::SubBottomTrace> traces;
        int   n_samples  = 0;
        float freq_hz    = 0.f;
        float dur_s      = 0.f;
        float line_len_m = 0.f;
    };

    auto* watcher = new QFutureWatcher<LoadResult>(this);
    connect(watcher, &QFutureWatcher<LoadResult>::finished,
            this, [this, watcher, gen]() {
                watcher->deleteLater();
                if (gen != m_load_gen) return;

                finishProgress();

                LoadResult res;
                try {
                    res = watcher->result();
                } catch (...) {
                    setDataState(ViewerDataState::Failed);
                    if (m_status_left)
                        m_status_left->setText(tr("Failed to load sub-bottom data."));
                    return;
                }

                if (m_status_left) m_status_left->clear();

                m_total_traces = static_cast<int>(res.traces.size());
                m_traces_raw   = std::move(res.traces);
                scheduleProcessing();

                if (m_inspector && m_layer) {
                    const float spd = m_display
                        ? m_display->currentParams().sound_speed_ms : 1500.f;
                    m_inspector->refresh(m_layer, m_source_path, m_source_size_bytes,
                                         m_total_traces, res.n_samples, res.dur_s,
                                         res.line_len_m, res.freq_hz, spd);
                }
            });

    watcher->setFuture(QtConcurrent::run(
        [svc, store_path, store_format, idx, source_path = m_source_path, cancel]() -> LoadResult {
            LoadResult res;
            res.traces = svc->loadAllSubBottomTraces(store_path, store_format, idx, source_path);
            if (cancel.isCancelled()) return {};

            const int n = static_cast<int>(res.traces.size());
            res.n_samples = n > 0 ? static_cast<int>(res.traces.front().samples.size()) : 0;
            res.freq_hz   = n > 0 ? res.traces.front().frequency_hz : 0.f;
            if (n >= 2)
                res.dur_s = static_cast<float>(
                    (res.traces.back().timestamp_us - res.traces.front().timestamp_us) * 1e-6);

            for (int i = 1; i < n; ++i) {
                const auto& a = res.traces[i - 1].nav;
                const auto& b = res.traces[i].nav;
                if (a.valid && b.valid)
                    res.line_len_m += static_cast<float>(geo::navDistanceMetres(a, b));
            }
            return res;
        }));
}

void SubBottomWindow::clearLayer()
{
    m_load_cancel.cancel();
    m_proc_cancel.cancel();
    m_load_gen++;
    m_proc_gen++;
    setDataState(ViewerDataState::Idle);
    m_layer             = nullptr;
    m_import_service    = nullptr;
    m_source_path.clear();
    m_total_traces      = 0;
    m_traces_raw.clear();
    m_view->clear();
    if (m_hscroll)      m_hscroll->setRange(0, 0);
    if (m_status_left)  m_status_left->clear();
    if (m_status_right) m_status_right->clear();
    if (m_progress_bar) m_progress_bar->setVisible(false);
    if (m_inspector)    m_inspector->refresh(nullptr, {}, 0, 0, 0, 0.f, 0.f, 0.f, 0.f);
}

const std::string& SubBottomWindow::currentLayerId() const
{
    static const std::string kEmpty;
    return m_layer ? m_layer->id : kEmpty;
}

void SubBottomWindow::reloadCurrentLayer()
{
    if (!m_layer) return;
    setLayer(m_layer, m_import_service, m_source_path, m_source_size_bytes);
}

void SubBottomWindow::onViewerRefresh(ViewerRefreshReason reason,
                                      const std::string&  layer_id)
{
    switch (reason) {
    case ViewerRefreshReason::LayerDataChanged:
    case ViewerRefreshReason::CrsChanged:
        if (layer_id.empty() || (m_layer && m_layer->id == layer_id))
            reloadCurrentLayer();
        break;
    case ViewerRefreshReason::DisplaySettingsChanged:
        // Apply palette from AppState. Sound velocity is handled separately
        // via setSoundVelocity(); other per-window params stay as-is.
        if (m_app_state)
            setPalette(m_app_state->current().default_palette);
        break;
    case ViewerRefreshReason::ProjectReplaced:
        clearLayer();
        break;
    }
}

} // namespace dolphin::ui
