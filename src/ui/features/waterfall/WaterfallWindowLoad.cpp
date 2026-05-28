// WaterfallWindowLoad.cpp — layer management, async data loading, navigation slots

#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/mainwindow/AppState.h"
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/panels/WaterfallInspectorPanel.h"
#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/features/waterfall/processing/SeabedAutoDetector.h"
#include "app/layers/DataLayer.h"
#include "app/layers/LayerUtils.h"
#include "app/services/ImportService.h"
#include "core/Artifact.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"

#include <QComboBox>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QLabel>
#include <QProgressBar>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dolphin::ui {

namespace {

// Count the sidescan entries that belong to the selected frequency band.
// The bands come from per-entry frequency_hz, matching the display loader.
static int countSidescanForBand(const app::DataLayer* layer, float target_hz)
{
    if (!layer) return 0;
    if (target_hz <= 0.f) return layer->sidescanCount();

    const auto bands = app::sidescanFrequencyBands(layer->artifact_index);
    if (bands.size() < 2)
        return layer->sidescanCount();

    const float target_band = app::nearestFrequencyBand(bands, target_hz);
    int count = 0;
    for (const auto& e : layer->artifact_index.entries) {
        if (e.type != core::ArtifactType::Sidescan) continue;
        if (e.frequency_hz > 0.f && std::fabs(e.frequency_hz - target_band) < 1.f)
            ++count;
    }
    return count;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Data API
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallWindow::setLayer(app::DataLayer*     layer,
                               app::ImportService* import_service,
                               const std::string&  source_path,
                               uint64_t            source_size_bytes)
{
    m_scroll_debounce->stop();
    m_pending_abs_row   = -1;
    m_layer             = layer;
    m_import_service    = import_service;
    m_source_path       = source_path;
    m_source_size_bytes = source_size_bytes;

    const std::vector<float> bands = layer
        ? app::sidescanFrequencyBands(layer->artifact_index)
        : std::vector<float>{};
    const float preferred_hz = (layer && layer->frequency_hz > 0.f)
        ? layer->frequency_hz
        : 0.f;

    // A layer with low_frequency_hz == 0 and frequency_hz > 0 is a pinned
    // single-band layer (_HF or _LF from split import).  The waterfall always
    // filters to its band; the selector is hidden so the user cannot switch bands.
    const bool is_pinned_band = layer
        && layer->low_frequency_hz == 0.f
        && layer->frequency_hz > 0.f;

    m_selected_frequency_hz = (is_pinned_band || bands.size() < 2)
        ? preferred_hz
        : app::nearestFrequencyBand(bands, preferred_hz);

    if (m_freq_selector) {
        QSignalBlocker sb(m_freq_selector);
        m_freq_selector->clear();
        if (!is_pinned_band && bands.size() >= 2) {
            auto kHz = [](float hz) -> QString {
                return QString("%1 kHz").arg(static_cast<int>(hz / 1000.f + 0.5f));
            };
            for (size_t i = 0; i < bands.size(); ++i) {
                QString label;
                if (i == 0)
                    label = tr("HF ") + kHz(bands[i]);
                else if (i + 1 == bands.size())
                    label = tr("LF ") + kHz(bands[i]);
                else
                    label = tr("Band ") + kHz(bands[i]);
                m_freq_selector->addItem(label, QVariant(bands[i]));
            }
            const auto selected_it = std::find_if(bands.begin(), bands.end(),
                [&](float hz) { return std::fabs(hz - m_selected_frequency_hz) < 1.f; });
            if (selected_it != bands.end()) {
                m_freq_selector->setCurrentIndex(
                    static_cast<int>(std::distance(bands.begin(), selected_it)));
            }
            m_freq_selector->setVisible(true);
        } else {
            m_freq_selector->setVisible(false);
        }
    }

    m_total_ssc_entries = countSidescanForBand(m_layer, m_selected_frequency_hz);
    m_window_first_row  = 0;
    m_entries_per_row   = 1.0f;
    m_reset_view_next   = true;

    // Clear window-local contact overlays so picks from the previous layer
    // don't bleed through onto the new waterfall image.
    resetContactTool();

    // Clear stale seabed picks from the previous layer; detection stays enabled
    // so it auto-runs when the new layer's pings arrive via setPings().
    m_view->resetSeabedForNewLayer();

    refreshInspector();

    // Restore the per-layer SRC state so the analysis panel and view reflect the
    // persisted flag before loadWindow triggers pushParams.
    if (layer && m_analysis) {
        WaterfallParams p = m_analysis->currentParams(
            m_inspector ? m_inspector->currentPaletteIndex() : 0);
        if (p.slant_range_correction != layer->slant_range_corrected) {
            p.slant_range_correction = layer->slant_range_corrected;
            m_analysis->setParams(p);
        }
    }

    // Start at the beginning of each new layer so Prev/Next navigation
    // always lands at the top of the waterfall, not some arbitrary midpoint.
    loadWindow(0);
}

void WaterfallWindow::resetContactTool()
{
    // Three surfaces must stay in sync: view tool state, panel pick button,
    // toolbar pick button.  setContactPickActive uses QSignalBlocker so it
    // doesn't re-emit contactToolChanged — we must update m_btn_contact directly.
    m_view->clearContacts();
    m_view->setContactTool(0);
    if (m_analysis) m_analysis->setContactPickActive(false);
    if (m_btn_contact) {
        QSignalBlocker sb(m_btn_contact);
        m_btn_contact->setChecked(false);
    }
}

void WaterfallWindow::clearLayer()
{
    m_scroll_debounce->stop();
    m_pending_abs_row = -1;
    m_load_cancel.cancel();
    m_load_gen++;
    setDataState(ViewerDataState::Idle);
    m_layer                   = nullptr;
    m_import_service          = nullptr;
    m_source_path.clear();
    m_selected_frequency_hz   = 0.f;
    m_total_ssc_entries       = 0;
    m_window_first_row        = 0;
    if (m_freq_selector) {
        QSignalBlocker sb(m_freq_selector);
        m_freq_selector->clear();
        m_freq_selector->setVisible(false);
    }
    m_status_left->clear();
    finishProgress();
    m_view->clear();
    resetContactTool();
    m_vscroll->setRange(0, 0);
    refreshInspector();
}

void WaterfallWindow::refreshInspector()
{
    if (m_inspector)
        m_inspector->refresh(m_layer, m_source_path, m_source_size_bytes,
                             m_total_ssc_entries, m_entries_per_row,
                             m_view->samplesPerPing(),
                             m_view->lineLengthMetres(),
                             m_view->frequencyHz(),
                             m_layer ? m_layer->sonar_name : std::string{},
                             m_view->soundVelocityMs());
}

void WaterfallWindow::pushParams()
{
    if (!m_view || !m_analysis || !m_inspector) return;
    WaterfallParams p = m_analysis->currentParams(m_inspector->currentPaletteIndex());
    p.display_channel = m_display_channel;
    // Seabed line is only meaningful before SRC is applied; hide it once corrected.
    m_view->setShowSeabedLine(!p.slant_range_correction);
    m_view->setParams(p);
}

void WaterfallWindow::setPalette(int idx)
{
    if (!m_inspector) return;
    if (m_inspector->currentPaletteIndex() == idx) return;  // already set — skip pushParams
    m_inspector->setPalette(idx);   // updates combo without re-emitting
    pushParams();
}

void WaterfallWindow::scheduleNavProcessing(const NavProcessingParams& nav)
{
    if (!m_view || m_view->rawPings().empty()) return;

    m_load_cancel.cancel();
    m_load_cancel.reset();
    auto cancel = m_load_cancel;
    const int gen = ++m_load_gen;
    setDataState(ViewerDataState::Processing);
    startProgress();

    const WaterfallParams  params  = m_view->params();
    const SeabedAutoParams seabed  = m_view->seabedAutoParams();
    const bool seabed_en           = m_view->seabedEnabled();
    auto raw = m_view->rawPings();

    struct Repipe {
        std::vector<core::SidescanPing> raw_pings;
        WaterfallView::WfPipelineResult pipeline;
    };
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
        [r = std::move(raw), nav, params, seabed, seabed_en, cancel]() mutable -> Repipe {
            if (cancel.isCancelled()) return {};
            r = WaterfallView::runNavCorrections(std::move(r), nav);
            if (cancel.isCancelled()) return {};
            Repipe out;
            out.pipeline  = WaterfallView::runPipeline(r, params, seabed, seabed_en);
            out.raw_pings = std::move(r);
            return out;
        }));
}

