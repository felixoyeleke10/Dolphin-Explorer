#include "pipeline/nodes/correction/SlantRangeNode.h"

#include "core/SidescanPing.h"
#include "ui/features/waterfall/rendering/WaterfallRangeGeometry.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace {

dolphin::core::SidescanPing run(
    dolphin::core::SidescanPing ping,
    const dolphin::pipeline::NodeParams& params = {})
{
    dolphin::pipeline::SlantRangeNode node;
    dolphin::pipeline::ArtifactBuffer input;
    input.emplace_back(std::move(ping));
    auto output = node.process(input, params);
    return std::get<dolphin::core::SidescanPing>(std::move(output.front()));
}

} // namespace

int main()
{
    using namespace dolphin;

    pipeline::SlantRangeNode node;
    assert(node.schema().params.count("auto_detect_bottom") == 1);

    core::SidescanPing ping;
    ping.nav.altitude_m = 3.0f;
    ping.samples = {{1, -1.0f}, {1, 3.0f}, {1, 5.0f}};
    auto corrected = run(ping);
    assert(corrected.samples[0].range_m == -1.0f);
    assert(corrected.samples[1].range_m == 0.0f);
    assert(std::abs(corrected.samples[2].range_m - 4.0f) < 1e-6f);
    assert(core::hasCorrectionFlag(corrected.correction_flags,
                                   core::CorrectionFlag::SlantRange));

    const auto twice = run(corrected);
    assert(twice.samples[2].range_m == corrected.samples[2].range_m);

    core::SidescanPing picked;
    picked.nav.altitude_m = 2.0f;
    picked.bottom_pick = {6.0f, 0.9f, 2};
    picked.samples = {{1, 10.0f}};
    const auto pick_corrected = run(picked);
    assert(std::abs(pick_corrected.samples[0].range_m - 8.0f) < 1e-6f);

    // Bottom must persist for kPersistSamples (3) consecutive samples above
    // threshold to be accepted — the true return here starts at index 2
    // (900, 950, 1000) and is detected at its onset (index 2), not later.
    core::SidescanPing estimated_altitude;
    estimated_altitude.slant_range_m = 20.0f;
    estimated_altitude.samples = {
        {10, 1.0f}, {20, 2.0f}, {900, 3.0f}, {950, 4.0f}, {1000, 5.0f},
        {20, 6.0f}, {20, 7.0f}, {20, 8.0f}, {20, 9.0f}, {20, 14.0f}};
    const auto estimated = run(estimated_altitude);
    assert(estimated.bottom_pick.source == 1);
    assert(estimated.bottom_pick.range_m == 3.0f);
    assert(core::hasCorrectionFlag(estimated.correction_flags,
                                   core::CorrectionFlag::SlantRange));
    assert(estimated.samples[0].range_m < 0.0f);
    assert(estimated.samples[1].range_m < 0.0f);
    assert(estimated.samples[2].range_m == 0.0f);
    assert(std::abs(estimated.samples[9].range_m
                    - std::sqrt(14.0f * 14.0f - 3.0f * 3.0f)) < 1e-5f);

    const pipeline::NodeParams disabled{{"auto_detect_bottom", false}};
    const auto unchanged = run(estimated_altitude, disabled);
    assert(unchanged.samples[2].range_m == 3.0f);
    assert(!core::hasCorrectionFlag(unchanged.correction_flags,
                                    core::CorrectionFlag::SlantRange));

    // An isolated single-sample spike ahead of the true (persistent) bottom
    // must not be picked — the old first-crossing detector would have
    // latched onto index 1 (a noise spike / multipath return / biological
    // scatter), producing a badly wrong altitude.
    core::SidescanPing spiky;
    spiky.slant_range_m = 20.0f;
    spiky.samples = {
        {10, 1.0f}, {1000, 2.0f}, {10, 3.0f}, {10, 4.0f}, {900, 5.0f},
        {950, 6.0f}, {1000, 7.0f}, {20, 8.0f}, {20, 9.0f}, {20, 10.0f}};
    const auto spike_rejected = run(spiky);
    assert(spike_rejected.bottom_pick.source == 1);
    assert(spike_rejected.bottom_pick.range_m == 5.0f);  // true bottom, not the spike at index 1

    // Confidence reflects contrast against the local background, not merely
    // proximity to the window's own peak (which the old formula guaranteed
    // to be >= threshold regardless of how noisy the background was).
    core::SidescanPing noisy_background;
    noisy_background.slant_range_m = 20.0f;
    noisy_background.samples = {
        {640, 1.0f}, {640, 2.0f}, {900, 3.0f}, {950, 4.0f}, {1000, 5.0f},
        {20, 6.0f}, {20, 7.0f}, {20, 8.0f}};
    const auto low_confidence = run(noisy_background);
    assert(low_confidence.bottom_pick.confidence < 0.5f);
    assert(!core::hasCorrectionFlag(low_confidence.correction_flags,
                                    core::CorrectionFlag::SlantRange));
    assert(low_confidence.samples[0].range_m == 1.0f);

    core::SidescanPing clean_background;
    clean_background.slant_range_m = 20.0f;
    clean_background.samples = {
        {10, 1.0f}, {10, 2.0f}, {900, 3.0f}, {950, 4.0f}, {1000, 5.0f},
        {20, 6.0f}, {20, 7.0f}, {20, 8.0f}};
    const auto high_confidence = run(clean_background);
    assert(high_confidence.bottom_pick.confidence > 0.9f);
    assert(core::hasCorrectionFlag(high_confidence.correction_flags,
                                   core::CorrectionFlag::SlantRange));

    // A ping with no sample beyond the altitude has no ground-range imagery
    // and must not claim successful SRC provenance.
    core::SidescanPing no_seabed_samples;
    no_seabed_samples.nav.altitude_m = 5.0f;
    no_seabed_samples.samples = {{1, 1.0f}, {1, 5.0f}};
    const auto no_seabed = run(no_seabed_samples);
    assert(no_seabed.samples[0].range_m == 1.0f);
    assert(no_seabed.samples[1].range_m == 5.0f);
    assert(!core::hasCorrectionFlag(no_seabed.correction_flags,
                                    core::CorrectionFlag::SlantRange));

    // Shared waterfall geometry must preserve nonlinear vendor range tables
    // and independent port/starboard altitude references.
    ui::PingRow row;
    row.slant_range_m = 100.f;
    row.port_altitude_m = 10.f;
    row.stbd_altitude_m = 20.f;
    row.port_ranges = {2.f, 7.f, 25.f, 60.f, 100.f};
    assert(ui::waterfallSideAltitude(row, core::SidescanChannel::Port) == 10.f);
    assert(ui::waterfallSideAltitude(row, core::SidescanChannel::Starboard) == 20.f);
    const float sample = ui::waterfallSampleForRange(
        row.port_ranges, 5, 16.f, row.slant_range_m);
    assert(std::abs(sample - 1.5f) < 1e-6f);
    assert(std::abs(ui::waterfallRangeAtSample(
        row.port_ranges, 5, sample, row.slant_range_m) - 16.f) < 1e-6f);

    core::SidescanPing invalid;
    invalid.nav.altitude_m = 3.0f;
    invalid.samples = {{1, std::numeric_limits<float>::quiet_NaN()}};
    const auto invalid_result = run(invalid);
    assert(std::isnan(invalid_result.samples[0].range_m));
    assert(!core::hasCorrectionFlag(invalid_result.correction_flags,
                                    core::CorrectionFlag::SlantRange));

    std::cout << "Slant-range correction tests passed\n";
    return 0;
}
