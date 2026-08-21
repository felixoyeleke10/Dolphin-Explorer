#include "pipeline/SidescanEnhancementAlgorithms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace dolphin::pipeline::enhancement {
namespace {

int maxRowLength(const AmplitudeRows& rows)
{
    size_t result = 0;
    for (const auto* row : rows) if (row) result = std::max(result, row->size());
    return static_cast<int>(result);
}

float medianInPlace(std::vector<float>& values)
{
    if (values.empty()) return 0.f;
    const auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    if ((values.size() & 1u) != 0u) return *middle;
    const float upper = *middle;
    return 0.5f * (upper + *std::max_element(values.begin(), middle));
}

void smoothValidProfile(std::vector<float>& values, std::vector<uint8_t>& valid,
                        int radius)
{
    if (radius <= 0 || values.empty()) return;
    const int count = static_cast<int>(values.size());
    std::vector<double> sums(static_cast<size_t>(count + 1), 0.0);
    std::vector<int> hits(static_cast<size_t>(count + 1), 0);
    for (int i = 0; i < count; ++i) {
        sums[static_cast<size_t>(i + 1)] = sums[static_cast<size_t>(i)]
            + (valid[static_cast<size_t>(i)] ? values[static_cast<size_t>(i)] : 0.f);
        hits[static_cast<size_t>(i + 1)] = hits[static_cast<size_t>(i)]
            + (valid[static_cast<size_t>(i)] ? 1 : 0);
    }
    std::vector<float> filtered(values.size(), 0.f);
    std::vector<uint8_t> filtered_valid(values.size(), 0);
    for (int i = 0; i < count; ++i) {
        const int begin = std::max(0, i - radius);
        const int end = std::min(count - 1, i + radius);
        const int n = hits[static_cast<size_t>(end + 1)] - hits[static_cast<size_t>(begin)];
        if (n > 0) {
            filtered[static_cast<size_t>(i)] = static_cast<float>(
                (sums[static_cast<size_t>(end + 1)] - sums[static_cast<size_t>(begin)]) / n);
            filtered_valid[static_cast<size_t>(i)] = 1;
        }
    }
    values = std::move(filtered);
    valid = std::move(filtered_valid);
}

bool applyProfile(AmplitudeRows& rows, std::vector<float> profile,
                  std::vector<uint8_t> valid, int smooth_radius,
                  float strength, float gain_cap_db)
{
    smoothValidProfile(profile, valid, smooth_radius);
    std::vector<float> references;
    references.reserve(profile.size());
    for (size_t i = 0; i < profile.size(); ++i)
        if (valid[i] && profile[i] > 0.f) references.push_back(profile[i]);
    const float target = medianInPlace(references);
    if (!(target > 0.f)) return false;

    gain_cap_db = std::isfinite(gain_cap_db) ? gain_cap_db : 0.f;
    strength = std::isfinite(strength) ? strength : 0.f;
    const float cap = std::pow(10.f, std::clamp(gain_cap_db, 0.f, 40.f) / 20.f);
    strength = std::clamp(strength, 0.f, 1.f);
    bool modified = false;
    for (auto* row : rows) {
        if (!row) continue;
        const size_t count = std::min(row->size(), profile.size());
        for (size_t column = 0; column < count; ++column) {
            auto& sample = (*row)[column];
            if (sample == 0 || !valid[column] || !(profile[column] > 0.f)) continue;
            const float requested = std::clamp(target / profile[column], 1.f / cap, cap);
            const float corrected = sample * std::pow(requested, strength);
            const auto replacement = static_cast<uint16_t>(
                std::clamp(corrected, 0.f, 65535.f));
            modified |= replacement != sample;
            sample = replacement;
        }
    }
    return modified;
}

} // namespace

bool applyBeamPattern(AmplitudeRows& rows, const BeamPatternSettings& p)
{
    if (!p.enabled || rows.empty()) return false;
    const int columns = maxRowLength(rows);
    std::vector<float> profile(static_cast<size_t>(columns), 0.f);
    std::vector<uint8_t> valid(static_cast<size_t>(columns), 0);
    std::vector<uint16_t> values;
    values.reserve(rows.size());
    for (int column = 0; column < columns; ++column) {
        values.clear();
        for (const auto* row : rows) {
            if (!row || column >= static_cast<int>(row->size())) continue;
            const auto value = (*row)[static_cast<size_t>(column)];
            if (value > 0) values.push_back(value);
        }
        if (!values.empty()) {
            // Robust trimmed mean: isolated contacts/shadows must not become
            // part of the learned transducer response curve.
            std::sort(values.begin(), values.end());
            const size_t trim = values.size() >= 10 ? values.size() / 10 : 0;
            const auto begin = values.begin() + static_cast<std::ptrdiff_t>(trim);
            const auto end = values.end() - static_cast<std::ptrdiff_t>(trim);
            const double sum = std::accumulate(begin, end, 0.0);
            profile[static_cast<size_t>(column)] = static_cast<float>(
                sum / static_cast<double>(std::distance(begin, end)));
            valid[static_cast<size_t>(column)] = 1;
        }
    }
    return applyProfile(rows, std::move(profile), std::move(valid),
                        std::clamp(p.smooth_radius, 0, 1000),
                        p.strength, p.gain_cap_db);
}

