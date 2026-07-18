#pragma once

// Canonical, bounded line context for sidescan display amplitudes.
//
// Context-dependent operators (Variable AGC, beam normalization, ARN,
// destriping, and ML enhancement) cannot be run independently on a map-thinned
// survey and a waterfall window without producing different answers. This
// product learns one deterministic gain field from a bounded representative
// sample of the line, then both viewers apply that same field by ping identity /
// timestamp and normalized ground-range position.

#include "app/display/WaterfallParams.h"
#include "app/tasks/CancellationToken.h"
#include "core/ArtifactIndex.h"
#include "core/SidescanPing.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dolphin::ui::imaging {

inline constexpr size_t kSssAmplitudeContextBins = 256;

struct SssAmplitudeContextRow {
    uint64_t id            = 0;
    int64_t  timestamp_us  = 0;
    uint32_t ping_number   = 0;
    double   sort_key      = 0.0;
    std::array<float, kSssAmplitudeContextBins> gain{};
};

struct SssAmplitudeContext {
    uint64_t params_fingerprint = 0;
    float stretch_low  = 0.f;
    float stretch_high = 1.f;
    std::vector<SssAmplitudeContextRow> port;
    std::vector<SssAmplitudeContextRow> starboard;

    bool valid() const noexcept
    {
        return stretch_high > stretch_low
            && (!port.empty() || !starboard.empty());
    }
};

struct SssAmplitudeContextRequest {
    std::string store_path;
    std::string store_format;
    std::shared_ptr<const core::ArtifactIndex> artifact_index;
    std::string source_path;
    float frequency_hz = 0.f;
    WaterfallParams params;
};

// Stable processing-only fingerprint. Palette, opacity, manual display range,
// channel visibility, and slant-range geometry do not change this context.
uint64_t sssAmplitudeParamsFingerprint(const WaterfallParams& params) noexcept;

// Build from pings on which applyPerPingCalibration has already run. Exposed for
// focused regression tests; production callers normally use the repository.
std::shared_ptr<const SssAmplitudeContext>
buildSssAmplitudeContextFromCalibrated(
    const std::vector<core::SidescanPing>& calibrated,
    const WaterfallParams& params);

// Get or build the deterministic line context. The repository is process-wide,
// thread-safe, and bounded; a miss decodes at most 2,048 channel records with at
// most 256 retained samples each.
std::shared_ptr<const SssAmplitudeContext>
getOrBuildSssAmplitudeContext(const SssAmplitudeContextRequest& request,
                              const app::CancellationToken& cancel = {});

// Apply the canonical gain field in-place. Per-ping TVG/ARC/Global-AGC must have
// run first on native samples. This function is independent of viewer window or
// map quality tier.
void applySssAmplitudeContext(std::vector<core::SidescanPing>& pings,
                              const SssAmplitudeContext& context);

} // namespace dolphin::ui::imaging
