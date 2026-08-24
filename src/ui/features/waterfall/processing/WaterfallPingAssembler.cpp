// WaterfallPingAssembler.cpp — raw SidescanPing → PingRow pairing
//
// Extracted from WaterfallView::setPings().  Pure data transformation;
// no Qt widget dependency.

#include "ui/features/waterfall/processing/WaterfallPingAssembler.h"
#include "render/sonar/SSSAmplitudeProcessor.h"
#include "core/SidescanGeometry.h"

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Public API
// -----------------------------------------------------------------------------

std::vector<PingRow> WaterfallPingAssembler::assemble(
    const std::vector<core::SidescanPing>& pings,
    [[maybe_unused]] const WaterfallParams& params)
{
    std::vector<PingRow> rows;
    rows.reserve(pings.size() / 2 + 1);

    // Separate channels; sort both by timestamp.
    std::vector<const core::SidescanPing*> ports, stbds;
    ports.reserve(pings.size());
    stbds.reserve(pings.size());

    for (const auto& p : pings) {
        if (p.channel == core::SidescanChannel::Port) ports.push_back(&p);
        else                                           stbds.push_back(&p);
    }

    auto byTime = [](const core::SidescanPing* a, const core::SidescanPing* b) {
        return a->timestamp_us < b->timestamp_us;
    };
    std::sort(ports.begin(), ports.end(), byTime);
    std::sort(stbds.begin(), stbds.end(), byTime);

    auto push = [&](const core::SidescanPing* pp, const core::SidescanPing* sp) {
        rows.push_back(buildRow(pp, sp));
    };

    if (ports.empty() || stbds.empty()) {
        // Single-channel data — no pairing needed.
        for (auto* p : ports) push(p, nullptr);
        for (auto* s : stbds) push(nullptr, s);
        return rows;
    }

    // Prefer ping_number matching only when BOTH sides carry nonzero ping numbers.
    // If only one side has them the numbers won't match and every ping emits single-sided.
    const bool use_ping_number =
        (ports.front()->ping_number != 0 && stbds.front()->ping_number != 0);

    if (use_ping_number) {
        size_t pi = 0, si = 0;
        while (pi < ports.size() && si < stbds.size()) {
            const uint32_t pn = ports[pi]->ping_number;
            const uint32_t sn = stbds[si]->ping_number;
            if      (pn == sn) push(ports[pi++], stbds[si++]);
            else if (pn <  sn) push(ports[pi++], nullptr);
            else               push(nullptr,     stbds[si++]);
        }
        while (pi < ports.size()) push(ports[pi++], nullptr);
        while (si < stbds.size()) push(nullptr,     stbds[si++]);
    } else {
        // Timestamp-based merging: treat pings within kTolUs as a pair.
        size_t pi = 0, si = 0;
        while (pi < ports.size() && si < stbds.size()) {
            const int64_t dt = std::abs(
                ports[pi]->timestamp_us - stbds[si]->timestamp_us);
            if (dt <= kTolUs)
                push(ports[pi++], stbds[si++]);
            else if (ports[pi]->timestamp_us < stbds[si]->timestamp_us)
                push(ports[pi++], nullptr);
            else
                push(nullptr, stbds[si++]);
        }
        while (pi < ports.size()) push(ports[pi++], nullptr);
        while (si < stbds.size()) push(nullptr,     stbds[si++]);
    }

    return rows;
}

// -----------------------------------------------------------------------------
//  Private: build a single PingRow from an optional (port, stbd) pair
// -----------------------------------------------------------------------------