void WaterfallWindow::applyNavToLine(const NavProcessingParams& p)
{
    scheduleNavProcessing(p);
}

void WaterfallWindow::applyNavToAll(const NavProcessingParams& p)
{
    scheduleNavProcessing(p);
    emit navProcessAllLinesRequested(p);
}

const WaterfallParams& WaterfallWindow::currentParams() const
{
    return m_view->params();
}

const std::string& WaterfallWindow::currentLayerId() const
{
    static const std::string kEmpty;
    return m_layer ? m_layer->id : kEmpty;
}

WaterfallSettingsDialog::Settings WaterfallWindow::wfSettings() const
{
    // Start with persisted defaults (covers overlay params), then override
    // with the live state for params we store in member variables.
    WaterfallSettingsDialog::Settings s = WaterfallSettingsDialog::loadDefaults();
    s.window_size     = m_window_size;
    s.display_channel = m_display_channel;
    if (m_view)
        s.overlay = m_view->overlayParams();
    return s;
}

void WaterfallWindow::applyWfSettings(const WaterfallSettingsDialog::Settings& s)
{
    m_window_size     = s.window_size;
    m_display_channel = s.display_channel;
    if (m_view) {
        m_view->setShowAmpBar(s.show_amp_bar);
        m_view->setOverlayParams(s.overlay);
        pushParams();  // applies display_channel to the live view immediately
    }
}

