#include "ui/features/map/sidescan/SidescanEntryFilter.h"
#include "app/layers/LayerUtils.h"
#include "core/Artifact.h"

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

std::vector<core::ArtifactIndexEntry>
thinSidescanEntriesForMap(const core::ArtifactIndex& index, size_t max_ping_groups)
{
    std::vector<core::ArtifactIndexEntry> sidescan_entries;
    sidescan_entries.reserve(index.entries.size());
    for (const auto& entry : index.entries) {
        if (entry.type == core::ArtifactType::Sidescan)
            sidescan_entries.push_back(entry);
    }

    if (sidescan_entries.empty() || max_ping_groups == 0)
        return sidescan_entries;

    struct GroupSpan {
        size_t begin = 0;
        size_t end   = 0;
    };

    std::vector<GroupSpan> groups;
    groups.reserve(sidescan_entries.size());

    // Group by ping_number (XTF) or timestamp (JSF) so port/starboard stay together.
    const bool use_pn = !sidescan_entries.empty()
                        && sidescan_entries.front().ping_number != 0;

    size_t begin = 0;
    while (begin < sidescan_entries.size()) {
        size_t end = begin + 1;
        if (use_pn) {
            const uint32_t pn = sidescan_entries[begin].ping_number;
            while (end < sidescan_entries.size()
                   && sidescan_entries[end].ping_number == pn) {
                ++end;
            }
        } else {
            const int64_t ts_us = sidescan_entries[begin].timestamp_us;
            while (end < sidescan_entries.size()
                   && sidescan_entries[end].timestamp_us == ts_us) {
                ++end;
            }
        }
        groups.push_back({begin, end});
        begin = end;
    }

    if (groups.size() <= max_ping_groups)
        return sidescan_entries;

    const size_t step =
        std::max<size_t>(1, (groups.size() + max_ping_groups - 1) / max_ping_groups);

    std::vector<core::ArtifactIndexEntry> thinned;
    thinned.reserve(std::min(sidescan_entries.size(), max_ping_groups * size_t{2}));

    for (size_t gi = 0; gi < groups.size(); gi += step) {
        const auto& span = groups[gi];
        thinned.insert(thinned.end(),
                       sidescan_entries.begin() + span.begin,
                       sidescan_entries.begin() + span.end);
    }

    const auto& last = groups.back();
    const bool has_last = !thinned.empty()
        && thinned.back().artifact_id
           == sidescan_entries[last.end - 1].artifact_id;
    if (!has_last) {
        thinned.insert(thinned.end(),
                       sidescan_entries.begin() + last.begin,
                       sidescan_entries.begin() + last.end);
    }

    return thinned;
}

void filterSidescanEntriesByBand(core::ArtifactIndex& index, float target_hz)
{
    if (target_hz <= 0.f) return;
    const auto bands = app::sidescanFrequencyBands(index);
    if (bands.size() < 2) return;
    const float target = app::nearestFrequencyBand(bands, target_hz);
    index.entries.erase(
        std::remove_if(index.entries.begin(), index.entries.end(),
            [target](const core::ArtifactIndexEntry& e) {
                return e.type == core::ArtifactType::Sidescan
                    && e.frequency_hz > 0.f
                    && std::fabs(e.frequency_hz - target) >= 1.f;
            }),
        index.entries.end());
}

} // namespace dolphin::ui