bool applyArn(AmplitudeRows& rows, const ArnSettings& p)
{
    if (!p.enabled || rows.empty()) return false;
    const int columns = maxRowLength(rows);
    std::vector<float> profile(static_cast<size_t>(columns), 0.f);
    std::vector<uint8_t> valid(static_cast<size_t>(columns), 0);
    std::vector<uint16_t> values;
    values.reserve(rows.size());
    for (int column = 0; column < columns; ++column) {
        values.clear();
        for (const auto* row : rows)
            if (row && column < static_cast<int>(row->size())
                    && (*row)[static_cast<size_t>(column)] > 0)
                values.push_back((*row)[static_cast<size_t>(column)]);
        if (values.empty()) continue;
        const size_t rank = static_cast<size_t>(0.4 * static_cast<double>(values.size() - 1));
        std::nth_element(values.begin(), values.begin() + rank, values.end());
        profile[static_cast<size_t>(column)] = values[rank];
        valid[static_cast<size_t>(column)] = 1;
    }
    return applyProfile(rows, std::move(profile), std::move(valid),
                        std::clamp(p.column_smooth, 0, 1000),
                        p.strength, p.gain_cap_db);
}

bool applyDestripe(AmplitudeRows& rows, const DestripeSettings& p)
{
    if (!p.enabled || rows.empty()) return false;
    const int row_count = static_cast<int>(rows.size());
    const int columns = maxRowLength(rows);
    if (columns == 0) return false;
    bool modified = false;
    const int window = std::clamp(p.window, 1, 5000);
    const int left = (window - 1) / 2;
    const int right = window / 2;
    const int zones = std::clamp(p.subdivision, 1, 64);
    const int zone_width = (columns + zones - 1) / zones;
    const float cap = std::clamp(std::isfinite(p.capping) ? p.capping : 1.f,
                                 1.f, 10.f);
    const float threshold_db = std::clamp(
        std::isfinite(p.threshold_db) ? p.threshold_db : 1.f, 0.f, 12.f);
    const float log_threshold = threshold_db * std::log(10.f) / 20.f;
    std::vector<std::vector<uint16_t>> original(static_cast<size_t>(row_count));
    for (int row = 0; row < row_count; ++row)
        if (rows[static_cast<size_t>(row)]) original[static_cast<size_t>(row)] = *rows[static_cast<size_t>(row)];

    for (int zone = 0; zone < zones; ++zone) {
        const int first_column = zone * zone_width;
        const int last_column = std::min(columns, first_column + zone_width);
        if (first_column >= last_column) break;
        std::vector<float> means(static_cast<size_t>(row_count), 0.f);
        for (int row = 0; row < row_count; ++row) {
            double sum = 0.0; int count = 0;
            for (int column = first_column;
                 column < last_column && column < static_cast<int>(original[static_cast<size_t>(row)].size());
                 ++column) {
                const auto value = original[static_cast<size_t>(row)][static_cast<size_t>(column)];
                if (value > 0) { sum += value; ++count; }
            }
            if (count > 0) means[static_cast<size_t>(row)] = static_cast<float>(sum / count);
        }
        std::vector<float> neighbours;
        neighbours.reserve(static_cast<size_t>(window));
        for (int row = 0; row < row_count; ++row) {
            const float current = means[static_cast<size_t>(row)];
            if (!(current > 0.f) || !rows[static_cast<size_t>(row)]) continue;
            neighbours.clear();
            for (int nearby = std::max(0, row - left);
                 nearby <= std::min(row_count - 1, row + right); ++nearby)
                if (means[static_cast<size_t>(nearby)] > 0.f)
                    neighbours.push_back(means[static_cast<size_t>(nearby)]);
            const float reference = medianInPlace(neighbours);
            if (!(reference > 0.f)) continue;
            const float requested = reference / current;
            if (std::fabs(std::log(requested)) < log_threshold) continue;
            const float factor = std::clamp(requested, 1.f / cap, cap);
            auto& output = *rows[static_cast<size_t>(row)];
            for (int column = first_column;
                 column < last_column && column < static_cast<int>(output.size()); ++column) {
                const auto replacement = static_cast<uint16_t>(std::clamp(
                    original[static_cast<size_t>(row)][static_cast<size_t>(column)] * factor,
                    0.f, 65535.f));
                modified |= replacement != output[static_cast<size_t>(column)];
                output[static_cast<size_t>(column)] = replacement;
            }
        }
    }
    return modified;
}

