#pragma once

#include "ui/features/map/sidescan/SssContinuity.h"
#include "ui/features/map/sidescan/SssGeorefParams.h"

namespace dolphin::ui::sssnavcontinuity {

bool compatibleFrames(const CorrectedSssNav& a, const CorrectedSssNav& b);
double distanceMetres(const CorrectedSssNav& a, const CorrectedSssNav& b);
double headingBetween(const CorrectedSssNav& from, const CorrectedSssNav& to);
bool samePingCycle(const core::SidescanPing& a, const core::SidescanPing& b);
bool positionsCoincide(const CorrectedSssNav& a, const CorrectedSssNav& b);
double interpolateLongitude(double left, double right, double alpha,
                            bool is_projected);

ssscontinuity::Thresholds deriveThresholds(
    const std::vector<core::SidescanPing>& pings,
    const std::vector<size_t>& order,
    const std::vector<CorrectedSssNav>& positions);

bool isContinuousPair(const CorrectedSssNav& a,
                      const CorrectedSssNav& b,
                      const core::SidescanPing& a_ping,
                      const core::SidescanPing& b_ping,
                      const ssscontinuity::Thresholds& thresholds);

} // namespace dolphin::ui::sssnavcontinuity
