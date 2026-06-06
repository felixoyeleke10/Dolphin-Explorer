// WaterfallProcessingAlgorithms.cpp — amplitude processing algorithm implementations.
//
// Pre-assembly algorithms (TVG, ARC, AGC normalise) delegate to the canonical
// implementations in app/corrections/CorrectionAlgorithms.cpp so both the
// display-time pipeline and the bake-to-dlpd path share identical code.
#include "ui/features/waterfall/processing/WaterfallProcessingAlgorithms.h"
#include "app/corrections/CorrectionAlgorithms.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dolphin::ui {
namespace detail {

// -- Pre-assembly --------------------------------------------------------------

void normalizeRawAmplitudes(std::vector<core::SidescanPing>& pings,
                            const WaterfallParams& params)
{
    app::corrections::normalizeAmplitudes(pings, params.agc);
}

void stretchRawAmplitudes(std::vector<core::SidescanPing>& pings)
{
    constexpr uint32_t kBins  = 1024;
    constexpr float    kScale = float(kBins) / 65536.f;

    uint32_t hist_p[kBins] = {}, hist_s[kBins] = {};
    uint32_t total_p = 0, total_s = 0;

    for (const auto& ping : pings) {
        const bool is_port = (ping.channel == core::SidescanChannel::Port);
        auto& hist  = is_port ? hist_p  : hist_s;
        auto& total = is_port ? total_p : total_s;
        for (const auto& s : ping.samples)
            if (s.amplitude > 0) {
                hist[static_cast<uint32_t>(s.amplitude * kScale)]++;
                ++total;
            }
    }

    auto findStretch = [&](const uint32_t* hist, uint32_t total,
                           uint32_t& raw_lo, uint32_t& raw_hi) -> bool {
        if (total == 0) return false;
        const uint32_t lo_tgt = std::max(uint32_t(1), total / 100u);
        const uint32_t hi_tgt = total - lo_tgt;
        uint32_t p01_bin = 0, p99_bin = kBins - 1;
        uint32_t cum = 0; bool found_lo = false;
        for (uint32_t i = 0; i < kBins; ++i) {
            cum += hist[i];
            if (!found_lo && cum >= lo_tgt) { p01_bin = i; found_lo = true; }
            if (cum >= hi_tgt)              { p99_bin = i; break; }
        }
        raw_lo = p01_bin * 65536u / kBins;
        raw_hi = std::min((p99_bin + 1) * 65536u / kBins, uint32_t(65535));
        return raw_hi > raw_lo;
    };

    uint32_t p_lo = 0, p_hi = 65535, s_lo = 0, s_hi = 65535;
    const bool ok_p = findStretch(hist_p, total_p, p_lo, p_hi);
    const bool ok_s = findStretch(hist_s, total_s, s_lo, s_hi);
    if (!ok_p && !ok_s) return;

    for (auto& ping : pings) {
        const bool is_port = (ping.channel == core::SidescanChannel::Port);
        if (is_port && !ok_p) continue;
        if (!is_port && !ok_s) continue;
        const float raw_lo   = static_cast<float>(is_port ? p_lo : s_lo);
        const float inv_span = is_port ? (65535.f / float(p_hi - p_lo))
                                       : (65535.f / float(s_hi - s_lo));
        for (auto& s : ping.samples) {
            const int v = static_cast<int>(
                (static_cast<float>(s.amplitude) - raw_lo) * inv_span + 0.5f);
            s.amplitude = static_cast<uint16_t>(std::clamp(v, 0, 65535));
        }
    }
}

void applyTvg(std::vector<core::SidescanPing>& pings, const WaterfallParams& params)
{
    app::corrections::applyTvg(pings, params.tvg);
}

void applyArc(std::vector<core::SidescanPing>& pings, const WaterfallParams& params)
{
    app::corrections::applyArc(pings, params.arc);
}

// -- Post-assembly -------------------------------------------------------------

void applyBeamPattern(std::vector<PingRow>& rows, const WaterfallParams& params)
{
    const BeamPatternParams& bp = params.beam_pattern;
    if (!bp.enabled || rows.empty()) return;

    int ns_port = 0, ns_stbd = 0;
    for (const auto& r : rows) {
        ns_port = std::max(ns_port, static_cast<int>(r.port.size()));
        ns_stbd = std::max(ns_stbd, static_cast<int>(r.stbd.size()));
    }

    constexpr float kRefLevel = 32768.f;

    auto normalizeChannel = [&](bool port, int ns) {
        if (ns == 0) return;

        std::vector<float> mean(ns, 0.f);
        std::vector<int>   cnt(ns, 0);
        for (const auto& row : rows) {
            const auto& side = port ? row.port : row.stbd;
            for (int ci = 0; ci < static_cast<int>(side.size()) && ci < ns; ++ci)
                if (side[ci] > 0) { mean[ci] += side[ci]; ++cnt[ci]; }
        }
        for (int ci = 0; ci < ns; ++ci)
            mean[ci] = cnt[ci] > 0 ? mean[ci] / cnt[ci] : kRefLevel;

        if (bp.smooth_radius > 0) {
            std::vector<float> smooth(ns, 0.f);
            for (int ci = 0; ci < ns; ++ci) {
                float sum = 0.f; int k = 0;
                for (int j = std::max(0, ci - bp.smooth_radius);
                         j <= std::min(ns - 1, ci + bp.smooth_radius); ++j) {
                    sum += mean[j]; ++k;
                }
                smooth[ci] = k > 0 ? sum / k : mean[ci];
            }
            mean = std::move(smooth);
        }

        for (auto& row : rows) {
            auto& side = port ? row.port : row.stbd;
            for (int ci = 0; ci < static_cast<int>(side.size()) && ci < ns; ++ci) {
                const float ref     = std::max(1024.f, mean[ci]);
                const float factor  = kRefLevel / ref;
                const float blended = side[ci] * (1.f + (factor - 1.f) * bp.strength);
                side[ci] = static_cast<uint16_t>(std::clamp(blended, 0.f, 65535.f));
            }
        }
    };

    normalizeChannel(true,  ns_port);
    normalizeChannel(false, ns_stbd);
}

void applyArn(std::vector<PingRow>& rows, const WaterfallParams& params)
{
    const ArnParams& arn = params.arn;
    if (!arn.enabled || rows.empty()) return;

    const int n_rows = static_cast<int>(rows.size());
    constexpr float kRefLevel = 32768.f;
    constexpr float kPctFrac  = 0.40f;
    const float gain_cap = std::max(1.f, std::pow(10.f, arn.gain_cap_db / 20.f));
    const float strength = std::clamp(arn.strength, 0.f, 1.f);

    int ns_port = 0, ns_stbd = 0;
    for (const auto& r : rows) {
        ns_port = std::max(ns_port, static_cast<int>(r.port.size()));
        ns_stbd = std::max(ns_stbd, static_cast<int>(r.stbd.size()));
    }

    auto normalizeChannel = [&](bool port, int ns) {
        if (ns == 0) return;
        std::vector<float>    ref(ns, kRefLevel);
        std::vector<bool>     valid(ns, false);
        std::vector<uint16_t> col_vals;
        col_vals.reserve(n_rows);

        for (int ci = 0; ci < ns; ++ci) {
            col_vals.clear();
            for (const auto& r : rows) {
                const auto& side = port ? r.port : r.stbd;
                if (ci < static_cast<int>(side.size()) && side[ci] > 0)
                    col_vals.push_back(side[ci]);
            }
            if (static_cast<int>(col_vals.size()) < 3) continue;
            const int k = std::clamp(
                static_cast<int>(col_vals.size() * kPctFrac),
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
                          k <= std::min(ns - 1, ci + arn.column_smooth); ++k) {
                    if (valid[k]) {
                        sum += ref[k];
                        ++cnt;
                    }
                }
                smooth[ci] = cnt > 0 ? sum / cnt : ref[ci];
                smooth_valid[ci] = cnt > 0;
            }
            ref = std::move(smooth);
            valid = std::move(smooth_valid);
        }

        for (auto& row : rows) {
            auto& side = port ? row.port : row.stbd;
            for (int ci = 0; ci < static_cast<int>(side.size()) && ci < ns; ++ci) {
                if (!valid[ci] || ref[ci] < 1.f) continue;
                const float factor  = std::clamp(kRefLevel / ref[ci],
                                                  1.f / gain_cap, gain_cap);
                const float blended = side[ci] * (1.f + (factor - 1.f) * strength);
                side[ci] = static_cast<uint16_t>(std::clamp(blended, 0.f, 65535.f));
            }
        }
    };