PingRow WaterfallPingAssembler::buildRow(const core::SidescanPing* pp,
                                          const core::SidescanPing* sp)
{
    PingRow row;
    float max_range = 0.f;
    bool  nav_set   = false;

    auto fill = [&](const core::SidescanPing* ping,
                    std::vector<uint16_t>& out, std::vector<float>& out_ranges,
                    float& side_altitude, core::SidescanRangeDomain& range_domain,
                    SeabedDetectionResult& side_seabed,
                    int64_t& side_timestamp, std::uint64_t& side_artifact_id) {
        if (!ping) return;
        side_timestamp = ping->timestamp_us;
        side_artifact_id = ping->id;
        range_domain = core::hasCorrectionFlag(
            ping->correction_flags, core::CorrectionFlag::SlantRange)
            ? core::SidescanRangeDomain::Ground
            : core::SidescanRangeDomain::Slant;
        side_altitude = static_cast<float>(
            core::sidescanCorrectionAltitudeMetres(*ping).value_or(0.0));
        if (std::isfinite(ping->slant_range_m))
            max_range = std::max(max_range, ping->slant_range_m);
        if (!row.timestamp_us) row.timestamp_us = ping->timestamp_us;
        if (!row.artifact_id)  row.artifact_id  = static_cast<int64_t>(ping->id);
        if (!nav_set && ping->nav.valid) {
            row.lat          = ping->nav.lat;
            row.lon          = ping->nav.lon;
            row.is_projected = ping->nav.is_projected;
            row.heading_deg  = ping->nav.heading_deg;
            row.altitude_m   = ping->nav.altitude_m;
            nav_set          = true;
        }
        out.resize(ping->samples.size());
        bool any_range = false;
        for (size_t i = 0; i < ping->samples.size(); ++i) {
            out[i] = SSSAmplitudeProcessor::physical16(
                ping->samples[i].amplitude,
                ping->samples[i].range_m);
            if (ping->samples[i].range_m > 0.f) any_range = true;
        }
        if (any_range) {
            out_ranges.resize(ping->samples.size());
            for (size_t i = 0; i < ping->samples.size(); ++i)
                out_ranges[i] = ping->samples[i].range_m;
        }

        // Transfer pipeline seabed detection.  When both channels carry a pick,
        // keep the one with higher confidence.  User-edited picks (source == 2)
        // are marked manual so SeabedAutoDetector::detectAll leaves them alone;
        // pipeline auto-detected picks (source == 1) will still be overwritten
        // by the UI detector, but a manual pick from either channel always wins.
        if (ping->bottom_pick.valid()) {
            const bool manual  = (ping->bottom_pick.source == 2);
            side_seabed = {ping->bottom_pick.range_m,
                           ping->bottom_pick.confidence, true, manual};
            const bool replace = !row.seabed.detected
                              || ping->bottom_pick.confidence > row.seabed.confidence
                              || (manual && !row.seabed.is_manual);
            if (replace) {
                row.seabed.range_m    = ping->bottom_pick.range_m;
                row.seabed.confidence = ping->bottom_pick.confidence;
                row.seabed.detected   = true;
                row.seabed.is_manual  = manual;
                row.seabed_domain = range_domain;
            }
        }
    };

    fill(pp, row.port, row.port_ranges, row.port_altitude_m,
         row.port_range_domain, row.port_seabed, row.port_timestamp_us,
         row.port_artifact_id);
    fill(sp, row.stbd, row.stbd_ranges, row.stbd_altitude_m,
         row.stbd_range_domain, row.stbd_seabed, row.stbd_timestamp_us,
         row.stbd_artifact_id);
    row.slant_range_m = max_range;
    return row;
}

// -----------------------------------------------------------------------------
//  Input contract gate
// -----------------------------------------------------------------------------

int WaterfallPingAssembler::sanitize(std::vector<core::SidescanPing>& pings)
{
    const int before = static_cast<int>(pings.size());
    pings.erase(
        std::remove_if(pings.begin(), pings.end(),
            [](const core::SidescanPing& p) {
                return p.samples.size() < 2
                    || !std::isfinite(p.slant_range_m)
                    || !(p.slant_range_m > 0.f);
            }),
        pings.end());
    for (auto& p : pings) {
        if (!std::isfinite(p.blanking_m)) p.blanking_m = 0.f;
        for (auto& s : p.samples)
            if (!std::isfinite(s.range_m))
                s.range_m = 0.f;  // keep negative (masked/water-column); only clear NaN/Inf
    }
    return before - static_cast<int>(pings.size());
}

} // namespace dolphin::ui
