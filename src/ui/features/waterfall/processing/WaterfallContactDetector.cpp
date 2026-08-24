#include "ui/features/waterfall/processing/WaterfallContactDetector.h"

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

std::vector<WfContact> WaterfallContactDetector::detect(
    const std::vector<PingRow>& rows,
    const std::vector<WfContact>& existing,
    ContactClass classification,
    int sensitivity,
    int max_candidates)
{
    const float ratio_threshold = sensitivity <= 0 ? 2.2f
                                : sensitivity == 1 ? 1.8f : 1.5f;
    const int row_gap = sensitivity <= 0 ? 10 : sensitivity == 1 ? 7 : 5;
    std::vector<WfContact> candidates;
    if (max_candidates <= 0) return candidates;
    candidates.reserve(static_cast<size_t>(max_candidates));
    int last_row[2] = {-1000, -1000};

    const auto duplicate = [&](int row, core::SidescanChannel channel, float range_m) {
        const float tolerance = std::max(1.f, range_m * 0.05f);
        const auto matches = [=](const WfContact& contact) {
            return contact.ch == channel
                && std::abs(contact.row_idx - row) <= 2
                && std::abs(contact.range_m - range_m) <= tolerance;
        };
        return std::any_of(existing.cbegin(), existing.cend(), matches)
            || std::any_of(candidates.cbegin(), candidates.cend(), matches);
    };

    for (int row = 0; row < static_cast<int>(rows.size())
            && static_cast<int>(candidates.size()) < max_candidates; ++row) {
        const auto& ping = rows[static_cast<size_t>(row)];
        for (int side = 0; side < 2
                && static_cast<int>(candidates.size()) < max_candidates; ++side) {
            if (row - last_row[side] < row_gap) continue;
            const auto& samples = side == 0 ? ping.port : ping.stbd;
            const auto& ranges = side == 0 ? ping.port_ranges : ping.stbd_ranges;
            if (samples.size() < 24) continue;

            int best_sample = -1;
            float best_score = ratio_threshold;
            for (int i = 10; i + 10 < static_cast<int>(samples.size()); ++i) {
                const float target = static_cast<float>(samples[static_cast<size_t>(i)]);
                if (target < 1500.f
                    || target < samples[static_cast<size_t>(i - 1)]
                    || target < samples[static_cast<size_t>(i + 1)]) continue;
                float context = 0.f;
                float shadow = 0.f;
                for (int k = 4; k <= 9; ++k) context += samples[static_cast<size_t>(i - k)];
                for (int k = 2; k <= 7; ++k) shadow += samples[static_cast<size_t>(i + k)];
                context /= 6.f;
                shadow /= 6.f;
                if (context < 1.f || shadow > context * 0.9f) continue;
                const float score = target / context;
                if (score > best_score) {
                    best_score = score;
                    best_sample = i;
                }
            }
            if (best_sample < 0) continue;

            float range_m = 0.f;
            if (ranges.size() == samples.size())
                range_m = ranges[static_cast<size_t>(best_sample)];
            else if (ping.slant_range_m > 0.f)
                range_m = ping.slant_range_m * best_sample
                        / static_cast<float>(samples.size() - 1);
            const auto channel = side == 0 ? core::SidescanChannel::Port
                                           : core::SidescanChannel::Starboard;
            if (range_m <= 0.f) continue;
            if (duplicate(row, channel, range_m)) {
                last_row[side] = row;
                continue;
            }

            candidates.push_back({row, channel, range_m, classification});
            last_row[side] = row;
        }
    }
    return candidates;
}

} // namespace dolphin::ui
