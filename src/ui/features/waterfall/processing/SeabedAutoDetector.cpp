// SeabedAutoDetector.cpp — stateless seabed auto-detection
//
// Algorithm overview (ContinuityAware mode):
//   1. Per-ping: run threshold, gradient, and first-return scanners; score by
//      inter-method agreement to produce a single best candidate per channel.
//   2. Channel combination: average port/stbd when within channel_agree_m;
//      fall back to the stronger channel otherwise.
//   3. Bidirectional continuity: forward pass + backward pass, each with an
//      EMA rate predictor and max_delta_m gate.  Merge forward/backward by
//      confidence-weighted average when they agree, or by winner when they diverge.
//   4. Gap-fill: linear interpolation between anchors; filled rows keep
//      detected=false so callers can distinguish real detections from guesses.

#include "ui/features/waterfall/processing/SeabedAutoDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  Utility helpers
// ─────────────────────────────────────────────────────────────────────────────

float SeabedAutoDetector::rangeAt(const core::SidescanPing* ping, int i)
{
    const auto& s = ping->samples[i];
    if (s.range_m > 0.f) return s.range_m;
    const int n = static_cast<int>(ping->samples.size());
    if (n <= 1) return ping->slant_range_m;
    return ping->slant_range_m * static_cast<float>(i) / static_cast<float>(n - 1);
}

int SeabedAutoDetector::blankingSamples(const core::SidescanPing* ping,
                                         const SeabedAutoParams& p)
{
    const int n = static_cast<int>(ping->samples.size());
    if (p.blanking_m > 0.f && ping->slant_range_m > 0.f)
        return std::max(1, static_cast<int>(n * p.blanking_m / ping->slant_range_m));
    return std::max(1, static_cast<int>(n * p.blanking_pct / 100.f));
}

// Estimates noise from the water-column window immediately after blanking.
// Takes the 15th percentile of `window_n` samples starting at `skip` — robust
// against any early weak returns that may appear in the window.
float SeabedAutoDetector::estimateNoiseFloor(const core::SidescanPing* ping,
                                              int skip, int window_n)
{
    const int n   = static_cast<int>(ping->samples.size());
    const int end = std::min(n, skip + window_n);
    if (end <= skip) return 0.f;

    std::vector<uint32_t> vals;
    vals.reserve(static_cast<size_t>(end - skip));
    for (int i = skip; i < end; ++i)
        vals.push_back(ping->samples[i].amplitude);

    const int idx = std::max(0, static_cast<int>(vals.size() * 0.15f) - 1);
    std::nth_element(vals.begin(), vals.begin() + idx, vals.end());
    return static_cast<float>(vals[static_cast<size_t>(idx)]);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-ping scanners
// ─────────────────────────────────────────────────────────────────────────────

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

    // Agreement window: half the ping-to-ping gate, minimum 2 m.
    const float agree_w = std::max(2.f, p.max_delta_m * 0.5f);

    struct Pair { float range; float conf; };
    Pair candidates[4];
    int  nc = 0;
    if (thr.range_m  >= 0.f) candidates[nc++] = {thr.range_m,  thr.confidence};
    if (fr.range_m   >= 0.f) candidates[nc++] = {fr.range_m,   fr.confidence};
    if (grad.range_m >= 0.f) candidates[nc++] = {grad.range_m, grad.confidence};
    if (heur.range_m >= 0.f) candidates[nc++] = {heur.range_m, heur.confidence};

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
        return {wrange / wsum, std::min(1.f, max_conf + 0.10f)};
    }

    // No consensus — highest-confidence candidate, penalised.
    Pair best = candidates[0];
    for (int i = 1; i < nc; ++i)
        if (candidates[i].conf > best.conf) best = candidates[i];
    return {best.range, best.conf * 0.70f};
}

// ─────────────────────────────────────────────────────────────────────────────
//  Channel combination
// ─────────────────────────────────────────────────────────────────────────────

