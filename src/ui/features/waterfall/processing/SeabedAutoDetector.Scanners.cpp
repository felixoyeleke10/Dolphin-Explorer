// SeabedAutoDetector.Scanners.cpp — per-ping amplitude scanners
//
// Four independent scanners that each return a (range_m, confidence) pair for
// one SidescanPing.  A fifth wrapper (scanPingBest) runs all four and returns
// a consensus result.  combineChannels merges port and starboard results.
//
// scanPingThreshold   — first sample exceeding a fraction of the ping peak.
// scanPingFirstReturn — first sample above noise floor × min_snr.
// scanPingGradient    — sample with the steepest rising edge.
// scanPingHeuristic   — peak-contrast seabed echo shape scoring.
// scanPingBest        — consensus of the four scanners.
// combineChannels     — port/starboard agreement combination.

#include "ui/features/waterfall/processing/SeabedAutoDetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dolphin::ui {

SeabedAutoDetector::ScanResult
SeabedAutoDetector::scanPingThreshold(const core::SidescanPing* ping,
                                       const SeabedAutoParams&   p)
{
    if (!ping || ping->samples.empty() || ping->slant_range_m <= 0.f)
        return {};

    const int n    = static_cast<int>(ping->samples.size());
    const int skip = blankingSamples(ping, p);

    uint32_t peak = 0;
    for (int i = skip; i < n; ++i)
        if (ping->samples[i].amplitude > peak)
            peak = ping->samples[i].amplitude;
    if (peak == 0) return {};

    const uint32_t threshold =
        std::max(1u, static_cast<uint32_t>(peak * p.threshold_pct / 100.f));

    for (int i = skip; i < n; ++i) {
        if (ping->samples[i].amplitude >= threshold) {
            const bool sustained = (i + 1 >= n)
                || (ping->samples[i + 1].amplitude >= threshold / 2u);
            if (!sustained) continue;

            const float conf = static_cast<float>(ping->samples[i].amplitude)
                             / static_cast<float>(peak);
            return { rangeAt(ping, i), conf };
        }
    }
    return {};
}

SeabedAutoDetector::ScanResult
SeabedAutoDetector::scanPingFirstReturn(const core::SidescanPing* ping,
                                         const SeabedAutoParams&   p)
{
    if (!ping || ping->samples.empty() || ping->slant_range_m <= 0.f)
        return {};

    const int n    = static_cast<int>(ping->samples.size());
    const int skip = blankingSamples(ping, p);

    // Noise from far-range water column (last 15% of swath) — avoids contamination
    // from nadir clutter near blanking which inflates min_amp and masks true seabed.
    const int far_skip  = std::max(skip, static_cast<int>(n * 0.82f));
    const int   noise_w     = std::max(4, n - far_skip);
    const float noise_floor = estimateNoiseFloor(ping, far_skip, noise_w);
    const float min_amp     = std::max(1.f, noise_floor * p.min_snr);

    uint32_t peak = 0;
    for (int i = skip; i < n; ++i)
        peak = std::max(peak, static_cast<uint32_t>(ping->samples[i].amplitude));

    for (int i = skip; i < n; ++i) {
        if (static_cast<float>(ping->samples[i].amplitude) >= min_amp) {
            const bool sustained = (i + 1 >= n)
                || (static_cast<float>(ping->samples[i + 1].amplitude) >= min_amp * 0.5f);
            if (!sustained) continue;

            const float conf = (peak > 0)
                ? std::min(1.f, static_cast<float>(ping->samples[i].amplitude)
                               / static_cast<float>(peak))
                : 0.f;
            return { rangeAt(ping, i), conf };
        }
    }
    return {};
}

SeabedAutoDetector::ScanResult
SeabedAutoDetector::scanPingGradient(const core::SidescanPing* ping,
                                      const SeabedAutoParams&   p)
{
    if (!ping || ping->samples.empty() || ping->slant_range_m <= 0.f)
        return {};

    const int n    = static_cast<int>(ping->samples.size());
    const int skip = blankingSamples(ping, p);
    if (skip >= n - 1) return {};

    int     best_idx  = -1;
    int32_t best_grad = 0;
    for (int i = skip; i < n - 1; ++i) {
        const int32_t g = static_cast<int32_t>(ping->samples[i + 1].amplitude)
                        - static_cast<int32_t>(ping->samples[i].amplitude);
        if (g > best_grad) {
            best_grad = g;
            best_idx  = i;
        }
    }
    if (best_idx < 0 || best_grad <= 0) return {};

    uint32_t peak = 0;
    for (int i = skip; i < n; ++i)
        peak = std::max(peak, static_cast<uint32_t>(ping->samples[i].amplitude));
    const float conf = (peak > 0)
        ? std::min(1.f, static_cast<float>(best_grad) / static_cast<float>(peak))
        : 0.f;

    // Report the onset side (best_idx) not the peak side (best_idx+1).
    return { rangeAt(ping, best_idx), conf };
}

