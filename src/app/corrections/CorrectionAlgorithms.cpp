#include "app/corrections/CorrectionAlgorithms.h"

#include "pipeline/SidescanRadiometryAlgorithms.h"

namespace dolphin::app::corrections {

bool applyTvg(std::vector<core::SidescanPing>& pings, const TvgParams& settings)
{
    return pipeline::radiometry::applyTvg(pings, settings);
}

bool canApplyArc(const core::SidescanPing& ping)
{
    return pipeline::radiometry::canApplyArc(ping);
}

bool applyArc(std::vector<core::SidescanPing>& pings, const ArcParams& settings)
{
    return pipeline::radiometry::applyArc(pings, settings);
}

bool normalizeAmplitudes(std::vector<core::SidescanPing>& pings,
                         const AgcParams& settings)
{
    return pipeline::radiometry::applyAgc(pings, settings);
}

} // namespace dolphin::app::corrections
