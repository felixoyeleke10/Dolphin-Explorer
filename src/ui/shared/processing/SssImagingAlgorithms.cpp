// SssImagingAlgorithms.cpp — shared post-assembly imaging algorithm cores.
//
// The per-channel cores are lifted verbatim (logic-preserving) from
// WaterfallProcessingAlgorithms.cpp so the waterfall and the SSS map mosaic apply
// identical corrections.  They operate on a list of pointers to mutable amplitude
// rows for a single channel.
#include "ui/shared/processing/SssImagingAlgorithms.h"
#include "pipeline/SidescanEnhancementAlgorithms.h"
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

float medianValue(std::vector<float> values)
{
    if (values.empty()) return 0.f;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if (values.size() % 2 != 0) return values[middle];
    const float upper = values[middle];
    const float lower = *std::max_element(values.begin(), values.begin() + middle);
    return 0.5f * (lower + upper);
}

void applyAgc(std::vector<core::SidescanPing>& pings,
              const WaterfallParams& params)
{
    if (!params.agc.enabled) return;

    // A valid store normally has uniform baked flags. Keep a mixed/corrupt
    // store safe as well: already-normalized records are never processed twice.
    const auto is_baked = [](const core::SidescanPing& ping) {
        return core::hasCorrectionFlag(
            ping.correction_flags, core::CorrectionFlag::GainNormalized);
    };
    const size_t baked_count = static_cast<size_t>(std::count_if(
        pings.cbegin(), pings.cend(), is_baked));
    if (baked_count == pings.size()) return;
    if (baked_count == 0) {
        app::corrections::normalizeAmplitudes(pings, params.agc);
        return;
    }

    std::vector<size_t> indices;
    std::vector<core::SidescanPing> work;
    indices.reserve(pings.size() - baked_count);
    work.reserve(pings.size() - baked_count);
    for (size_t i = 0; i < pings.size(); ++i) {
        if (is_baked(pings[i])) continue;
        indices.push_back(i);
        work.push_back(pings[i]);
    }
    if (work.empty()) return;

    app::corrections::normalizeAmplitudes(work, params.agc);
    for (size_t i = 0; i < work.size(); ++i) {
        pings[indices[i]].samples = std::move(work[i].samples);
        pings[indices[i]].correction_flags = work[i].correction_flags;
    }
}

} // namespace

// -- Beam pattern normalisation ------------------------------------------------
static void legacyBeamPatternChannel(std::vector<std::vector<uint16_t>*>& rows, const BeamPatternParams& bp)
{
    if (!bp.enabled || rows.empty()) return;
    const int ns = maxRowLen(rows);
    if (ns == 0) return;

    std::vector<float> mean(ns, 0.f);
    std::vector<int>   cnt(ns, 0);
    for (const auto* row : rows)
        for (int ci = 0; ci < static_cast<int>(row->size()) && ci < ns; ++ci)
            if ((*row)[ci] > 0) { mean[ci] += (*row)[ci]; ++cnt[ci]; }
    for (int ci = 0; ci < ns; ++ci)
        if (cnt[ci] > 0) mean[ci] /= cnt[ci];

    if (bp.smooth_radius > 0) {
        std::vector<float> smooth(ns, 0.f);
        std::vector<double> prefix(static_cast<size_t>(ns) + 1, 0.0);
        std::vector<int> valid_prefix(static_cast<size_t>(ns) + 1, 0);
        for (int ci = 0; ci < ns; ++ci) {
            prefix[static_cast<size_t>(ci) + 1] = prefix[static_cast<size_t>(ci)] + mean[ci];
            valid_prefix[static_cast<size_t>(ci) + 1] =
                valid_prefix[static_cast<size_t>(ci)] + (cnt[ci] > 0 ? 1 : 0);
        }
        for (int ci = 0; ci < ns; ++ci) {
            const int begin = std::max(0, ci - bp.smooth_radius);
            const int end = std::min(ns - 1, ci + bp.smooth_radius);
            const double sum = prefix[static_cast<size_t>(end) + 1]
                             - prefix[static_cast<size_t>(begin)];
            const int valid = valid_prefix[static_cast<size_t>(end) + 1]
                            - valid_prefix[static_cast<size_t>(begin)];
            smooth[ci] = valid > 0 ? static_cast<float>(sum / valid) : 0.f;
        }
        mean = std::move(smooth);
    }

    std::vector<float> valid_profile;
    valid_profile.reserve(ns);
    for (float value : mean) if (value > 0.f) valid_profile.push_back(value);
    const float reference = medianValue(std::move(valid_profile));
    if (!(reference > 0.f)) return;
    const float gain_cap = std::pow(10.f, std::clamp(bp.gain_cap_db, 0.f, 40.f) / 20.f);
    const float strength = std::clamp(bp.strength, 0.f, 1.f);

    for (auto* row : rows)
        for (int ci = 0; ci < static_cast<int>(row->size()) && ci < ns; ++ci) {
            if ((*row)[ci] == 0 || !(mean[ci] > 0.f)) continue;
            const float requested = std::clamp(reference / mean[ci],
                                               1.f / gain_cap, gain_cap);
            const float factor = std::pow(requested, strength);
            const float blended = (*row)[ci] * factor;
            (*row)[ci] = static_cast<uint16_t>(std::clamp(blended, 0.f, 65535.f));
        }
}

