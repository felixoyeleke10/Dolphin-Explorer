#include "app/layers/LayerUtils.h"
#include "app/layers/DataLayer.h"

#include <algorithm>
#include <cmath>

namespace dolphin::app {

std::string modalityLabel(Modality m)
{
    switch (m) {
    case Modality::Sidescan:     return "Sidescan";
    case Modality::SubBottom:    return "Sub-bottom";
    case Modality::Magnetometer: return "Magnetometer";
    case Modality::Multibeam:    return "Multibeam";
    case Modality::Raster:       return "Raster";
    case Modality::Mixed:        return "Mixed";
    default:                     return "Unknown";
    }
}

Modality inferModality(const core::ArtifactIndex& idx)
{
    const bool has_sss  = !idx.byType(core::ArtifactType::Sidescan).empty();
    const bool has_sbp  = !idx.byType(core::ArtifactType::SubBottom).empty();
    const bool has_mag  = !idx.byType(core::ArtifactType::Magnetometer).empty();
    const bool has_mbes = !idx.byType(core::ArtifactType::Multibeam).empty();
    const bool has_rast = !idx.byType(core::ArtifactType::Raster).empty();

    const int count = (has_sss?1:0) + (has_sbp?1:0) + (has_mag?1:0)
                    + (has_mbes?1:0) + (has_rast?1:0);
    // Mixed is intentionally transient: callers (ImportService, Project::fromJson)
    // split Mixed SSS+SBP sources into two single-modality layers immediately.
    if (count > 1)  return Modality::Mixed;
    if (has_sss)    return Modality::Sidescan;
    if (has_sbp)    return Modality::SubBottom;
    if (has_mag)    return Modality::Magnetometer;
    if (has_mbes)   return Modality::Multibeam;
    if (has_rast)   return Modality::Raster;
    return Modality::Unknown;
}

core::ArtifactIndex filteredByType(const core::ArtifactIndex& src,
                                   core::ArtifactType          type)
{
    core::ArtifactIndex out;
    out.source_id = src.source_id;
    for (const auto& e : src.entries)
        if (e.type == type)
            out.entries.push_back(e);
    return out;
}

Modality modalityForType(core::ArtifactType type)
{
    switch (type) {
    case core::ArtifactType::Sidescan:     return Modality::Sidescan;
    case core::ArtifactType::SubBottom:    return Modality::SubBottom;
    case core::ArtifactType::Magnetometer: return Modality::Magnetometer;
    case core::ArtifactType::Multibeam:    return Modality::Multibeam;
    case core::ArtifactType::Raster:       return Modality::Raster;
    default:                               return Modality::Unknown;
    }
}

core::ArtifactType artifactTypeForModality(Modality m)
{
    switch (m) {
    case Modality::Sidescan:     return core::ArtifactType::Sidescan;
    case Modality::SubBottom:    return core::ArtifactType::SubBottom;
    case Modality::Magnetometer: return core::ArtifactType::Magnetometer;
    case Modality::Multibeam:    return core::ArtifactType::Multibeam;
    default:                     return core::ArtifactType::Sidescan;
    }
}

const char* moduleSuffix(core::ArtifactType type)
{
    switch (type) {
    case core::ArtifactType::Sidescan:     return "[SSS]";
    case core::ArtifactType::SubBottom:    return "[SBP]";
    case core::ArtifactType::Magnetometer: return "[MAG]";
    case core::ArtifactType::Multibeam:    return "[MBES]";
    default:                               return "[?]";
    }
}

std::vector<float> sidescanFrequencyBands(const core::ArtifactIndex& index)
{
    std::vector<float> bands;
    for (const auto& e : index.entries) {
        if (e.type != core::ArtifactType::Sidescan || e.frequency_hz <= 0.f) continue;
        bool found = false;
        for (float b : bands)
            if (std::fabs(b - e.frequency_hz) < 1.f) { found = true; break; }
        if (!found) bands.push_back(e.frequency_hz);
    }
    std::sort(bands.begin(), bands.end(), std::greater<float>());
    return bands;
}

float nearestFrequencyBand(const std::vector<float>& bands, float target_hz)
{
    if (bands.empty()) return 0.f;
    if (target_hz <= 0.f) return bands.front();
    float best = bands.front();
    for (float b : bands)
        if (std::fabs(b - target_hz) < std::fabs(best - target_hz))
            best = b;
    return best;
}

} // namespace dolphin::app
