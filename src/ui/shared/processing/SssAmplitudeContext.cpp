#include "ui/shared/processing/SssAmplitudeContext.h"

#include "app/services/ImportService.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace dolphin::ui::imaging {
namespace {

constexpr size_t kContextPingGroups = 1024;
constexpr size_t kContextEntries    = 2048;
constexpr int    kContextSamples    = 256;
constexpr size_t kMaxResidentContexts = 8;
constexpr int64_t kHardInterpolationGapUs = 5'000'000;

uint64_t mix(uint64_t h, uint64_t value) noexcept
{
    constexpr uint64_t kPrime = 1099511628211ull;
    for (int i = 0; i < 8; ++i) {
        h ^= (value >> (i * 8)) & 0xffu;
        h *= kPrime;
    }
    return h;
}

uint64_t mixFloat(uint64_t h, double value) noexcept
{
    if (!std::isfinite(value)) value = 0.0;
    return mix(h, static_cast<uint64_t>(
        static_cast<int64_t>(std::llround(value * 10000.0))));
}

double rowKey(const core::SidescanPing& ping) noexcept
{
    if (ping.timestamp_us != 0)
        return static_cast<double>(ping.timestamp_us);
    if (ping.ping_number != 0)
        return static_cast<double>(ping.ping_number);
    return static_cast<double>(ping.id);
}

double rowKey(const SssAmplitudeContextRow& row) noexcept
{
    return row.sort_key;
}

void filterBand(std::vector<core::ArtifactIndexEntry>& entries, float requested_hz)
{
    if (!(requested_hz > 0.f)) return;

    std::vector<float> bands;
    for (const auto& entry : entries) {
        if (!(entry.frequency_hz > 0.f)) continue;
        const bool known = std::any_of(bands.cbegin(), bands.cend(),
            [&](float band) { return std::fabs(band - entry.frequency_hz) < 1.f; });
        if (!known) bands.push_back(entry.frequency_hz);
    }
    if (bands.size() < 2) return;

    float selected = bands.front();
    for (float band : bands)
        if (std::fabs(band - requested_hz) < std::fabs(selected - requested_hz))
            selected = band;

    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [selected](const core::ArtifactIndexEntry& entry) {
            return entry.frequency_hz > 0.f
                && std::fabs(entry.frequency_hz - selected) >= 1.f;
        }), entries.end());
}

core::ArtifactIndex contextIndex(const core::ArtifactIndex& source,
                                 float frequency_hz)
{
    std::vector<core::ArtifactIndexEntry> entries;
    entries.reserve(source.entries.size());
    for (const auto& entry : source.entries)
        if (entry.type == core::ArtifactType::Sidescan)
            entries.push_back(entry);
    filterBand(entries, frequency_hz);

    struct Span { size_t begin = 0; size_t end = 0; };
    std::vector<Span> groups;
    groups.reserve(entries.size());
    const bool use_ping_number = !entries.empty() && entries.front().ping_number != 0;
    for (size_t begin = 0; begin < entries.size();) {
        size_t end = begin + 1;
        while (end < entries.size()
               && (use_ping_number
                   ? entries[end].ping_number == entries[begin].ping_number
                   : entries[end].timestamp_us == entries[begin].timestamp_us)) {
            ++end;
        }
        groups.push_back({begin, end});
        begin = end;
    }

    std::vector<core::ArtifactIndexEntry> selected;
    if (groups.size() <= kContextPingGroups) {
        selected = std::move(entries);
    } else {
        selected.reserve(kContextEntries);
        for (size_t i = 0; i < kContextPingGroups; ++i) {
            const size_t gi = i * (groups.size() - 1) / (kContextPingGroups - 1);
            const auto span = groups[gi];
            selected.insert(selected.end(), entries.begin() + span.begin,
                            entries.begin() + span.end);
        }
    }

    // Normal port/starboard data is already <= 2,048 entries. This is only a
    // malformed/multi-record group safety backstop.
    if (selected.size() > kContextEntries) {
        std::vector<core::ArtifactIndexEntry> bounded;
        bounded.reserve(kContextEntries);
        for (size_t i = 0; i < kContextEntries; ++i) {
            const size_t at = i * (selected.size() - 1) / (kContextEntries - 1);
            bounded.push_back(selected[at]);
        }
        selected = std::move(bounded);
    }

    core::ArtifactIndex result;
    result.source_id = source.source_id;
    result.entries = std::move(selected);
    return result;
}

float gainAt(const SssAmplitudeContextRow& row, float sample_fraction) noexcept
{
    const float position = std::clamp(sample_fraction, 0.f, 1.f)
                         * static_cast<float>(kSssAmplitudeContextBins - 1);
    const size_t lo = static_cast<size_t>(std::floor(position));
    const size_t hi = std::min(lo + 1, kSssAmplitudeContextBins - 1);
    const float t = position - static_cast<float>(lo);
    return row.gain[lo] + (row.gain[hi] - row.gain[lo]) * t;
}