// -- Adaptive range normalisation ----------------------------------------------
static void legacyArnChannel(std::vector<std::vector<uint16_t>*>& rows, const ArnParams& arn)
{
    if (!arn.enabled || rows.empty()) return;
    const int ns = maxRowLen(rows);
    if (ns == 0) return;

    const int n_rows = static_cast<int>(rows.size());
    constexpr float kPctFrac  = 0.40f;
    const float gain_cap = std::max(1.f, std::pow(10.f, arn.gain_cap_db / 20.f));
    const float strength = std::clamp(arn.strength, 0.f, 1.f);

    std::vector<float>    ref(ns, 0.f);
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
        ref[ci] = static_cast<float>(col_vals[k]);
        valid[ci] = true;
    }

    if (arn.column_smooth > 0) {
        std::vector<float> smooth(ns);
        std::vector<bool>  smooth_valid(ns, false);
        std::vector<double> prefix(static_cast<size_t>(ns) + 1, 0.0);
        std::vector<int> count_prefix(static_cast<size_t>(ns) + 1, 0);
        for (int ci = 0; ci < ns; ++ci) {
            prefix[static_cast<size_t>(ci) + 1] = prefix[static_cast<size_t>(ci)]
                + (valid[ci] ? ref[ci] : 0.f);
            count_prefix[static_cast<size_t>(ci) + 1] =
                count_prefix[static_cast<size_t>(ci)] + (valid[ci] ? 1 : 0);
        }
        for (int ci = 0; ci < ns; ++ci) {
            const int begin = std::max(0, ci - arn.column_smooth);
            const int end = std::min(ns - 1, ci + arn.column_smooth);
            const double sum = prefix[static_cast<size_t>(end) + 1]
                             - prefix[static_cast<size_t>(begin)];
            const int cnt = count_prefix[static_cast<size_t>(end) + 1]
                          - count_prefix[static_cast<size_t>(begin)];
            smooth[ci] = cnt > 0 ? static_cast<float>(sum / cnt) : ref[ci];
            smooth_valid[ci] = cnt > 0;
        }
        ref = std::move(smooth);
        valid = std::move(smooth_valid);
    }

    std::vector<float> valid_reference;
    valid_reference.reserve(ns);
    for (int ci = 0; ci < ns; ++ci)
        if (valid[ci] && ref[ci] > 0.f) valid_reference.push_back(ref[ci]);
    const float target = medianValue(std::move(valid_reference));
    if (!(target > 0.f)) return;

    for (auto* row : rows)
        for (int ci = 0; ci < static_cast<int>(row->size()) && ci < ns; ++ci) {
            if (!valid[ci] || ref[ci] < 1.f) continue;
            if ((*row)[ci] == 0) continue;
            const float requested = std::clamp(target / ref[ci], 1.f / gain_cap, gain_cap);
            const float factor = std::pow(requested, strength);
            const float blended = (*row)[ci] * factor;
            (*row)[ci] = static_cast<uint16_t>(std::clamp(blended, 0.f, 65535.f));
        }
}

