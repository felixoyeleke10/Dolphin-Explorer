#pragma once
#include "ui/features/waterfall/PingRow.h"
#include "core/SidescanPing.h"

#include <climits>
#include <vector>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Detection parameters
// -----------------------------------------------------------------------------

enum class SeabedMethod  { Threshold = 0, FirstReturn = 1 };

struct SeabedAutoParams {
    SeabedMethod  method        = SeabedMethod::Threshold;

    // Blanking: physical distance takes priority over percentage.
    // Set blanking_m > 0 to specify metres; otherwise blanking_pct is used.
    float blanking_m    = 0.f;    // metres from nadir to skip (0 = use blanking_pct)
    float blanking_pct  = 10.f;   // % of swath samples to skip when blanking_m == 0

    float threshold_pct = 17.f;   // Threshold method: % of ping peak required for a hit
    float min_snr       = 3.f;    // FirstReturn: min amplitude / noise-floor ratio

    float channel_agree_m = 3.f;  // max port/stbd range disagreement before falling back to
                                   // the stronger channel (single-ping agreement, not continuity)
    float max_delta_m   = 5.f;    // max ping-to-ping range change for continuity gate and
                                   // outlier rejection; set 0 to disable (0 = off)
    float smoothing     = 0.f;    // moving-average half-window over rows (0 = off)
};

// -----------------------------------------------------------------------------
//  SeabedAutoDetector — stateless seabed auto-detection.
//
//  detectOne()       — single ping pair → seabed slant-range (or -1.f)
//  detectAll()       — batch fill; skips rows with is_manual set
//  rejectOutliers()  — removes picks that jump farther than max_delta_m from
//                      the local median (run before smooth)
//  smooth()          — moving-average pass on the detected line
// -----------------------------------------------------------------------------

class SeabedAutoDetector {
public:
    // Detect the seabed slant range for one ping pair.
    // Returns -1.f if detection fails for both channels.
    // out_confidence (optional): filled with 0–1 quality estimate.
    static float detectOne(const core::SidescanPing* port_ping,
                           const core::SidescanPing* stbd_ping,
                           const SeabedAutoParams&   params = {},
                           float*                    out_confidence = nullptr);

    // Batch: re-detect seabed for every row that is not a manual edit.
    // Builds transient SidescanPing stubs from each row's uint16 amplitude arrays.
    static void detectAll(std::vector<PingRow>& rows,
                          const SeabedAutoParams& params = {});

    // Outlier rejection: marks auto-detected picks that deviate from their
    // local median by more than max_delta_m as undetected.
    static void rejectOutliers(std::vector<PingRow>& rows, float max_delta_m);

    // Gap-fill: linearly interpolates between detected/manual anchors for rows
    // that have range_m <= 0, up to max_gap rows per span (INT_MAX = unlimited).
    // Filled rows are marked detected with endpoint-bounded confidence so the
    // overlay and SLR geometry consume the same continuous result. Gaps wider
    // than max_gap remain empty rather than creating a misleading long bridge.
    static void gapFill(std::vector<PingRow>& rows, int max_gap = INT_MAX);

    // Moving-average smoothing over a half-window of 'radius' rows.
    // Manual rows are not modified but contribute to neighbours' average.
    static void smooth(std::vector<PingRow>& rows, int radius);

private:
    struct ScanResult { float range_m = -1.f; float confidence = 0.f; };

    static ScanResult scanPingThreshold  (const core::SidescanPing* ping, const SeabedAutoParams& p);
    static ScanResult scanPingFirstReturn(const core::SidescanPing* ping, const SeabedAutoParams& p);
    static ScanResult scanPingGradient   (const core::SidescanPing* ping, const SeabedAutoParams& p);
    static ScanResult scanPingHeuristic  (const core::SidescanPing* ping, const SeabedAutoParams& p);
    // Runs all per-ping scanners; returns confidence-weighted best candidate.
    static ScanResult scanPingBest       (const core::SidescanPing* ping, const SeabedAutoParams& p);

    // Combine port/starboard ScanResults using channel_agree_m; shared by detectOne
    // and the continuity-aware tracker so both use the same agreement parameter.
    static ScanResult combineChannels(const ScanResult& sb, const ScanResult& pt,
                                      float channel_agree_m);

    // Blanking zone in samples, honouring blanking_m when > 0.
    static int   blankingSamples  (const core::SidescanPing* ping, const SeabedAutoParams& p);
    // Slant range for sample i (per-sample range_m when available, else linear).
    static float rangeAt          (const core::SidescanPing* ping, int i);
    // Noise floor from post-blanking water-column region: 15th-percentile of window_n samples.
    static float estimateNoiseFloor(const core::SidescanPing* ping, int skip, int window_n);
};

} // namespace dolphin::ui
