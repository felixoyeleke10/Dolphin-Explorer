// SssImagingAlgorithms.cpp — shared post-assembly imaging algorithm cores.
//
// The per-channel cores are lifted verbatim (logic-preserving) from
// WaterfallProcessingAlgorithms.cpp so the waterfall and the SSS map mosaic apply
// identical corrections.  They operate on a list of pointers to mutable amplitude
// rows for a single channel.
#include "ui/shared/processing/SssImagingAlgorithms.h"
#include "app/corrections/CorrectionAlgorithms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <future>

namespace dolphin::ui::imaging {

namespace {

int maxRowLen(const std::vector<std::vector<uint16_t>*>& rows)
{
    int ns = 0;
    for (const auto* r : rows) ns = std::max(ns, static_cast<int>(r->size()));
    return ns;
}

} // namespace

// -- Beam pattern normalisation ------------------------------------------------
void beamPatternChannel(std::vector<std::vector<uint16_t>*>& rows, const BeamPatternParams& bp)
{
    if (!bp.enabled || rows.empty()) return;
    const int ns = maxRowLen(rows);
    if (ns == 0) return;

    constexpr float kRefLevel = 32768.f;

    std::vector<float> mean(ns, 0.f);
    std::vector<int>   cnt(ns, 0);
    for (const auto* row : rows)
        for (int ci = 0; ci < static_cast<int>(row->size()) && ci < ns; ++ci)
            if ((*row)[ci] > 0) { mean[ci] += (*row)[ci]; ++cnt[ci]; }
    for (int ci = 0; ci < ns; ++ci)
        mean[ci] = cnt[ci] > 0 ? mean[ci] / cnt[ci] : kRefLevel;

    if (bp.smooth_radius > 0) {
        std::vector<float> smooth(ns, 0.f);
        for (int ci = 0; ci < ns; ++ci) {
            float sum = 0.f; int k = 0;
            for (int j = std::max(0, ci - bp.smooth_radius);
                     j <= std::min(ns - 1, ci + bp.smooth_radius); ++j) { sum += mean[j]; ++k; }
            smooth[ci] = k > 0 ? sum / k : mean[ci];
        }
        mean = std::move(smooth);
    }

    for (auto* row : rows)
        for (int ci = 0; ci < static_cast<int>(row->size()) && ci < ns; ++ci) {
            const float ref     = std::max(1024.f, mean[ci]);
            const float factor  = kRefLevel / ref;
            const float blended = (*row)[ci] * (1.f + (factor - 1.f) * bp.strength);
            (*row)[ci] = static_cast<uint16_t>(std::clamp(blended, 0.f, 65535.f));
        }
}

// -- Adaptive range normalisation ----------------------------------------------
void arnChannel(std::vector<std::vector<uint16_t>*>& rows, const ArnParams& arn)
{
    if (!arn.enabled || rows.empty()) return;
    const int ns = maxRowLen(rows);
    if (ns == 0) return;

    const int n_rows = static_cast<int>(rows.size());
    constexpr float kRefLevel = 32768.f;
    constexpr float kPctFrac  = 0.40f;
    const float gain_cap = std::max(1.f, std::pow(10.f, arn.gain_cap_db / 20.f));
    const float strength = std::clamp(arn.strength, 0.f, 1.f);

    std::vector<float>    ref(ns, kRefLevel);
    std::vector<bool>     valid(ns, false);
    std::vector<uint16_t> col_vals;
    col_vals.reserve(n_rows);

    for (int ci = 0; ci < ns; ++ci) {
        col_vals.clear();
        for (const auto* r : rows)
            if (ci < static_cast<int>(r->size()) && (*r)[ci] > 0)
                col_vals.push_back((*r)[ci]);
        if (static_cast<int>(col_vals.size()) < 3) continue;
        const int k = std::clamp(static_cast<int>(col_vals.size() * kPctFrac),
                                 0, static_cast<int>(col_vals.size()) - 1);
        std::nth_element(col_vals.begin(), col_vals.begin() + k, col_vals.end());
        ref[ci] = std::max(1024.f, static_cast<float>(col_vals[k]));
        valid[ci] = true;
    }

    if (arn.column_smooth > 0) {
        std::vector<float> smooth(ns);
        std::vector<bool>  smooth_valid(ns, false);
        for (int ci = 0; ci < ns; ++ci) {
            float sum = 0.f; int cnt = 0;
            for (int k = std::max(0, ci - arn.column_smooth);
                      k <= std::min(ns - 1, ci + arn.column_smooth); ++k)
                if (valid[k]) { sum += ref[k]; ++cnt; }
            smooth[ci] = cnt > 0 ? sum / cnt : ref[ci];
            smooth_valid[ci] = cnt > 0;
        }
        ref = std::move(smooth);
        valid = std::move(smooth_valid);
    }

    for (auto* row : rows)
        for (int ci = 0; ci < static_cast<int>(row->size()) && ci < ns; ++ci) {
            if (!valid[ci] || ref[ci] < 1.f) continue;
            const float factor  = std::clamp(kRefLevel / ref[ci], 1.f / gain_cap, gain_cap);
            const float blended = (*row)[ci] * (1.f + (factor - 1.f) * strength);
            (*row)[ci] = static_cast<uint16_t>(std::clamp(blended, 0.f, 65535.f));
        }
}

// -- Destripe ------------------------------------------------------------------
void destripeChannel(std::vector<std::vector<uint16_t>*>& rows, const DestripeParams& d)
{
    if (!d.enabled || rows.empty()) return;
    const int n     = static_cast<int>(rows.size());
    const int hw    = std::max(1, d.window / 2);
    const int nsegs = std::max(1, d.subdivision);
    const float cap = std::max(1.001f, d.capping);
    const int ns    = maxRowLen(rows);
    if (ns == 0) return;

    std::vector<std::vector<uint16_t>> orig(n);
    for (int i = 0; i < n; ++i) orig[i] = *rows[i];

    const int seg_w = (ns + nsegs - 1) / nsegs;

    for (int seg = 0; seg < nsegs; ++seg) {
        const int c0 = seg * seg_w;
        const int c1 = std::min(c0 + seg_w, ns);
        if (c0 >= ns) break;

        std::vector<double> seg_sum(n, 0.0);
        std::vector<int>    seg_cnt(n, 0);
        for (int i = 0; i < n; ++i)
            for (int ci = c0; ci < c1 && ci < static_cast<int>(orig[i].size()); ++ci)
                if (orig[i][ci] > 0) { seg_sum[i] += orig[i][ci]; ++seg_cnt[i]; }

        double gsum = 0.0; int gcnt = 0;
        for (int i = 0; i < n; ++i) { gsum += seg_sum[i]; gcnt += seg_cnt[i]; }
        if (gcnt == 0) continue;
        const float global_mean = static_cast<float>(gsum / gcnt);
        if (global_mean < 1.f) continue;

        for (int i = 0; i < n; ++i) {
            double lsum = 0.0; int lcnt = 0;
            for (int k = std::max(0, i - hw); k <= std::min(n - 1, i + hw); ++k) {
                lsum += seg_sum[k]; lcnt += seg_cnt[k];
            }
            if (lcnt == 0) continue;
            const float local_mean = static_cast<float>(lsum / lcnt);
            if (local_mean < 1.f) continue;

            const float factor = std::clamp(global_mean / local_mean, 1.f / cap, cap);
            auto& side = *rows[i];
            for (int ci = c0; ci < c1 && ci < static_cast<int>(side.size()); ++ci)
                side[ci] = static_cast<uint16_t>(
                    std::clamp(static_cast<float>(orig[i][ci]) * factor, 0.f, 65535.f));
        }
    }
}

// -- ML enhance (CLAHE-like) ---------------------------------------------------
void mlEnhanceChannel(std::vector<std::vector<uint16_t>*>& rows, const MlEnhanceParams& me)
{
    if (!me.enabled || rows.empty()) return;
    const int   n_rows = static_cast<int>(rows.size());
    const int   tw     = std::max(16, me.tile_samps);
    const int   th     = std::max(16, me.tile_pings);
    const float clip   = std::max(1.f, me.clip_limit);
    const int   ns     = maxRowLen(rows);
    if (ns == 0) return;

    auto buildCdf = [&](const std::vector<uint16_t>& vals) -> std::array<uint16_t, 256> {
        std::array<int, 256> hist = {};
        for (uint16_t v : vals) ++hist[v >> 8];

        const int tile_area  = static_cast<int>(vals.size());
        const int clip_count = std::max(1, static_cast<int>(clip * tile_area / 256));
        int excess = 0;
        for (int b = 0; b < 256; ++b)
            if (hist[b] > clip_count) { excess += hist[b] - clip_count; hist[b] = clip_count; }
        const int add_each = excess / 256;
        for (int b = 0; b < 256; ++b) hist[b] += add_each;

        int total = 0;
        for (int b = 0; b < 256; ++b) total += hist[b];

        std::array<uint16_t, 256> cdf = {};
        int cumsum = 0;
        for (int b = 0; b < 256; ++b) {
            cumsum += hist[b];
            cdf[b] = static_cast<uint16_t>(std::clamp(cumsum * 65535 / std::max(1, total), 0, 65535));
        }
        return cdf;
    };

    const int ntc = (ns     + tw - 1) / tw;
    const int ntr = (n_rows + th - 1) / th;

    std::vector<std::array<uint16_t, 256>> cdfs(static_cast<size_t>(ntr) * ntc);
    for (auto& cdf : cdfs)
        for (int b = 0; b < 256; ++b) cdf[b] = static_cast<uint16_t>(b * 257u);

    for (int tr = 0; tr < ntr; ++tr) {
        const int r0 = tr * th, r1 = std::min(r0 + th, n_rows);
        for (int tc = 0; tc < ntc; ++tc) {
            const int c0 = tc * tw, c1 = std::min(c0 + tw, ns);
            std::vector<uint16_t> pixels;
            pixels.reserve(static_cast<size_t>(r1 - r0) * (c1 - c0));
            for (int ri = r0; ri < r1; ++ri) {
                const auto& side = *rows[ri];
                for (int ci = c0; ci < c1 && ci < static_cast<int>(side.size()); ++ci)
                    pixels.push_back(side[ci]);
            }
            if (!pixels.empty())
                cdfs[static_cast<size_t>(tr) * ntc + tc] = buildCdf(pixels);
        }
    }

    for (int ri = 0; ri < n_rows; ++ri) {
        auto& side   = *rows[ri];
        const int ns_row = static_cast<int>(side.size());

        const float tr_f = std::clamp((static_cast<float>(ri) / th) - 0.5f,
                                       0.f, static_cast<float>(ntr - 1));
        const int   tr0  = std::clamp(static_cast<int>(std::floor(tr_f)), 0, ntr - 1);
        const int   tr1  = std::clamp(tr0 + 1, 0, ntr - 1);
        const float wr1  = tr_f - std::floor(tr_f);
        const float wr0  = 1.f - wr1;

        for (int ci = 0; ci < ns_row; ++ci) {
            const int v = static_cast<int>(side[ci] >> 8);

            const float tc_f = std::clamp((static_cast<float>(ci) / tw) - 0.5f,
                                           0.f, static_cast<float>(ntc - 1));
            const int   tc0  = std::clamp(static_cast<int>(std::floor(tc_f)), 0, ntc - 1);
            const int   tc1  = std::clamp(tc0 + 1, 0, ntc - 1);
            const float wc1  = tc_f - std::floor(tc_f);
            const float wc0  = 1.f - wc1;

            const float mapped =
                wr0 * (wc0 * cdfs[static_cast<size_t>(tr0) * ntc + tc0][v] + wc1 * cdfs[static_cast<size_t>(tr0) * ntc + tc1][v]) +
                wr1 * (wc0 * cdfs[static_cast<size_t>(tr1) * ntc + tc0][v] + wc1 * cdfs[static_cast<size_t>(tr1) * ntc + tc1][v]);

            side[ci] = static_cast<uint16_t>(std::clamp(mapped, 0.f, 65535.f));
        }
    }
}

// -- Whole-pings convenience ---------------------------------------------------

namespace {

// Build per-channel row pointers over a working amplitude buffer, run the enabled
// imaging cores, and write the result back into the pings' samples.
void runChannel(std::vector<core::SidescanPing*>& chan, const WaterfallParams& params)
{
    if (chan.empty()) return;
    // Along-track order matters for destripe / ML.
    std::sort(chan.begin(), chan.end(),
              [](const core::SidescanPing* a, const core::SidescanPing* b) {
                  return a->timestamp_us < b->timestamp_us;
              });

    // Work directly on raw amplitudes — do NOT mask water-column samples to 0.
    // The map georeferencer rasterizes ping.samples[].amplitude verbatim, so zeroing
    // (e.g. via physical16's negative-range mask) would blank large regions of the
    // mosaic. The algorithms already skip zero-valued samples in their statistics.
    std::vector<std::vector<uint16_t>> amp(chan.size());
    std::vector<std::vector<uint16_t>*> rows(chan.size());
    for (size_t i = 0; i < chan.size(); ++i) {
        const auto& s = chan[i]->samples;
        amp[i].resize(s.size());
        for (size_t k = 0; k < s.size(); ++k) amp[i][k] = s[k].amplitude;
        rows[i] = &amp[i];
    }

    if (params.beam_pattern.enabled) beamPatternChannel(rows, params.beam_pattern);
    if (params.arn.enabled)          arnChannel(rows, params.arn);
    if (params.destripe.enabled)     destripeChannel(rows, params.destripe);
    if (params.ml_enhance.enabled)   mlEnhanceChannel(rows, params.ml_enhance);

    for (size_t i = 0; i < chan.size(); ++i) {
        auto& s = chan[i]->samples;
        for (size_t k = 0; k < s.size(); ++k) s[k].amplitude = amp[i][k];
    }
}

} // namespace

void applyImagingChain(std::vector<core::SidescanPing>& pings, const WaterfallParams& params)
{
    const bool any = params.beam_pattern.enabled || params.arn.enabled
                  || params.destripe.enabled || params.ml_enhance.enabled;
    if (!any || pings.empty()) return;

    std::vector<core::SidescanPing*> ports, stbds;
    ports.reserve(pings.size());
    stbds.reserve(pings.size());
    for (auto& p : pings) {
        if (p.channel == core::SidescanChannel::Port) ports.push_back(&p);
        else                                          stbds.push_back(&p);
    }
    // Port and stbd are independent — process them concurrently. The caller is
    // already a background worker, so one extra thread halves the chain's wall time.
    auto fut = std::async(std::launch::async, [&] { runChannel(ports, params); });
    runChannel(stbds, params);
    fut.get();
}

void applySssMapCorrections(std::vector<core::SidescanPing>& pings, const WaterfallParams& params)
{
    if (pings.empty()) return;

    // Skip corrections already baked into the store so they are not double-applied
    // (mirrors WaterfallView::runPipeline).  All pings in one line share the flags.
    const uint32_t baked = pings.front().correction_flags;

    if (params.tvg.enabled && !core::hasCorrectionFlag(baked, core::CorrectionFlag::Tvg))
        app::corrections::applyTvg(pings, params.tvg);
    if (params.arc.enabled && !core::hasCorrectionFlag(baked, core::CorrectionFlag::Arc))
        app::corrections::applyArc(pings, params.arc);
    if (params.agc.enabled && !core::hasCorrectionFlag(baked, core::CorrectionFlag::GainNormalized))
        app::corrections::normalizeAmplitudes(pings, params.agc);

    applyImagingChain(pings, params);
}

} // namespace dolphin::ui::imaging
