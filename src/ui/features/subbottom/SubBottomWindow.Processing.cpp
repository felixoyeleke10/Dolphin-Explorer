// SubBottomWindow.Processing.cpp — SBP gain and signal processing pipeline.
// Operates on m_traces_raw (a copy of the last loaded traces) so that
// param changes don't require a round-trip disk read.

#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/subbottom/SubBottomView.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>

namespace dolphin::ui {

namespace {

std::vector<core::SubBottomTrace> processTraces(
    std::vector<core::SubBottomTrace> traces,
    SbpGainParams                     gp,
    SbpSignalParams                   sp)
{
    // ── Per-trace passes ────────────────────────────────────────────────────
    for (auto& t : traces) {
        auto& s = t.samples;
        if (s.empty()) continue;
        const int n = static_cast<int>(s.size());

        // DC removal: subtract the per-trace mean to remove hydrophone bias.
        if (sp.dc_removal_en) {
            float sum = 0.f;
            for (float v : s) sum += v;
            const float mean = sum / static_cast<float>(n);
            for (float& v : s) v -= mean;
        }

        // Envelope: instantaneous amplitude |sample|.
        if (sp.envelope_en)
            for (float& v : s) v = std::abs(v);

        // Per-trace normalize: scale so the loudest sample equals 1.
        if (gp.normalize_en) {
            float mx = 0.f;
            for (float v : s) mx = std::max(mx, std::abs(v));
            if (mx > 0.f)
                for (float& v : s) v /= mx;
        }

        // Static gain: apply a constant dB boost/cut.
        if (gp.static_gain_en && gp.static_gain_db != 0.f) {
            const float factor = std::pow(10.f, gp.static_gain_db / 20.f);
            for (float& v : s) v *= factor;
        }
    }

    // ── AGC: running-window RMS normalisation across traces ─────────────────
    if (gp.agc_en) {
        const int n_t = static_cast<int>(traces.size());
        const int hw  = std::max(1, gp.agc_window);
        for (int ti = 0; ti < n_t; ++ti) {
            float rms = 0.f;
            int   cnt = 0;
            for (int w = std::max(0, ti - hw); w <= std::min(n_t - 1, ti + hw); ++w) {
                for (float v : traces[w].samples) rms += v * v;
                cnt += static_cast<int>(traces[w].samples.size());
            }
            if (cnt > 0) rms = std::sqrt(rms / static_cast<float>(cnt));
            if (rms > 0.f)
                for (float& v : traces[ti].samples) v /= rms;
        }
    }

    // Bandpass (sp.bandpass_en / sp.bp_lo_hz / sp.bp_hi_hz): stored, awaiting
    // FFT-based butterworth integration.

    return traces;
}

} // namespace

void SubBottomWindow::invalidateProcessedCache()
{
    if (m_traces_raw.empty()) return;
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

void SubBottomWindow::scheduleProcessing()
{
    if (m_traces_raw.empty()) {
        m_view->clear();
        return;
    }

    m_proc_cancel.cancel();
    m_proc_cancel.reset();
    auto cancel = m_proc_cancel;

    // Transition after the token reset so that procToken() returns the new
    // token when the dataStateChanged signal reaches the coordinator.
    setDataState(ViewerDataState::Processing);

    const int gen = ++m_proc_gen;

    const SbpGainParams   gp = m_gain_params;
    const SbpSignalParams sp = m_signal_params;
    auto traces_copy         = m_traces_raw;

    auto* watcher = new QFutureWatcher<std::vector<core::SubBottomTrace>>(this);
    connect(watcher, &QFutureWatcher<std::vector<core::SubBottomTrace>>::finished,
            this, [this, watcher, gen]() {
                watcher->deleteLater();
                if (gen != m_proc_gen) return;
                try {
                    m_view->setTraces(watcher->result());
                    setDataState(ViewerDataState::Ready);
                } catch (...) {
                    setDataState(ViewerDataState::Failed);
                }
            });

    watcher->setFuture(QtConcurrent::run(
        [traces = std::move(traces_copy), gp, sp, cancel]() mutable
                -> std::vector<core::SubBottomTrace> {
            if (cancel.isCancelled()) return {};
            return processTraces(std::move(traces), gp, sp);
        }));
}

} // namespace dolphin::ui