    normalizeChannel(true,  ns_port);
    normalizeChannel(false, ns_stbd);
}

void applyDestripe(std::vector<PingRow>& rows, const WaterfallParams& params)
{
    const DestripeParams& d = params.destripe;
    if (!d.enabled || rows.empty()) return;

    const int n     = static_cast<int>(rows.size());
    const int hw    = std::max(1, d.window / 2);
    const int nsegs = std::max(1, d.subdivision);
    const float cap = std::max(1.001f, d.capping);

    auto destripeChannel = [&](bool port) {
        int ns = 0;
        for (const auto& r : rows)
            ns = std::max(ns, static_cast<int>((port ? r.port : r.stbd).size()));
        if (ns == 0) return;

        std::vector<std::vector<uint16_t>> orig(n, std::vector<uint16_t>(ns, 0));
        for (int i = 0; i < n; ++i) {
            const auto& side = port ? rows[i].port : rows[i].stbd;
            for (int ci = 0; ci < static_cast<int>(side.size()); ++ci)
                orig[i][ci] = side[ci];
        }

        const int seg_w = (ns + nsegs - 1) / nsegs;

        for (int seg = 0; seg < nsegs; ++seg) {
            const int c0 = seg * seg_w;
            const int c1 = std::min(c0 + seg_w, ns);
            if (c0 >= ns) break;

            std::vector<double> seg_sum(n, 0.0);
            std::vector<int>    seg_cnt(n, 0);
            for (int i = 0; i < n; ++i)
                for (int ci = c0; ci < c1; ++ci)
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
                auto& side = port ? rows[i].port : rows[i].stbd;
                for (int ci = c0; ci < c1 && ci < static_cast<int>(side.size()); ++ci)
                    side[ci] = static_cast<uint16_t>(
                        std::clamp(static_cast<float>(orig[i][ci]) * factor, 0.f, 65535.f));
            }
        }
    };

    destripeChannel(true);
    destripeChannel(false);
}