const SssAmplitudeContextRow* exactRow(
    const std::vector<SssAmplitudeContextRow>& rows, uint64_t id) noexcept
{
    if (id == 0) return nullptr;
    const auto it = std::find_if(rows.cbegin(), rows.cend(),
        [id](const SssAmplitudeContextRow& row) { return row.id == id; });
    return it == rows.cend() ? nullptr : &*it;
}

struct ResidentRepository {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<const SssAmplitudeContext>> values;
    std::vector<std::string> order;
};

ResidentRepository& repository()
{
    static ResidentRepository value;
    return value;
}

std::string repositoryKey(const SssAmplitudeContextRequest& request)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto size = fs::file_size(fs::path(request.store_path), ec);
    const uint64_t safe_size = ec ? 0 : static_cast<uint64_t>(size);
    ec.clear();
    const auto mtime = fs::last_write_time(fs::path(request.store_path), ec);
    const auto safe_mtime = ec ? int64_t{0}
        : static_cast<int64_t>(mtime.time_since_epoch().count());
    uint64_t index_fingerprint = 1469598103934665603ull;
    if (request.artifact_index) {
        for (const auto& entry : request.artifact_index->entries) {
            if (entry.type != core::ArtifactType::Sidescan) continue;
            index_fingerprint = mix(index_fingerprint, entry.artifact_id);
            index_fingerprint = mix(index_fingerprint,
                                    static_cast<uint64_t>(entry.timestamp_us));
            index_fingerprint = mix(index_fingerprint, entry.ping_number);
        }
    }
    return request.store_path + '|' + std::to_string(safe_size) + '|'
         + std::to_string(safe_mtime) + '|'
         + std::to_string(static_cast<int>(std::llround(request.frequency_hz))) + '|'
         + std::to_string(index_fingerprint) + '|'
         + std::to_string(sssAmplitudeParamsFingerprint(request.params));
}

} // namespace

uint64_t sssAmplitudeParamsFingerprint(const WaterfallParams& p) noexcept
{
    uint64_t h = 1469598103934665603ull;
    h = mix(h, 2); // canonical context algorithm revision
    h = mix(h, p.tvg.enabled);             h = mixFloat(h, p.tvg.spreading);
    h = mixFloat(h, p.tvg.absorption);
    h = mix(h, p.arc.enabled);             h = mixFloat(h, p.arc.exponent);
    h = mixFloat(h, p.arc.gain_cap_db);
    h = mix(h, p.agc.enabled);             h = mix(h, static_cast<uint64_t>(p.agc.mode));
    h = mixFloat(h, p.agc.strength);       h = mix(h, p.agc.along_track_win);
    h = mix(h, static_cast<uint64_t>(p.agc.smoothing_type));
    h = mix(h, p.agc.smoothing_win);       h = mix(h, p.agc.edge_skip_samples);
    h = mixFloat(h, p.agc.noise_floor_pct);
    h = mix(h, p.beam_pattern.enabled);    h = mixFloat(h, p.beam_pattern.strength);
    h = mix(h, p.beam_pattern.smooth_radius);
    h = mix(h, p.arn.enabled);             h = mixFloat(h, p.arn.strength);
    h = mixFloat(h, p.arn.gain_cap_db);    h = mix(h, p.arn.column_smooth);
    h = mix(h, p.destripe.enabled);        h = mix(h, p.destripe.window);
    h = mix(h, p.destripe.subdivision);    h = mixFloat(h, p.destripe.capping);
    h = mix(h, p.ml_enhance.enabled);      h = mix(h, p.ml_enhance.tile_pings);
    h = mix(h, p.ml_enhance.tile_samps);   h = mixFloat(h, p.ml_enhance.clip_limit);
    return h;
}