SeabedAutoDetector::ScanResult
SeabedAutoDetector::scanPingHeuristic(const core::SidescanPing* ping,
                                       const SeabedAutoParams&   p)
{
    if (!ping || ping->samples.empty() || ping->slant_range_m <= 0.f)
        return {};

    const int n    = static_cast<int>(ping->samples.size());
    const int skip = blankingSamples(ping, p);
    if (skip >= n - 4) return {};

    uint32_t peak = 0;
    for (int i = skip; i < n; ++i)
        peak = std::max(peak, static_cast<uint32_t>(ping->samples[i].amplitude));
    if (peak == 0) return {};

    // Far-range noise window — same rationale as scanPingFirstReturn
    const int far_skip2  = std::max(skip, static_cast<int>(n * 0.82f));
    const int   noise_w     = std::max(4, n - far_skip2);
    const float noise_floor = estimateNoiseFloor(ping, far_skip2, noise_w);
    const float min_amp     = std::max(1.f, noise_floor * p.min_snr * 0.72f);

    int   best_i     = -1;
    float best_score = 0.f;

    for (int i = skip + 1; i < n - 3; ++i) {
        const float amp = static_cast<float>(ping->samples[i].amplitude);
        if (amp < min_amp) continue;

        const int pre0  = std::max(skip, i - 6);
        const int pre_n = std::max(1, i - pre0);
        float pre_mean = 0.f;
        for (int j = pre0; j < i; ++j)
            pre_mean += static_cast<float>(ping->samples[j].amplitude);
        pre_mean /= static_cast<float>(pre_n);

        const int post1  = std::min(n, i + 7);
        const int post_n = std::max(1, post1 - i);
        float post_mean = 0.f;
        for (int j = i; j < post1; ++j)
            post_mean += static_cast<float>(ping->samples[j].amplitude);
        post_mean /= static_cast<float>(post_n);

        const float grad = std::max(0.f,
            static_cast<float>(ping->samples[i].amplitude)
          - static_cast<float>(ping->samples[i - 1].amplitude));
        const float contrast = (post_mean + 1.f) / (pre_mean + 1.f);
        const float amp_score = std::min(1.f, amp / static_cast<float>(peak));
        const float grad_score = std::min(1.f, grad / static_cast<float>(peak));
        const float contrast_score = std::min(1.f, (contrast - 1.f) / 4.f);
        const float sustain_score = std::min(1.f, post_mean / static_cast<float>(peak));
        const float quiet_score = 1.f - std::min(1.f, pre_mean / static_cast<float>(peak));

        const float range_frac = static_cast<float>(i) / static_cast<float>(std::max(1, n - 1));
        // Penalise the first 12% of the swath — nadir clutter commonly extends
        // well past the 6% blanking zone, especially in shallow water.
        const float early_penalty = range_frac < 0.12f ? range_frac / 0.12f : 1.f;
        const float late_penalty  = range_frac > 0.92f ? (1.f - range_frac) / 0.08f : 1.f;
        const float range_score = std::clamp(std::min(early_penalty, late_penalty), 0.f, 1.f);

        const float score =
            0.24f * amp_score
          + 0.24f * grad_score
          + 0.22f * contrast_score
          + 0.18f * sustain_score
          + 0.08f * quiet_score
          + 0.04f * range_score;

        if (score > best_score) {
            best_score = score;
            best_i = i;
        }
    }

    if (best_i < 0 || best_score < 0.18f) return {};
    return {rangeAt(ping, best_i), std::min(1.f, best_score)};
}