// -- Destripe ------------------------------------------------------------------
static void legacyDestripeChannel(std::vector<std::vector<uint16_t>*>& rows, const DestripeParams& d)
{
    if (!d.enabled || rows.empty()) return;
    const int n     = static_cast<int>(rows.size());
    const int window = std::max(1, d.window);
    const int left_extent = (window - 1) / 2;
    const int right_extent = window / 2;
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

        std::vector<float> ping_mean(static_cast<size_t>(n), 0.f);
        for (int i = 0; i < n; ++i)
            if (seg_cnt[i] > 0)
                ping_mean[static_cast<size_t>(i)] = static_cast<float>(
                    seg_sum[i] / static_cast<double>(seg_cnt[i]));
        std::vector<float> neighbours;
        neighbours.reserve(static_cast<size_t>(window));

        for (int i = 0; i < n; ++i) {
            const float current = ping_mean[static_cast<size_t>(i)];
            if (!(current > 0.f)) continue;
            neighbours.clear();
            const int begin = std::max(0, i - left_extent);
            const int end = std::min(n - 1, i + right_extent);
            for (int k = begin; k <= end; ++k)
                if (ping_mean[static_cast<size_t>(k)] > 0.f)
                    neighbours.push_back(ping_mean[static_cast<size_t>(k)]);
            const float local_reference = medianValue(neighbours);
            if (!(local_reference > 0.f)) continue;

            const float requested = local_reference / current;
            // A destriper must not continuously flatten legitimate along-track
            // gain/geology trends.  Treat small deviations from the robust local
            // reference as signal and only correct a meaningful stripe outlier.
            // 0.12 nepers is approximately 1.04 dB (12.7% in amplitude).
            if (std::fabs(std::log(requested)) < 0.12f) continue;
            const float factor = std::clamp(requested, 1.f / cap, cap);
            auto& side = *rows[i];
            for (int ci = c0; ci < c1 && ci < static_cast<int>(side.size()); ++ci)
                side[ci] = static_cast<uint16_t>(
                    std::clamp(static_cast<float>(orig[i][ci]) * factor, 0.f, 65535.f));
        }
    }
}