void applyMlEnhance(std::vector<PingRow>& rows, const WaterfallParams& params)
{
    const MlEnhanceParams& me = params.ml_enhance;
    if (!me.enabled || rows.empty()) return;

    const int   n_rows = static_cast<int>(rows.size());
    const int   tw     = std::max(16, me.tile_samps);
    const int   th     = std::max(16, me.tile_pings);
    const float clip   = std::max(1.f, me.clip_limit);

    auto buildCdf = [&](const std::vector<uint16_t>& vals) -> std::array<uint16_t, 256> {
        std::array<int, 256> hist = {};
        for (uint16_t v : vals) ++hist[v >> 8];

        const int tile_area  = static_cast<int>(vals.size());
        const int clip_count = std::max(1, static_cast<int>(clip * tile_area / 256));
        int excess = 0;
        for (int b = 0; b < 256; ++b) {
            if (hist[b] > clip_count) { excess += hist[b] - clip_count; hist[b] = clip_count; }
        }
        const int add_each = excess / 256;
        for (int b = 0; b < 256; ++b) hist[b] += add_each;

        int total = 0;
        for (int b = 0; b < 256; ++b) total += hist[b];

        std::array<uint16_t, 256> cdf = {};
        int cumsum = 0;
        for (int b = 0; b < 256; ++b) {
            cumsum += hist[b];
            cdf[b] = static_cast<uint16_t>(
                std::clamp(cumsum * 65535 / std::max(1, total), 0, 65535));
        }
        return cdf;
    };

    auto processChannel = [&](bool port) {
        int ns = 0;
        for (const auto& r : rows)
            ns = std::max(ns, static_cast<int>((port ? r.port : r.stbd).size()));
        if (ns == 0) return;

        const int ntc = (ns     + tw - 1) / tw;
        const int ntr = (n_rows + th - 1) / th;

        std::vector<std::array<uint16_t, 256>> cdfs(ntr * ntc);
        for (auto& cdf : cdfs)
            for (int b = 0; b < 256; ++b) cdf[b] = static_cast<uint16_t>(b * 257u);

        for (int tr = 0; tr < ntr; ++tr) {
            const int r0 = tr * th, r1 = std::min(r0 + th, n_rows);
            for (int tc = 0; tc < ntc; ++tc) {
                const int c0 = tc * tw, c1 = std::min(c0 + tw, ns);
                std::vector<uint16_t> pixels;
                pixels.reserve((r1 - r0) * (c1 - c0));
                for (int ri = r0; ri < r1; ++ri) {
                    const auto& side = port ? rows[ri].port : rows[ri].stbd;
                    for (int ci = c0; ci < c1 && ci < static_cast<int>(side.size()); ++ci)
                        pixels.push_back(side[ci]);
                }
                if (!pixels.empty())
                    cdfs[tr * ntc + tc] = buildCdf(pixels);
            }
        }

        for (int ri = 0; ri < n_rows; ++ri) {
            auto& side   = port ? rows[ri].port : rows[ri].stbd;
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
                    wr0 * (wc0 * cdfs[tr0 * ntc + tc0][v] + wc1 * cdfs[tr0 * ntc + tc1][v]) +
                    wr1 * (wc0 * cdfs[tr1 * ntc + tc0][v] + wc1 * cdfs[tr1 * ntc + tc1][v]);

                side[ci] = static_cast<uint16_t>(std::clamp(mapped, 0.f, 65535.f));
            }
        }
    };

    processChannel(true);
    processChannel(false);
}

} // namespace detail
} // namespace dolphin::ui