// Runs all per-ping scanners and returns a confidence-weighted best candidate.
// When two or more methods agree within half of max_delta_m, their results are
// averaged and their confidence is boosted.  A lone candidate is penalised.
SeabedAutoDetector::ScanResult
SeabedAutoDetector::scanPingBest(const core::SidescanPing* ping,
                                  const SeabedAutoParams&   p)
{
    if (!ping || ping->samples.empty() || ping->slant_range_m <= 0.f)
        return {};

    const ScanResult thr  = scanPingThreshold  (ping, p);
    const ScanResult fr   = scanPingFirstReturn(ping, p);
    const ScanResult grad = scanPingGradient   (ping, p);
    const ScanResult heur = scanPingHeuristic  (ping, p);

    // Near-nadir override: if every non-heuristic scanner that got a hit landed
    // in the first 12% of swath, but the heuristic found a credible return at
    // ≥1.8× that range, the consensus is nadir clutter — return the heuristic.
    if (heur.range_m > 0.f && heur.confidence >= 0.22f) {
        float best_range = -1.f, best_conf = -1.f;
        for (const auto& c : {thr, fr, grad}) {
            if (c.range_m > 0.f && c.confidence > best_conf) {
                best_conf  = c.confidence;
                best_range = c.range_m;
            }
        }
        const float nadir_cutoff = ping->slant_range_m * 0.12f;
        if (best_range > 0.f && best_range < nadir_cutoff
                && heur.range_m >= best_range * 1.8f)
            return {heur.range_m, heur.confidence};
    }

    // Agreement window: half the ping-to-ping gate, minimum 2 m.
    const float agree_w = std::max(2.f, p.max_delta_m * 0.5f);

    struct Pair { float range; float conf; };
    Pair candidates[4];
    int  nc = 0;
    if (thr.range_m  > 0.f) candidates[nc++] = {thr.range_m,  thr.confidence};
    if (fr.range_m   > 0.f) candidates[nc++] = {fr.range_m,   fr.confidence};
    if (grad.range_m > 0.f) candidates[nc++] = {grad.range_m, grad.confidence};
    if (heur.range_m > 0.f) candidates[nc++] = {heur.range_m, heur.confidence};

    if (nc == 0) return {};
    if (nc == 1) return {candidates[0].range, candidates[0].conf * 0.80f};

    float wsum = 0.f, wrange = 0.f, max_conf = 0.f;
    int   agreed = 0;
    for (int a = 0; a < nc; ++a) {
        bool has_partner = false;
        for (int b = 0; b < nc; ++b) {
            if (a == b) continue;
            if (std::fabs(candidates[a].range - candidates[b].range) <= agree_w) {
                has_partner = true;
                break;
            }
        }
        if (has_partner) {
            wrange += candidates[a].range * candidates[a].conf;
            wsum   += candidates[a].conf;
            max_conf = std::max(max_conf, candidates[a].conf);
            ++agreed;
        }
    }

    if (agreed >= 2) {
        // Find the highest-confidence agreeing pair and restrict the average to its
        // cluster only.  Without this, two independent clusters (e.g., nadir clutter
        // at 6 m and the true seabed at 30 m) both show internal agreement and the
        // naive average lands at a nonsense midpoint between them.
        float best_pc = 0.f, best_ctr = -1.f;
        for (int a = 0; a < nc; ++a) {
            for (int b = a + 1; b < nc; ++b) {
                if (std::fabs(candidates[a].range - candidates[b].range) > agree_w) continue;
                const float pc = candidates[a].conf + candidates[b].conf;
                if (pc > best_pc) {
                    best_pc  = pc;
                    best_ctr = (candidates[a].range * candidates[a].conf
                              + candidates[b].range * candidates[b].conf) / pc;
                }
            }
        }
        float sw = 0.f, swr = 0.f, mc = 0.f;
        for (int a = 0; a < nc; ++a) {
            if (best_ctr >= 0.f && std::fabs(candidates[a].range - best_ctr) <= agree_w) {
                swr += candidates[a].range * candidates[a].conf;
                sw  += candidates[a].conf;
                mc   = std::max(mc, candidates[a].conf);
            }
        }
        if (sw > 0.f) return {swr / sw, std::min(1.f, mc + 0.10f)};
        return {wrange / wsum, std::min(1.f, max_conf + 0.10f)};
    }

    // No consensus — highest-confidence candidate, penalised.
    Pair best = candidates[0];
    for (int i = 1; i < nc; ++i)
        if (candidates[i].conf > best.conf) best = candidates[i];
    return {best.range, best.conf * 0.70f};
}

// Combines port and starboard ScanResults.  Uses channel_agree_m (the
// port/stbd spatial agreement parameter) — NOT max_delta_m (which is the
// ping-to-ping continuity gate).
SeabedAutoDetector::ScanResult
SeabedAutoDetector::combineChannels(const ScanResult& sb, const ScanResult& pt,
                                     float channel_agree_m)
{
    if (sb.range_m > 0.f && pt.range_m > 0.f) {
        const float delta = std::fabs(sb.range_m - pt.range_m);
        if (delta <= channel_agree_m) {
            const float tw = sb.confidence + pt.confidence;
            return {
                (tw > 0.f)
                    ? (sb.range_m * sb.confidence + pt.range_m * pt.confidence) / tw
                    : (sb.range_m + pt.range_m) * 0.5f,
                std::min(1.f, tw * 0.5f + 0.12f)
            };
        }
        // Channels disagree — pick higher confidence but penalise the result so
        // the tracker treats this ping as uncertain rather than a reliable anchor.
        const ScanResult& w = (sb.confidence >= pt.confidence) ? sb : pt;
        return { w.range_m, w.confidence * 0.58f };
    }
    if (sb.range_m > 0.f) return sb;
    if (pt.range_m > 0.f) return pt;
    return {};
}

} // namespace dolphin::ui