void WaterfallWindow::applyExternalParams(const WaterfallParams& p)
{
    if (!m_view) return;
    if (m_analysis) m_analysis->setParams(p);
    m_view->setShowSeabedLine(!p.slant_range_correction);

    // Params that affect the processing pipeline (TVG/ARC/AGC/ARN/destripe/
    // beam-pattern/ML) must run off the UI thread.  Display-only changes
    // (palette, gain, contrast, SRC flag, display channel) are fast-path.
    const WaterfallParams& cur = m_view->params();
    const bool needs_repipe = !m_view->rawPings().empty()
        && ((p.agc          != cur.agc)
         || (p.tvg          != cur.tvg)
         || (p.arn          != cur.arn)
         || (p.destripe     != cur.destripe)
         || (p.beam_pattern != cur.beam_pattern)
         || (p.arc          != cur.arc)
         || (p.ml_enhance   != cur.ml_enhance));

    if (needs_repipe) {
        m_view->setParamsNoRebuild(p);   // commit new params; skip sync rebuild
        invalidateProcessedCache();      // schedule async re-pipeline
    } else {
        m_view->setParams(p);
    }
    flashProgress();
    emit paramsApplied();
}

void WaterfallWindow::applyExternalParamsToAll(const WaterfallParams& p)
{
    applyExternalParams(p);
    emit applyToAllRequested();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Async data loading
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallWindow::loadWindow(int abs_row)
{
    if (!m_layer || !m_import_service) {
        m_view->clear();
        m_vscroll->setRange(0, 0);
        return;
    }

    m_load_cancel.cancel();
    m_load_cancel.reset();
    auto cancel = m_load_cancel;

    const int gen = ++m_load_gen;
    setDataState(ViewerDataState::Loading);
    m_status_left->setText("\u22ef");   // ⋯ — minimal "loading" indicator
    startProgress();

    // Snapshot plain-data fields — DataLayer is a QObject and must not be
    // accessed from a background thread.
    const std::string store_path   = m_layer->artifact_store_path;
    const std::string store_format = m_layer->artifact_store_format;
    const core::ArtifactIndex idx  = m_layer->artifact_index;
    const std::string source_path  = m_source_path;
    auto* svc                      = m_import_service;
    const int total_entries        = m_total_ssc_entries;

    const float epr         = m_entries_per_row;
    const int center_entry  = std::clamp(
        static_cast<int>(abs_row * epr), 0, std::max(0, total_entries - 1));
    const int first_entry   = std::max(0,
        std::min(center_entry - m_window_size / 2, total_entries - m_window_size));
    const int entries_in_window = std::min(m_window_size, total_entries - first_entry);
    const int target_local  = std::max(0,
        static_cast<int>((center_entry - first_entry) / epr));

    const bool reset_view   = m_reset_view_next;
    m_reset_view_next       = false;

    // Capture the layer's source CRS so the background thread can apply it to
    // pings that don't carry an exact spatial_ref of their own.
    core::SpatialRef layer_src_ref = m_layer->source_spatial_ref;
    const bool apply_layer_crs =
        layer_src_ref.exact && core::spatialRefIsProjected(layer_src_ref);

    // Snapshot display params and seabed config so the background pipeline
    // can assemble and detect seabed without touching UI-thread state.
    const WaterfallParams  snap_params         = m_view->params();
    const SeabedAutoParams snap_seabed_params  = m_view->seabedAutoParams();
    const bool             snap_seabed_enabled = m_view->seabedEnabled();

    struct LoadResult {
        std::vector<core::SidescanPing>  raw_pings;
        WaterfallView::WfPipelineResult  pipeline;
    };

    auto* watcher = new QFutureWatcher<LoadResult>(this);
    connect(watcher, &QFutureWatcher<LoadResult>::finished,
            this, [this, watcher, gen, first_entry, entries_in_window,
                   target_local, reset_view, snap_params]() {
                watcher->deleteLater();
                if (gen != m_load_gen) return;

                m_status_left->clear();
                finishProgress();

                LoadResult res;
                try {
                    res = watcher->result();
                } catch (...) {
                    setDataState(ViewerDataState::Failed);
                    m_status_left->setText(tr("Failed to load data"));
                    return;
                }

                // Detect pipeline params that changed while the window was loading.
                // Display-only params (palette, contrast, stretch) are handled by
                // pushParams() and don't require row reassembly.
                bool pipeline_stale = false;
                WaterfallParams current_params;
                if (m_analysis && m_inspector) {
                    current_params = m_analysis->currentParams(m_inspector->currentPaletteIndex());
                    current_params.display_channel = m_display_channel;
                    pipeline_stale =
                        current_params.tvg                    != snap_params.tvg
                        || current_params.agc                 != snap_params.agc
                        || current_params.arc                 != snap_params.arc
                        || current_params.arn                 != snap_params.arn
                        || current_params.destripe            != snap_params.destripe
                        || current_params.beam_pattern        != snap_params.beam_pattern
                        || current_params.ml_enhance          != snap_params.ml_enhance
                        || current_params.slant_range_correction != snap_params.slant_range_correction
                        || current_params.display_channel        != snap_params.display_channel;
                }

                // Install pre-built rows on the UI thread — no assembly work here.
                // Stale case: copy raw_pings so we keep them for the re-run below.
                // Common case: move directly (no extra allocation).
                auto raw_pings = std::move(res.raw_pings);
                if (pipeline_stale)
                    m_view->setPreassembledRows(raw_pings,
                                                std::move(res.pipeline), !reset_view);
                else
                    m_view->setPreassembledRows(std::move(raw_pings),
                                                std::move(res.pipeline), !reset_view);

                // Sync seabed line visibility and colour table with current panel state.
                pushParams();

                const int actual_rows = m_view->rowCount();

                if (actual_rows >= 20 && entries_in_window > 0) {
                    const float r = static_cast<float>(entries_in_window) / actual_rows;
                    m_entries_per_row = std::clamp(r, 1.0f, 2.1f);
                }

                m_window_first_row = static_cast<int>(
                    first_entry / std::max(m_entries_per_row, 1.0f));

                if (actual_rows > 0)
                    m_view->scrollToRow(std::clamp(target_local, 0, actual_rows - 1));

                refreshInspector();
                refreshContactOverlay();

                // Re-run pipeline with corrected params so stale rows are replaced.
                // Stays in Processing state until the second pass completes.
                if (pipeline_stale) {
                    setDataState(ViewerDataState::Processing);
                    const SeabedAutoParams rerun_seabed    = m_view->seabedAutoParams();
                    const bool             rerun_seabed_en = m_view->seabedEnabled();
                    const int              rerun_gen       = m_load_gen;
                    auto* rw = new QFutureWatcher<LoadResult>(this);
                    connect(rw, &QFutureWatcher<LoadResult>::finished,
                            this, [this, rw, rerun_gen]() {
                                rw->deleteLater();
                                if (rerun_gen != m_load_gen) return;
                                try {
                                    LoadResult r = rw->result();
                                    m_view->setPreassembledRows(std::move(r.raw_pings),
                                                                std::move(r.pipeline),
                                                                /*preserve_view=*/true);
                                    pushParams();
                                    setDataState(ViewerDataState::Ready);
                                } catch (...) {
                                    setDataState(ViewerDataState::Failed);
                                }
                            });
                    rw->setFuture(QtConcurrent::run(
                        [raw = std::move(raw_pings),
                         p   = current_params,
                         sp  = rerun_seabed,
                         se  = rerun_seabed_en]() mutable -> LoadResult {
                            LoadResult r;
                            r.pipeline  = WaterfallView::runPipeline(raw, p, sp, se);
                            r.raw_pings = std::move(raw);
                            return r;
                        }));
                } else {
                    setDataState(ViewerDataState::Ready);
                }
            });

    const float selected_hz  = m_selected_frequency_hz;
    const int   window_size  = m_window_size;

    watcher->setFuture(QtConcurrent::run(
        [svc, store_path, store_format, idx, source_path, center_entry,
         layer_src_ref, apply_layer_crs, selected_hz, window_size,
         snap_params, snap_seabed_params, snap_seabed_enabled, cancel]()
                -> LoadResult {
            auto raw = svc->loadSidescanWindowFromStore(
                store_path, store_format, idx, source_path,
                static_cast<int64_t>(center_entry), window_size, selected_hz);

            if (cancel.isCancelled()) return {};

            // Ping-level frequency guard: the index-level filter above handles
            // the normal case (tagged entries).  When entries were untagged the
            // filter is a no-op, so pings from both bands can arrive here.
            // If the loaded pings themselves carry frequency_hz and more than one
            // band is present, drop the non-target band before the assembler sees
            // them — a cross-band port+stbd pair would corrupt the seabed pick.
            if (selected_hz > 0.f) {
                std::vector<float> ping_bands;
                for (const auto& p : raw) {
                    if (p.frequency_hz <= 0.f) continue;
                    bool found = false;
                    for (float b : ping_bands)
                        if (std::fabs(b - p.frequency_hz) < 1.f) { found = true; break; }
                    if (!found) ping_bands.push_back(p.frequency_hz);
                }
                if (ping_bands.size() >= 2) {
                    float target = ping_bands.front();
                    for (float b : ping_bands)
                        if (std::fabs(b - selected_hz) < std::fabs(target - selected_hz))
                            target = b;
                    raw.erase(
                        std::remove_if(raw.begin(), raw.end(),
                            [target](const core::SidescanPing& p) {
                                return p.frequency_hz > 0.f
                                    && std::fabs(p.frequency_hz - target) >= 1.f;
                            }),
                        raw.end());
                }
            }

            // Mirror the coordinate normalisation that SidescanViewController
            // applies before feeding pings to the map.
            if (apply_layer_crs) {
                for (auto& ping : raw) {
                    if (ping.nav.is_projected && !ping.nav.spatial_ref.exact)
                        ping.nav.spatial_ref = layer_src_ref;
                }
            }
            auto normalised = geo::normalizeSidescanPingsForMap(
                std::move(raw), core::makeWgs84SpatialRef());

            if (cancel.isCancelled()) return {};

            // Run TVG/ARC/AGC → assemble rows → beam/ARN/destripe/ML → seabed → stretch
            // entirely off the UI thread so the main thread stays responsive.
            // raw_pings is the canonical copy; runPipeline reads it via const&.
            LoadResult result;
            result.raw_pings = std::move(normalised);
            result.pipeline  = WaterfallView::runPipeline(result.raw_pings,
                                                          snap_params,
                                                          snap_seabed_params,
                                                          snap_seabed_enabled);
            return result;
        }));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Navigation slots
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallWindow::onPrevFix()
{
    emit prevLineRequested(m_layer ? m_layer->id : std::string{});
}

void WaterfallWindow::onNextFix()
{
    emit nextLineRequested(m_layer ? m_layer->id : std::string{});
}

void WaterfallWindow::onFrequencyBandChanged(int index)
{
    if (!m_freq_selector || !m_layer) return;
    const QVariant data = m_freq_selector->itemData(index);
    m_selected_frequency_hz = data.isValid() ? data.toFloat() : 0.f;
    m_total_ssc_entries = countSidescanForBand(m_layer, m_selected_frequency_hz);
    m_window_first_row  = 0;
    m_entries_per_row   = 1.0f;
    m_reset_view_next   = true;
    loadWindow(0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Contact overlay sync
// ─────────────────────────────────────────────────────────────────────────────

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

void WaterfallWindow::invalidateProcessedCache()
{
    if (!m_view || m_view->rawPings().empty()) return;

    m_load_cancel.cancel();
    m_load_cancel.reset();
    auto cancel = m_load_cancel;
    const int gen = ++m_load_gen;
    setDataState(ViewerDataState::Processing);
    startProgress();

    const WaterfallParams  params         = m_view->params();
    const SeabedAutoParams seabed_params  = m_view->seabedAutoParams();
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
