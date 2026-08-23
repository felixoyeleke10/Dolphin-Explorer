#pragma once

#include "ui/features/map/MapTypes.h"
#include <cstdint>

namespace dolphin::ui {

enum class StagedRefreshStep : uint8_t {
    Ignore,
    ShowPreviewThenBuildTarget,
    ShowFinal,
    BuildTargetAfterPreviewFailure,
    FinalFailed
};

struct SidescanStagedRefresh {
    uint64_t generation = 0;
    MapSonarQuality target = MapSonarQuality::CoverageOnly;
    MapSonarQuality preview = MapSonarQuality::Low;
    bool awaiting_preview = false;
};

inline StagedRefreshStep acceptCompletedTier(
    SidescanStagedRefresh& refresh, uint64_t generation, MapSonarQuality quality)
{
    if (refresh.generation != generation) return StagedRefreshStep::Ignore;
    if (refresh.awaiting_preview && quality == refresh.preview) {
        refresh.awaiting_preview = false;
        return StagedRefreshStep::ShowPreviewThenBuildTarget;
    }
    if (!refresh.awaiting_preview && quality == refresh.target)
        return StagedRefreshStep::ShowFinal;
    return StagedRefreshStep::Ignore;
}

inline StagedRefreshStep acceptFailedTier(
    SidescanStagedRefresh& refresh, uint64_t generation, MapSonarQuality quality)
{
    if (refresh.generation != generation) return StagedRefreshStep::Ignore;
    if (refresh.awaiting_preview && quality == refresh.preview) {
        refresh.awaiting_preview = false;
        return StagedRefreshStep::BuildTargetAfterPreviewFailure;
    }
    if (!refresh.awaiting_preview && quality == refresh.target)
        return StagedRefreshStep::FinalFailed;
    return StagedRefreshStep::Ignore;
}

} // namespace dolphin::ui