// -- Adaptive local contrast (CLAHE) -------------------------------------------
static void legacyMlEnhanceChannel(std::vector<std::vector<uint16_t>*>& rows, const MlEnhanceParams& me)
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
        int valid_pixels = 0;
        for (uint16_t v : vals) {
            if (v == 0) continue; // preserve no-data/water masks
            ++hist[v >> 8];
            ++valid_pixels;
        }

        const int tile_area  = valid_pixels;
        if (tile_area == 0) {
            std::array<uint16_t, 256> identity = {};
            for (int b = 0; b < 256; ++b)
                identity[b] = static_cast<uint16_t>(b * 257u);
            return identity;
        }
        const int clip_count = std::max(1, static_cast<int>(clip * tile_area / 256));
        int excess = 0;
        for (int b = 0; b < 256; ++b)
            if (hist[b] > clip_count) { excess += hist[b] - clip_count; hist[b] = clip_count; }
        const int add_each = excess / 256;
        for (int b = 0; b < 256; ++b) hist[b] += add_each;
        const int remainder = excess % 256;
        // Spread the remainder across the histogram instead of dropping it;
        // deterministic spacing avoids biasing only the darkest bins.
        for (int i = 0; i < remainder; ++i)
            ++hist[(i * 256) / remainder];

        int total = 0;
        for (int b = 0; b < 256; ++b) total += hist[b];

        std::array<uint16_t, 256> cdf = {};
        int cumsum = 0;
        int cdf_min = 0;
        for (int b = 0; b < 256; ++b) {
            cdf_min += hist[b];
            if (hist[b] > 0) break;
        }
        if (total <= cdf_min) {
            for (int b = 0; b < 256; ++b)
                cdf[b] = static_cast<uint16_t>(b * 257u);
            return cdf;
        }
        const int denominator = std::max(1, total - cdf_min);
        for (int b = 0; b < 256; ++b) {
            cumsum += hist[b];
            cdf[b] = static_cast<uint16_t>(std::clamp(
                (cumsum - cdf_min) * 65535 / denominator, 0, 65535));
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
            if (side[ci] == 0) continue;
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

// UI-facing adapters. The Qt views and node graph intentionally share the
// lower-layer implementation in pipeline/SidescanEnhancementAlgorithms.
bool beamPatternChannel(std::vector<std::vector<uint16_t>*>& rows,
                        const BeamPatternParams& settings)
{
    return pipeline::enhancement::applyBeamPattern(rows, settings);
}

bool arnChannel(std::vector<std::vector<uint16_t>*>& rows,
                const ArnParams& settings)
{
    return pipeline::enhancement::applyArn(rows, settings);
}

bool destripeChannel(std::vector<std::vector<uint16_t>*>& rows,
                     const DestripeParams& settings)
{
    return pipeline::enhancement::applyDestripe(rows, settings);
}

bool mlEnhanceChannel(std::vector<std::vector<uint16_t>*>& rows,
                      const MlEnhanceParams& settings)
{
    return pipeline::enhancement::applyAdaptiveContrast(rows, settings);
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
                  if (a->timestamp_us != b->timestamp_us)
                      return a->timestamp_us < b->timestamp_us;
                  if (a->ping_number != b->ping_number)
                      return a->ping_number < b->ping_number;
                  return a->id < b->id;
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

    // A mixed legacy store may contain both baked and raw rows. Estimate each
    // operator exclusively from raw rows: already-corrected amplitudes are in a
    // different statistical domain and would bias the new correction curve.
    const auto applyUnlessBaked = [&](core::CorrectionFlag flag,
                                      const auto& operation) {
        std::vector<std::vector<uint16_t>*> unbaked_rows;
        unbaked_rows.reserve(chan.size());
        for (size_t i = 0; i < chan.size(); ++i)
            if (!core::hasCorrectionFlag(chan[i]->correction_flags, flag))
                unbaked_rows.push_back(rows[i]);
        if (unbaked_rows.empty()) return;
        if (!operation(unbaked_rows)) return;
        for (size_t i = 0; i < chan.size(); ++i)
            if (!core::hasCorrectionFlag(chan[i]->correction_flags, flag))
                chan[i]->correction_flags |= flag;
    };

    if (params.beam_pattern.enabled)
        applyUnlessBaked(core::CorrectionFlag::BeamPattern,
                         [&](auto& active) { return beamPatternChannel(active, params.beam_pattern); });
    if (params.arn.enabled)
        applyUnlessBaked(core::CorrectionFlag::Arn,
                         [&](auto& active) { return arnChannel(active, params.arn); });
    if (params.destripe.enabled)
        applyUnlessBaked(core::CorrectionFlag::Destriping,
                         [&](auto& active) { return destripeChannel(active, params.destripe); });
    if (params.ml_enhance.enabled)
        applyUnlessBaked(core::CorrectionFlag::AdaptiveContrast,
                         [&](auto& active) { return mlEnhanceChannel(active, params.ml_enhance); });

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

void applyPerPingCalibration(core::SidescanPing& ping,
                             const WaterfallParams& params)
{
    std::vector<core::SidescanPing> one;
    one.reserve(1);
    one.push_back(std::move(ping));
    auto& item = one.front();

    if (params.tvg.enabled
            && !core::hasCorrectionFlag(item.correction_flags,
                                        core::CorrectionFlag::Tvg)) {
        app::corrections::applyTvg(one, params.tvg);
    }
    if (params.arc.enabled
            && !core::hasCorrectionFlag(item.correction_flags,
                                        core::CorrectionFlag::Arc)) {
        app::corrections::applyArc(one, params.arc);
    }
    ping = std::move(one.front());
}

void applyCalibration(std::vector<core::SidescanPing>& pings,
                      const WaterfallParams& params)
{
    for (auto& ping : pings)
        applyPerPingCalibration(ping, params);
    applyAgc(pings, params);
}

void applyContextCalibrationAndImaging(
    std::vector<core::SidescanPing>& pings,
    const WaterfallParams& params)
{
    applyAgc(pings, params);
    applyImagingChain(pings, params);
}

void applyDisplayPipeline(std::vector<core::SidescanPing>& pings, const WaterfallParams& params)
{
    applyCalibration(pings, params);
    applyImagingChain(pings, params);
}

SssAutoStretch computeAutoStretch(const std::vector<core::SidescanPing>& pings)
{
    constexpr int kBins = 1024;
    uint64_t hist[kBins] = {};
    for (const auto& ping : pings)
        for (const auto& sample : ping.samples)
            if (sample.amplitude > 0) ++hist[sample.amplitude >> 6];
    uint64_t total = 0;
    for (int i = 0; i < kBins; ++i) total += hist[i];
    if (total == 0) return {};

    const uint64_t tail = std::max<uint64_t>(1, total / 100u);
    const uint64_t hi_target = total - tail;
    uint64_t cumulative = 0;
    int low = 0, high = kBins - 1;
    bool found_low = false;
    for (int i = 0; i < kBins; ++i) {
        cumulative += hist[i];
        if (!found_low && cumulative >= tail) { low = i; found_low = true; }
        if (cumulative >= hi_target) { high = i; break; }
    }
    const uint32_t low_value = static_cast<uint32_t>(low) << 6;
    const uint32_t high_value = std::min<uint32_t>(
        (static_cast<uint32_t>(high) + 1u) << 6, 65535u);
    return {float(low_value) / 65535.f,
            float(std::max(high_value, low_value + 1u)) / 65535.f};
}

} // namespace dolphin::ui::imaging
