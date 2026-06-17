// WaterfallViewProcessing.cpp — nav processing, pipeline orchestration, do* wrappers.
//
// Processing algorithms (TVG, ARC, AGC, beam, ARN, destripe, CLAHE) →
//   WaterfallProcessingAlgorithms.cpp / .h  (dolphin::ui::detail namespace)
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/processing/WaterfallProcessingAlgorithms.h"
#include "ui/features/waterfall/processing/WaterfallPingAssembler.h"
#include "ui/features/waterfall/processing/SeabedAutoDetector.h"
#include "app/display/NavCorrection.h"

#include <vector>

namespace dolphin::ui {

// -- Nav Processing ------------------------------------------------------------

std::vector<core::SidescanPing>
WaterfallView::runNavCorrections(std::vector<core::SidescanPing> pings,
                                  const NavProcessingParams& params)
{
    // Single source of truth: the same correction the SSS map applies, so the
    // waterfall and the map always agree (see app/display/NavCorrection).
    return applySidescanNavCorrections(std::move(pings), params);
}

// -- runPipeline — background-safe full display pipeline -----------------------
//
// TVG/ARC/AGC → assemble rows → seabed → beam/ARN/destripe/ML → auto-stretch.
// Seabed detection runs on clean calibrated amplitudes (before display-specific
// enhancements) so beam-pattern correction / ARN / destripe / ML cannot bias picks.
// pings are read-only; we make one internal working copy here so the caller
// can keep the original for re-processing on param changes.

WaterfallView::WfPipelineResult WaterfallView::runPipeline(
    const std::vector<core::SidescanPing>& pings,
    const WaterfallParams&                 params,
    const SeabedAutoParams&                seabed_params,
    bool                                   seabed_enabled)
{
    using namespace detail;
    auto work = pings;  // single working copy; original stays intact in the caller

    // Skip corrections that are already baked into the .dlpd data so they are
    // not double-applied.  All pings in one line share the same correction_flags.
    const uint32_t baked = pings.empty() ? 0u : pings.front().correction_flags;

    if (params.tvg.enabled && !core::hasCorrectionFlag(baked, core::CorrectionFlag::Tvg))
        applyTvg(work, params);
    if (params.arc.enabled && !core::hasCorrectionFlag(baked, core::CorrectionFlag::Arc))
        applyArc(work, params);
    if (params.agc.enabled && !core::hasCorrectionFlag(baked, core::CorrectionFlag::GainNormalized))
        normalizeRawAmplitudes(work, params);
    else if (!params.agc.enabled)
        stretchRawAmplitudes(work);

    WfPipelineResult result;
    result.rows = WaterfallPingAssembler::assemble(work, params);

    if (seabed_enabled) {
        SeabedAutoDetector::detectAll(result.rows, seabed_params);
        if (seabed_params.smoothing > 0.f)
            SeabedAutoDetector::smooth(result.rows,
                                       static_cast<int>(seabed_params.smoothing));
    }

    if (params.beam_pattern.enabled) applyBeamPattern(result.rows, params);
    if (params.arn.enabled)          applyArn(result.rows, params);
    if (params.destripe.enabled)     applyDestripe(result.rows, params);
    if (params.ml_enhance.enabled)   applyMlEnhance(result.rows, params);

    // Compute auto-stretch (1st/99th percentile across assembled amplitudes).
    // Bucket 0 is excluded — it captures water-column / nadir-blanking zeros.
    // 1024-bin stack histogram: bin = v >> 6, ±0.1% precision vs 65536 bins.
    {
        constexpr int kBins  = 1024;
        constexpr int kShift = 6;   // 65536 / 1024 = 64 = 2^6
        uint32_t hist[kBins] = {};
        for (const auto& row : result.rows) {
            for (uint16_t v : row.port) ++hist[v >> kShift];
            for (uint16_t v : row.stbd) ++hist[v >> kShift];
        }
        uint64_t total = 0;
        for (int i = 1; i < kBins; ++i) total += hist[i];

        if (total == 0) {
            result.stretch_low  = 0.f;
            result.stretch_high = 1.f;
        } else {
            const uint64_t lo_tgt = std::max(uint64_t(1), total / 100u);
            const uint64_t hi_tgt = total - lo_tgt;
            int p01 = 1, p99 = kBins - 1;
            uint64_t cum = 0; bool found_lo = false;
            for (int i = 1; i < kBins; ++i) {
                cum += hist[i];
                if (!found_lo && cum >= lo_tgt) { p01 = i; found_lo = true; }
                if (cum >= hi_tgt)              { p99 = i; break; }
            }
            const int hi = p01 < kBins - 1 ? std::max(p99, p01 + 1) : kBins - 1;
            result.stretch_low  = float(p01 << kShift) / 65535.f;
            result.stretch_high = float(hi  << kShift) / 65535.f;
        }
    }

    return result;
}

// -- do* wrappers — bridge rebuildRows/setPings to detail:: free functions -----

void WaterfallView::doApplyTvg(std::vector<core::SidescanPing>& pings)            { detail::applyTvg(pings, m_params); }
void WaterfallView::doApplyArc(std::vector<core::SidescanPing>& pings)            { detail::applyArc(pings, m_params); }
void WaterfallView::doNormalizeRawAmplitudes(std::vector<core::SidescanPing>& p)  { detail::normalizeRawAmplitudes(p, m_params); }
void WaterfallView::doStretchRawAmplitudes(std::vector<core::SidescanPing>& p)    { detail::stretchRawAmplitudes(p); }
void WaterfallView::doApplyBeamPattern(std::vector<PingRow>& rows)                { detail::applyBeamPattern(rows, m_params); }
void WaterfallView::doApplyArn(std::vector<PingRow>& rows)                        { detail::applyArn(rows, m_params); }
void WaterfallView::doApplyDestripe(std::vector<PingRow>& rows)                   { detail::applyDestripe(rows, m_params); }
void WaterfallView::doApplyMlEnhance(std::vector<PingRow>& rows)                  { detail::applyMlEnhance(rows, m_params); }

} // namespace dolphin::ui
