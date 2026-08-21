#pragma once

#include "core/SidescanPing.h"
#include "core/SubBottomTrace.h"
#include "pipeline/IProcessingNode.h"

#include <cstdint>

namespace dolphin::app::contracts {

struct ProcessingProvenance {
    uint32_t baked_correction_flags = 0;
    bool slant_range_corrected = false;
};

// Derive provenance from persisted output, not from UI intent or prior layer
// state. This is the single truth used by history, revert, 2D and 3D.
inline ProcessingProvenance deriveProcessingProvenance(
    const pipeline::ArtifactBuffer& artifacts) noexcept
{
    ProcessingProvenance result;
    for (const auto& artifact : artifacts) {
        if (const auto* ping = std::get_if<core::SidescanPing>(&artifact)) {
            result.baked_correction_flags |= ping->correction_flags;
            result.slant_range_corrected |= core::hasCorrectionFlag(
                ping->correction_flags, core::CorrectionFlag::SlantRange);
        } else if (const auto* trace = std::get_if<core::SubBottomTrace>(&artifact)) {
            // DataLayer::baked_correction_flags is modality-specific. Preserve
            // the SBP trace provenance through the same persistence path used
            // by SSS instead of silently reporting an unprocessed layer.
            result.baked_correction_flags |= trace->correction_flags;
        }
    }
    return result;
}

} // namespace dolphin::app::contracts