std::shared_ptr<const SssAmplitudeContext>
buildSssAmplitudeContextFromCalibrated(
    const std::vector<core::SidescanPing>& calibrated,
    const WaterfallParams& params)
{
    if (calibrated.empty()) return {};

    auto processed = calibrated;
    applyContextCalibrationAndImaging(processed, params);
    if (processed.size() != calibrated.size()) return {};

    auto context = std::make_shared<SssAmplitudeContext>();
    context->params_fingerprint = sssAmplitudeParamsFingerprint(params);
    const auto stretch = computeAutoStretch(processed);
    context->stretch_low = stretch.low;
    context->stretch_high = stretch.high;
    if (!(context->stretch_high > context->stretch_low)) {
        context->stretch_low = 0.f;
        context->stretch_high = 1.f;
    }

    for (size_t i = 0; i < calibrated.size(); ++i) {
        const auto& before = calibrated[i];
        const auto& after  = processed[i];
        if (before.samples.empty() || before.samples.size() != after.samples.size())
            continue;

        SssAmplitudeContextRow row;
        row.id = before.id;
        row.timestamp_us = before.timestamp_us;
        row.ping_number = before.ping_number;
        row.sort_key = rowKey(before);
        const size_t last = before.samples.size() - 1;
        for (size_t bin = 0; bin < kSssAmplitudeContextBins; ++bin) {
            const size_t at = bin * last / (kSssAmplitudeContextBins - 1);
            const float input = static_cast<float>(before.samples[at].amplitude);
            const float output = static_cast<float>(after.samples[at].amplitude);
            row.gain[bin] = input > 0.f
                ? std::clamp(output / input, 0.f, 64.f)
                : 1.f;
        }
        auto& channel = before.channel == core::SidescanChannel::Port
            ? context->port : context->starboard;
        channel.push_back(std::move(row));
    }

    const auto order = [](const SssAmplitudeContextRow& a,
                          const SssAmplitudeContextRow& b) {
        if (a.sort_key != b.sort_key) return a.sort_key < b.sort_key;
        return a.id < b.id;
    };
    std::sort(context->port.begin(), context->port.end(), order);
    std::sort(context->starboard.begin(), context->starboard.end(), order);
    return context->valid() ? context : std::shared_ptr<const SssAmplitudeContext>{};
}

std::shared_ptr<const SssAmplitudeContext>
getOrBuildSssAmplitudeContext(const SssAmplitudeContextRequest& request,
                              const app::CancellationToken& cancel)
{
    if (!request.artifact_index || request.artifact_index->empty()
            || request.store_path.empty()
            || cancel.isCancelled()) {
        return {};
    }

    const std::string key = repositoryKey(request);
    auto& repo = repository();
    {
        std::lock_guard lock(repo.mutex);
        if (const auto it = repo.values.find(key); it != repo.values.end())
            return it->second;
    }

    core::ArtifactIndex index = contextIndex(
        *request.artifact_index, request.frequency_hz);
    auto calibrated = app::ImportService::loadAllSidescanPingsFromStore(
        request.store_path, request.store_format, index, request.source_path,
        kContextSamples, {},
        [params = request.params](core::SidescanPing& ping) {
            applyPerPingCalibration(ping, params);
        },
        [&cancel]() { return cancel.isCancelled(); });
    if (calibrated.empty() || cancel.isCancelled()) return {};

    auto built = buildSssAmplitudeContextFromCalibrated(calibrated, request.params);
    if (!built || cancel.isCancelled()) return {};

    std::lock_guard lock(repo.mutex);
    if (const auto it = repo.values.find(key); it != repo.values.end())
        return it->second;
    repo.values.emplace(key, built);
    repo.order.push_back(key);
    while (repo.order.size() > kMaxResidentContexts) {
        repo.values.erase(repo.order.front());
        repo.order.erase(repo.order.begin());
    }
    return built;
}

void applySssAmplitudeContext(std::vector<core::SidescanPing>& pings,
                              const SssAmplitudeContext& context)
{
    if (!context.valid()) return;

    for (auto& ping : pings) {
        if (ping.samples.empty()) continue;
        const auto& rows = ping.channel == core::SidescanChannel::Port
            ? context.port : context.starboard;
        if (rows.empty()) continue;

        const SssAmplitudeContextRow* a = exactRow(rows, ping.id);
        const SssAmplitudeContextRow* b = a;
        float along_t = 0.f;
        if (!a) {
            const double key = rowKey(ping);
            const auto upper = std::lower_bound(rows.cbegin(), rows.cend(), key,
                [](const SssAmplitudeContextRow& row, double value) {
                    return rowKey(row) < value;
                });
            if (upper == rows.cbegin()) {
                a = b = &rows.front();
            } else if (upper == rows.cend()) {
                a = b = &rows.back();
            } else {
                a = &*std::prev(upper);
                b = &*upper;
                const double span = b->sort_key - a->sort_key;
                if (span > 0.0) {
                    along_t = static_cast<float>(std::clamp(
                        (key - a->sort_key) / span, 0.0, 1.0));
                }
                if (ping.timestamp_us != 0 && a->timestamp_us != 0
                        && b->timestamp_us != 0
                        && b->timestamp_us - a->timestamp_us
                            > kHardInterpolationGapUs) {
                    if (along_t < 0.5f) b = a;
                    else a = b;
                    along_t = 0.f;
                }
            }
        }

        const size_t last = ping.samples.size() - 1;
        for (size_t i = 0; i < ping.samples.size(); ++i) {
            const float fraction = last > 0
                ? static_cast<float>(i) / static_cast<float>(last) : 0.f;
            const float ga = gainAt(*a, fraction);
            const float gb = gainAt(*b, fraction);
            const float gain = ga + (gb - ga) * along_t;
            ping.samples[i].amplitude = static_cast<uint16_t>(std::clamp(
                static_cast<float>(ping.samples[i].amplitude) * gain + 0.5f,
                0.f, 65535.f));
        }
    }
}

} // namespace dolphin::ui::imaging
