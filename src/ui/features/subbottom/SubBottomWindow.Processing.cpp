// SubBottomWindow.Processing.cpp — SBP gain and signal processing pipeline.
// Operates on m_traces_raw (a shared_ptr to the last loaded traces) so that
// param changes don't require a round-trip disk read.

#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/subbottom/SubBottomView.h"
#include "app/display/NavCorrection.h"
#include "app/tasks/CancellationToken.h"
#include "app/tasks/OperationManager.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace dolphin::ui {

namespace {

std::vector<core::SubBottomTrace> processTraces(
    std::vector<core::SubBottomTrace> traces,
    SbpGainParams                     gp,
    SbpSignalParams                   sp,
    app::CancellationToken            cancel)
{
    const uint32_t baked = traces.empty() ? 0u : traces.front().correction_flags;

    // -- Per-trace passes ----------------------------------------------------
    for (auto& t : traces) {
        if (cancel.isCancelled()) return {};

        auto& s = t.samples;
        if (s.empty()) continue;
        const int n = static_cast<int>(s.size());

        // DC removal: subtract the per-trace mean to remove hydrophone bias.
        if (sp.dc_removal_en &&
            !core::hasSbpCorrectionFlag(baked, core::SbpCorrectionFlag::DcRemoval)) {
            float sum = 0.f;
            for (float v : s) sum += v;
            const float mean = sum / static_cast<float>(n);
            for (float& v : s) v -= mean;
        }

        // Envelope: instantaneous amplitude |sample|.
        if (sp.envelope_en &&
            !core::hasSbpCorrectionFlag(baked, core::SbpCorrectionFlag::Envelope))
            for (float& v : s) v = std::abs(v);

        // Per-trace normalize: scale so the loudest sample equals 1.
        if (gp.normalize_en &&
            !core::hasSbpCorrectionFlag(baked, core::SbpCorrectionFlag::Normalize)) {
            float mx = 0.f;
            for (float v : s) mx = std::max(mx, std::abs(v));
            if (mx > 0.f)
                for (float& v : s) v /= mx;
        }

        // Static gain: apply a constant dB boost/cut.
        if (gp.static_gain_en && gp.static_gain_db != 0.f &&
            !core::hasSbpCorrectionFlag(baked, core::SbpCorrectionFlag::StaticGain)) {
            const float factor = std::pow(10.f, gp.static_gain_db / 20.f);
            for (float& v : s) v *= factor;
        }
    }

    if (cancel.isCancelled()) return {};

    // -- AGC: sliding-window RMS normalisation across traces -----------------
    // Uses a sliding window over precomputed per-trace energy sums so the
    // overall cost is O(N) instead of the naive O(N × window) nested loop.
    if (gp.agc_en &&
        !core::hasSbpCorrectionFlag(baked, core::SbpCorrectionFlag::Agc)) {
        const int n_t = static_cast<int>(traces.size());
        const int hw  = std::max(1, gp.agc_window);

        // Precompute per-trace energy and sample count — O(N × samples).
        std::vector<float> trace_energy(n_t, 0.f);
        std::vector<int>   trace_count (n_t, 0);
        for (int i = 0; i < n_t; ++i) {
            for (float v : traces[i].samples) trace_energy[i] += v * v;
            trace_count[i] = static_cast<int>(traces[i].samples.size());
        }

        // Slide the window [left, right] across traces — O(N).
        float window_energy = 0.f;
        int   window_count  = 0;
        int   left = 0, right = -1;

        for (int ti = 0; ti < n_t; ++ti) {
            if (cancel.isCancelled()) return {};

            // Expand right edge to ti+hw.
            const int new_right = std::min(n_t - 1, ti + hw);
            while (right < new_right) {
                ++right;
                window_energy += trace_energy[right];
                window_count  += trace_count[right];
            }
            // Shrink left edge to ti-hw.
            const int new_left = std::max(0, ti - hw);
            while (left < new_left) {
                window_energy -= trace_energy[left];
                window_count  -= trace_count[left];
                ++left;
            }

            if (window_count > 0) {
                const float rms = std::sqrt(window_energy / static_cast<float>(window_count));
                if (rms > 0.f)
                    for (float& v : traces[ti].samples) v /= rms;
            }
        }
    }

    // Bandpass (sp.bandpass_en / sp.bp_lo_hz / sp.bp_hi_hz): stored, awaiting
    // FFT-based butterworth integration.

    return traces;
}

} // namespace

void SubBottomWindow::invalidateProcessedCache()
{
    if (!m_traces_raw || m_traces_raw->empty()) return;
    scheduleProcessing();
}

void SubBottomWindow::applyGainParams(const SbpGainParams& p)
{
    m_gain_params = p;
    scheduleProcessing();
}

void SubBottomWindow::applySignalParams(const SbpSignalParams& p)
{
    m_signal_params = p;
    scheduleProcessing();
}

void SubBottomWindow::applyNavToLine(const NavProcessingParams& p)
{
    m_nav_params = p;
    scheduleProcessing();   // re-runs the pipeline; nav corrections applied below
}

void SubBottomWindow::scheduleProcessing()
{
    if (!m_traces_raw || m_traces_raw->empty()) {
        m_view->clear();
        return;
    }

    // No explicit cancel here — the keyed OperationManager op supersedes any
    // in-flight processing when onProcDebounce launches the next one.
    setDataState(ViewerDataState::Processing);

    // Defer the actual launch so rapid param changes (slider drags) collapse
    // into a single pipeline run once the user settles.
    m_proc_debounce->start();
}

void SubBottomWindow::onProcDebounce()
{
    if (!m_traces_raw || m_traces_raw->empty() || !m_op_mgr) return;

    setDataState(ViewerDataState::Processing);

    const SbpGainParams       gp  = m_gain_params;
    const SbpSignalParams     sp  = m_signal_params;
    const NavProcessingParams np  = m_nav_params;
    auto                      raw = m_traces_raw;  // shared_ptr copy — O(1), no data copy

    // Keyed so a newer run supersedes the in-flight one (replaces the generation
    // guard + manual cancel token). The DSP is light, so not flagged heavy.
    m_op_mgr->run<std::vector<core::SubBottomTrace>>(
        tr("Processing sub-bottom"),
        [raw = std::move(raw), gp, sp, np](app::CancellationToken cancel)
                -> std::vector<core::SubBottomTrace> {
            try {
                if (cancel.isCancelled()) return {};
                // Copy raw traces on the bg thread so rapid UI param changes never
                // block the main thread waiting for the allocation.
                std::vector<core::SubBottomTrace> traces = *raw;
                if (cancel.isCancelled()) return {};
                // Nav corrections shift trace positions (cursor read-out + map);
                // applied before DSP so the sample passes operate on the final set.
                applySbpNavCorrections(traces, np);
                return processTraces(std::move(traces), gp, sp, cancel);
            } catch (...) {
                return {};   // empty → on_done sets Ready, never leaves busy stuck
            }
        },
        [this](std::vector<core::SubBottomTrace> result) {
            m_view->setTraces(std::move(result));
            setDataState(ViewerDataState::Ready);
        },
        "sbpwin:proc",
        /*heavy=*/false);
}

} // namespace dolphin::ui