// Combines port and starboard ScanResults.  Uses channel_agree_m (the
// port/stbd spatial agreement parameter) — NOT max_delta_m (which is the
// ping-to-ping continuity gate).
SeabedAutoDetector::ScanResult
SeabedAutoDetector::combineChannels(const ScanResult& sb, const ScanResult& pt,
                                     float channel_agree_m)
{
    if (sb.range_m >= 0.f && pt.range_m >= 0.f) {
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
    if (sb.range_m >= 0.f) return sb;
    if (pt.range_m >= 0.f) return pt;
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bidirectional continuity-aware detection
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// A plain range/confidence pair used inside the directional passes so the
// private ScanResult type doesn't need to escape to the anonymous namespace.
struct TrackResult { float range_m = -1.f; float conf = 0.f; };

// One directional pass through the candidate list.
// step=+1 → forward (0..n-1); step=-1 → backward (n-1..0).
// Accepts a candidate when it is both credible (conf >= kMinConf) and within
// max_delta_m of the extrapolated prediction.  An EMA rate tracks gradual
// seabed slope; the rate sign convention always follows the forward direction.
void trackFromAnchor(
    std::vector<TrackResult>&       out,
    const std::vector<TrackResult>& cands,
    const std::vector<bool>&        is_manual,
    const std::vector<float>&       manual_range,
    int anchor, int step, float max_delta_m)
{
    constexpr float kMinConf   = 0.16f;
    constexpr float kRateAlpha = 0.10f;
    // base_gate: max ping-to-ping jump in flat conditions.
    // The per-step gate widens when est_rate is large — allows the tracker to
    // follow steep seabed slopes once the rate predictor has adapted, without
    // relaxing the wreck-rejection guard in flat areas.
    const float base_gate = (max_delta_m > 0.f)
        ? max_delta_m
        : std::numeric_limits<float>::infinity();

    const int n = static_cast<int>(cands.size());
    if (anchor < 0 || anchor >= n) return;

    float est_range = out[static_cast<size_t>(anchor)].range_m;
    float est_rate  = 0.f;      // forward rate: positive = deeper with increasing index

    const int start = anchor + step;
    const int end   = (step > 0) ? n : -1;

    for (int i = start; i != end; i += step) {
        if (is_manual[i]) {
            est_range = manual_range[i];
            est_rate  = 0.f;
            out[static_cast<size_t>(i)] = {manual_range[i], 1.f};
            continue;
        }

        // Adaptive gate: widens proportionally to current rate so slope-following
        // is not clipped, but stays tight near max_delta_m in flat terrain.
        const float cur_gate = std::isfinite(base_gate)
            ? base_gate + std::fabs(est_rate) * 1.5f
            : base_gate;

        const float pred     = (est_range > 0.f) ? est_range + step * est_rate : -1.f;
        const float cand_r   = cands[static_cast<size_t>(i)].range_m;
        const float conf     = cands[static_cast<size_t>(i)].conf;
        const bool  credible = (cand_r > 0.f) && (conf >= kMinConf);

        if (credible) {
            if (pred < 0.f || std::fabs(cand_r - pred) <= cur_gate) {
                // Update rate in forward convention: positive when deeper going forward.
                const float delta = cand_r - est_range;
                est_rate  = kRateAlpha * (step > 0 ? delta : -delta)
                          + (1.f - kRateAlpha) * est_rate;
                est_range = cand_r;
                out[static_cast<size_t>(i)] = {cand_r, conf};
            } else {
                // Candidate jumped outside gate (wreck / clutter); advance prediction.
                est_range = pred;
            }
        } else if (pred > 0.f) {
            est_range = pred;
            // Damp rate during shadow/dropout gaps — prevents unbounded drift when
            // the tracker is extrapolating through a long shadow behind a wreck.
            est_rate *= 0.94f;
        }
        // else: out[i] stays {-1, 0}
    }
}

int chooseAnchor(const std::vector<TrackResult>& cands,
                 const std::vector<bool>&        is_manual,
                 const std::vector<float>&       manual_range,
                 float                           max_delta_m)
{
    // Manual anchor takes absolute priority — user placed it on the true seabed.
    for (int i = 0; i < static_cast<int>(is_manual.size()); ++i)
        if (is_manual[static_cast<size_t>(i)] && manual_range[static_cast<size_t>(i)] > 0.f)
            return i;

    const int   n        = static_cast<int>(cands.size());
    const float bin      = std::max(2.f, (max_delta_m > 0.f) ? max_delta_m : 5.f);
    constexpr float kMinConf = 0.15f;

    // Build sorted list of {range, index} for candidates that pass the conf threshold.
    std::vector<std::pair<float, int>> valid;
    valid.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        if (cands[static_cast<size_t>(i)].range_m > 0.f &&
            cands[static_cast<size_t>(i)].conf >= kMinConf)
            valid.push_back({cands[static_cast<size_t>(i)].range_m, i});
    if (valid.empty()) return -1;

    std::sort(valid.begin(), valid.end());

    // Sliding-window histogram: find the range bin with the most candidate support.
    // Wrecks are isolated outliers; the true seabed occupies the majority of rows.
    float mode_center = valid.front().first;
    int   mode_cnt    = 0;
    for (size_t a = 0; a < valid.size(); ++a) {
        const float lo = valid[a].first;
        float sum = 0.f;
        int   cnt = 0;
        for (size_t b = a; b < valid.size() && valid[b].first - lo <= bin; ++b) {
            sum += valid[b].first;
            ++cnt;
        }
        if (cnt > mode_cnt) { mode_cnt = cnt; mode_center = sum / static_cast<float>(cnt); }
    }

    // Within the modal range bin, prefer the candidate closest to the temporal
    // centre of the line — maximises bidirectional tracker coverage.
    int   best_i     = -1;
    float best_score = -1.f;
    for (int i = 0; i < n; ++i) {
        const auto& c = cands[static_cast<size_t>(i)];
        if (c.range_m <= 0.f || c.conf < kMinConf) continue;
        if (std::fabs(c.range_m - mode_center) > bin) continue;

        // Centrality bonus: row near the sequence midpoint gives the tracker the
        // most pings to fill in each direction.
        const float centrality = 1.f - std::fabs(2.f * i / std::max(1, n - 1) - 1.f);
        const float score = c.conf * (1.f + 0.25f * centrality);
        if (score > best_score) { best_score = score; best_i = i; }
    }

    // Fallback: if modal selection yields nothing, take the highest-confidence candidate.
    if (best_i < 0) {
        for (int i = 0; i < n; ++i) {
            if (cands[static_cast<size_t>(i)].range_m <= 0.f) continue;
            if (cands[static_cast<size_t>(i)].conf > best_score) {
                best_score = cands[static_cast<size_t>(i)].conf;
                best_i = i;
            }
        }
    }
    return best_i;
}

} // anonymous namespace

void SeabedAutoDetector::detectAllContinuityAware(std::vector<PingRow>& rows,
                                                    const SeabedAutoParams& p)
{
    const int n = static_cast<int>(rows.size());

    // ── Step 1: generate per-ping best candidates ─────────────────────────
    std::vector<TrackResult> cands(static_cast<size_t>(n));
    std::vector<bool>        is_manual(static_cast<size_t>(n), false);
    std::vector<float>       manual_range(static_cast<size_t>(n), -1.f);

    for (int i = 0; i < n; ++i) {
        if (rows[static_cast<size_t>(i)].seabed.is_manual) {
            is_manual[static_cast<size_t>(i)]    = true;
            manual_range[static_cast<size_t>(i)] = rows[static_cast<size_t>(i)].seabed.range_m;
            continue;
        }
        if (rows[static_cast<size_t>(i)].slant_range_m <= 0.f) continue;

        const PingRow& row = rows[static_cast<size_t>(i)];
        core::SidescanPing port_stub, stbd_stub;
        port_stub.slant_range_m = row.slant_range_m;
        stbd_stub.slant_range_m = row.slant_range_m;
        port_stub.samples.resize(row.port.size());
        stbd_stub.samples.resize(row.stbd.size());
        for (std::size_t j = 0; j < row.port.size(); ++j)
            port_stub.samples[j].amplitude = row.port[j];
        for (std::size_t j = 0; j < row.stbd.size(); ++j)
            stbd_stub.samples[j].amplitude = row.stbd[j];

        const ScanResult sb = scanPingBest(&stbd_stub, p);
        const ScanResult pt = scanPingBest(&port_stub, p);
        const ScanResult combined = combineChannels(sb, pt, p.channel_agree_m);
        cands[static_cast<size_t>(i)] = {combined.range_m, combined.confidence};
    }

    // ── Step 2: bidirectional continuity passes ───────────────────────────
    std::vector<TrackResult> fwd(static_cast<size_t>(n));
    std::vector<TrackResult> bwd(static_cast<size_t>(n));
    const int anchor = chooseAnchor(cands, is_manual, manual_range, p.max_delta_m);
    if (anchor >= 0) {
        const float anchor_range = is_manual[static_cast<size_t>(anchor)]
            ? manual_range[static_cast<size_t>(anchor)]
            : cands[static_cast<size_t>(anchor)].range_m;
        const float anchor_conf = is_manual[static_cast<size_t>(anchor)]
            ? 1.f
            : std::max(0.25f, cands[static_cast<size_t>(anchor)].conf);
        fwd[static_cast<size_t>(anchor)] = {anchor_range, anchor_conf};
        bwd[static_cast<size_t>(anchor)] = {anchor_range, anchor_conf};
        trackFromAnchor(fwd, cands, is_manual, manual_range, anchor, +1, p.max_delta_m);
        trackFromAnchor(bwd, cands, is_manual, manual_range, anchor, -1, p.max_delta_m);
    }

    // ── Step 3: merge forward + backward ─────────────────────────────────
    // When both passes agree within half the continuity gate, average them and
    // boost confidence.  When they disagree, prefer the higher-confidence pass
    // but penalise it.  Only one pass valid → accept with slight penalty.
    const float merge_gate = (p.max_delta_m > 0.f)
        ? std::max(1.f, p.max_delta_m * 0.5f)
        : std::numeric_limits<float>::infinity();

    for (int i = 0; i < n; ++i) {
        if (rows[static_cast<size_t>(i)].seabed.is_manual) continue;

        const TrackResult& f = fwd[static_cast<size_t>(i)];
        const TrackResult& b = bwd[static_cast<size_t>(i)];
        const bool f_ok = (f.range_m > 0.f);
        const bool b_ok = (b.range_m > 0.f);

        auto& sb = rows[static_cast<size_t>(i)].seabed;
        if (f_ok && b_ok) {
            if (std::fabs(f.range_m - b.range_m) <= merge_gate) {
                const float tw = f.conf + b.conf;
                sb.range_m    = (f.range_m * f.conf + b.range_m * b.conf) / tw;
                sb.confidence = std::min(1.f, tw * 0.55f);
                sb.detected   = true;
            } else {
                const TrackResult& w = (f.conf >= b.conf) ? f : b;
                sb.range_m    = w.range_m;
                sb.confidence = w.conf * 0.75f;
                sb.detected   = true;
            }
        } else if (f_ok) {
            sb.range_m    = f.range_m;
            sb.confidence = f.conf * 0.85f;
            sb.detected   = true;
        } else if (b_ok) {
            sb.range_m    = b.range_m;
            sb.confidence = b.conf * 0.85f;
            sb.detected   = true;
        } else {
            sb.range_m    = -1.f;
            sb.confidence = 0.f;
            sb.detected   = false;
        }
    }

    gapFill(rows);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────────────────────

float SeabedAutoDetector::detectOne(const core::SidescanPing* port_ping,
                                     const core::SidescanPing* stbd_ping,
                                     const SeabedAutoParams&   params,
                                     float*                    out_confidence)
{
    auto scan = [&](const core::SidescanPing* ping) -> ScanResult {
        switch (params.method) {
        case SeabedMethod::Threshold:       return scanPingThreshold  (ping, params);
        case SeabedMethod::FirstReturn:     return scanPingFirstReturn(ping, params);
        case SeabedMethod::ContinuityAware: return scanPingBest       (ping, params);
        }
        return scanPingThreshold(ping, params);
    };

    const ScanResult sb   = scan(stbd_ping);
    const ScanResult pt   = scan(port_ping);
    const ScanResult best = combineChannels(sb, pt, params.channel_agree_m);

    if (out_confidence) *out_confidence = best.confidence;
    return best.range_m;
}

void SeabedAutoDetector::detectAll(std::vector<PingRow>& rows,
                                    const SeabedAutoParams& params)
{
    if (params.method == SeabedMethod::ContinuityAware) {
        detectAllContinuityAware(rows, params);
        return;
    }

    for (auto& row : rows) {
        if (row.seabed.is_manual) continue;

        core::SidescanPing port_stub, stbd_stub;
        port_stub.slant_range_m = row.slant_range_m;
        stbd_stub.slant_range_m = row.slant_range_m;
        port_stub.samples.resize(row.port.size());
        stbd_stub.samples.resize(row.stbd.size());
        for (std::size_t i = 0; i < row.port.size(); ++i)
            port_stub.samples[i].amplitude = row.port[i];
        for (std::size_t i = 0; i < row.stbd.size(); ++i)
            stbd_stub.samples[i].amplitude = row.stbd[i];

        float conf = 0.f;
        const float r     = detectOne(&port_stub, &stbd_stub, params, &conf);
        row.seabed.range_m    = r;
        row.seabed.confidence = conf;
        row.seabed.detected   = (r > 0.f);
    }

    if (params.max_delta_m > 0.f)
        rejectOutliers(rows, params.max_delta_m);

    gapFill(rows);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Post-processing
// ─────────────────────────────────────────────────────────────────────────────

void SeabedAutoDetector::gapFill(std::vector<PingRow>& rows)
{
    const int n = static_cast<int>(rows.size());
    int prev_anchor = -1;
    for (int i = 0; i <= n; ++i) {
        const bool is_anchor = (i < n)
            && (rows[static_cast<size_t>(i)].seabed.detected
                || rows[static_cast<size_t>(i)].seabed.is_manual)
            && (rows[static_cast<size_t>(i)].seabed.range_m > 0.f);

        if (is_anchor || i == n) {
            if (prev_anchor >= 0 && i < n && i > prev_anchor + 1) {
                const float r0   = rows[static_cast<size_t>(prev_anchor)].seabed.range_m;
                const float r1   = rows[static_cast<size_t>(i)].seabed.range_m;
                const float span = static_cast<float>(i - prev_anchor);
                for (int j = prev_anchor + 1; j < i; ++j) {
                    if (!rows[static_cast<size_t>(j)].seabed.is_manual) {
                        const float t = static_cast<float>(j - prev_anchor) / span;
                        // detected stays false — callers can distinguish interpolated rows.
                        rows[static_cast<size_t>(j)].seabed.range_m = r0 + t * (r1 - r0);
                    }
                }
            }
            if (i < n) prev_anchor = i;
        }
    }
}

void SeabedAutoDetector::rejectOutliers(std::vector<PingRow>& rows,
                                         float max_delta_m)
{
    if (max_delta_m <= 0.f || rows.empty()) return;

    const int n = static_cast<int>(rows.size());
    constexpr int kRadius = 7;

    std::vector<float> orig(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        orig[static_cast<size_t>(i)] = rows[static_cast<size_t>(i)].seabed.range_m;

    std::vector<float> window;
    window.reserve(kRadius * 2 + 1);

    for (int i = 0; i < n; ++i) {
        if (rows[static_cast<size_t>(i)].seabed.is_manual
            || !rows[static_cast<size_t>(i)].seabed.detected) continue;

        window.clear();
        for (int j = std::max(0, i - kRadius); j <= std::min(n - 1, i + kRadius); ++j)
            if (orig[static_cast<size_t>(j)] > 0.f)
                window.push_back(orig[static_cast<size_t>(j)]);

        if (static_cast<int>(window.size()) < 3) continue;

        std::nth_element(window.begin(),
                         window.begin() + static_cast<std::ptrdiff_t>(window.size() / 2),
                         window.end());
        const float median = window[window.size() / 2];

        if (std::fabs(orig[static_cast<size_t>(i)] - median) > max_delta_m) {
            rows[static_cast<size_t>(i)].seabed.range_m    = -1.f;
            rows[static_cast<size_t>(i)].seabed.confidence = 0.f;
            rows[static_cast<size_t>(i)].seabed.detected   = false;
        }
    }
}

void SeabedAutoDetector::smooth(std::vector<PingRow>& rows, int radius)
{
    if (radius <= 0 || rows.empty()) return;

    const int n = static_cast<int>(rows.size());
    std::vector<float> smoothed(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        if (rows[static_cast<size_t>(i)].seabed.is_manual
            || !rows[static_cast<size_t>(i)].seabed.detected) {
            smoothed[static_cast<size_t>(i)] = rows[static_cast<size_t>(i)].seabed.range_m;
            continue;
        }
        float sum = 0.f;
        int   cnt = 0;
        for (int j = std::max(0, i - radius); j <= std::min(n - 1, i + radius); ++j) {
            if (rows[static_cast<size_t>(j)].seabed.range_m > 0.f) {
                sum += rows[static_cast<size_t>(j)].seabed.range_m;
                ++cnt;
            }
        }
        smoothed[static_cast<size_t>(i)] = (cnt > 0)
            ? sum / cnt
            : rows[static_cast<size_t>(i)].seabed.range_m;
    }

    for (int i = 0; i < n; ++i)
        if (!rows[static_cast<size_t>(i)].seabed.is_manual)
            rows[static_cast<size_t>(i)].seabed.range_m = smoothed[static_cast<size_t>(i)];
}

} // namespace dolphin::ui