bool applyAdaptiveContrast(AmplitudeRows& rows, const AdaptiveContrastSettings& p)
{
    if (!p.enabled || rows.empty()) return false;
    const int row_count = static_cast<int>(rows.size());
    const int columns = maxRowLength(rows);
    if (columns == 0) return false;
    const int tile_rows = std::clamp(p.tile_pings, 16, 4096);
    const int tile_columns = std::clamp(p.tile_samps, 16, 8192);
    const int tiles_y = (row_count + tile_rows - 1) / tile_rows;
    const int tiles_x = (columns + tile_columns - 1) / tile_columns;
    using Lut = std::array<uint16_t, 256>;
    std::vector<Lut> luts(static_cast<size_t>(tiles_y * tiles_x));
    std::vector<uint8_t> active(static_cast<size_t>(tiles_y * tiles_x), 0);

    for (int ty = 0; ty < tiles_y; ++ty) for (int tx = 0; tx < tiles_x; ++tx) {
        std::array<int, 256> histogram{};
        int valid_pixels = 0;
        for (int row = ty * tile_rows; row < std::min(row_count, (ty + 1) * tile_rows); ++row) {
            if (!rows[static_cast<size_t>(row)]) continue;
            const auto& values = *rows[static_cast<size_t>(row)];
            for (int column = tx * tile_columns;
                 column < std::min(static_cast<int>(values.size()), (tx + 1) * tile_columns); ++column) {
                const auto value = values[static_cast<size_t>(column)];
                if (value > 0) { ++histogram[value >> 8]; ++valid_pixels; }
            }
        }
        auto& lut = luts[static_cast<size_t>(ty * tiles_x + tx)];
        for (int bin = 0; bin < 256; ++bin) lut[static_cast<size_t>(bin)] = static_cast<uint16_t>(bin * 257);
        if (valid_pixels == 0) continue;
        const int occupied_bins = static_cast<int>(std::count_if(
            histogram.cbegin(), histogram.cend(), [](int count) { return count > 0; }));
        if (occupied_bins <= 1) continue;
        const float clip = std::clamp(
            std::isfinite(p.clip_limit) ? p.clip_limit : 1.f, 1.f, 16.f);
        const int limit = std::max(1, static_cast<int>(clip * valid_pixels / 256.f));
        int excess = 0;
        for (auto& count : histogram) if (count > limit) { excess += count - limit; count = limit; }
        const int quotient = excess / 256, remainder = excess % 256;
        for (int bin = 0; bin < 256; ++bin) histogram[static_cast<size_t>(bin)] += quotient + (bin < remainder ? 1 : 0);
        int cdf = 0, cdf_min = 0;
        for (int count : histogram) if (count > 0) { cdf_min = count; break; }
        if (valid_pixels <= cdf_min) continue;
        active[static_cast<size_t>(ty * tiles_x + tx)] = 1;
        for (int bin = 0; bin < 256; ++bin) {
            cdf += histogram[static_cast<size_t>(bin)];
            lut[static_cast<size_t>(bin)] = static_cast<uint16_t>(std::clamp(
                (cdf - cdf_min) * 65535.f / (valid_pixels - cdf_min), 0.f, 65535.f));
        }
    }

    bool modified = false;
    for (int row = 0; row < row_count; ++row) {
        if (!rows[static_cast<size_t>(row)]) continue;
        auto& values = *rows[static_cast<size_t>(row)];
        for (int column = 0; column < static_cast<int>(values.size()); ++column) {
            auto& value = values[static_cast<size_t>(column)];
            if (value == 0) continue;
            const float fy = (row + 0.5f) / tile_rows - 0.5f;
            const float fx = (column + 0.5f) / tile_columns - 0.5f;
            int y0 = static_cast<int>(std::floor(fy));
            int x0 = static_cast<int>(std::floor(fx));
            float wy = fy - std::floor(fy), wx = fx - std::floor(fx);
            int y1 = y0 + 1, x1 = x0 + 1;
            if (y0 < 0) { y0 = y1 = 0; wy = 0.f; }
            else if (y1 >= tiles_y) { y0 = y1 = tiles_y - 1; wy = 0.f; }
            if (x0 < 0) { x0 = x1 = 0; wx = 0.f; }
            else if (x1 >= tiles_x) { x0 = x1 = tiles_x - 1; wx = 0.f; }
            const int bin = value >> 8;
            const auto sample = [&](int y, int x) {
                const size_t tile = static_cast<size_t>(y * tiles_x + x);
                return active[tile] ? luts[tile][static_cast<size_t>(bin)] : value;
            };
            const float mapped = (1.f - wy) * ((1.f - wx) * sample(y0, x0) + wx * sample(y0, x1))
                               + wy * ((1.f - wx) * sample(y1, x0) + wx * sample(y1, x1));
            const auto replacement = static_cast<uint16_t>(
                std::clamp(mapped, 0.f, 65535.f));
            modified |= replacement != value;
            value = replacement;
        }
    }
    return modified;
}

} // namespace dolphin::pipeline::enhancement
