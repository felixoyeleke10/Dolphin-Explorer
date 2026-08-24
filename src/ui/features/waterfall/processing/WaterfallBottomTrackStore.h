#pragma once

#include "core/SidescanGeometry.h"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>

namespace dolphin::ui {

struct WaterfallChannelRecordKey {
    std::uint64_t artifact_id = 0;
    std::int64_t timestamp_us = 0;
    core::SidescanChannel channel = core::SidescanChannel::Port;
    bool operator==(const WaterfallChannelRecordKey&) const noexcept = default;
};

struct WaterfallChannelRecordKeyHash {
    std::size_t operator()(const WaterfallChannelRecordKey& key) const noexcept
    {
        const std::uint64_t identity = key.artifact_id != 0
            ? key.artifact_id : static_cast<std::uint64_t>(key.timestamp_us);
        return std::hash<std::uint64_t>{}(
            identity ^ (static_cast<std::uint64_t>(key.channel) << 61));
    }
};

inline WaterfallChannelRecordKey waterfallChannelRecordKey(
    std::uint64_t artifact_id, std::int64_t timestamp_us,
    core::SidescanChannel channel) noexcept
{
    return {artifact_id, artifact_id != 0 ? 0 : timestamp_us, channel};
}

// Persistent session edits keyed by source-record identity. Artifact IDs are
// preferred; timestamp is a compatibility fallback for formats without IDs.
class WaterfallBottomTrackStore {
public:
    void clear() noexcept { m_by_record.clear(); }
    bool empty() const noexcept { return m_by_record.empty(); }
    std::size_t size() const noexcept { return m_by_record.size(); }

    void set(WaterfallChannelRecordKey key,
             core::SidescanRangeCoordinate coordinate)
    {
        if ((key.artifact_id == 0 && key.timestamp_us == 0) || !coordinate.valid()) return;
        m_by_record.insert_or_assign(key, coordinate);
    }

    void erase(WaterfallChannelRecordKey key) { m_by_record.erase(key); }

    std::optional<core::SidescanRangeCoordinate> get(
        WaterfallChannelRecordKey key) const
    {
        const auto it = m_by_record.find(key);
        return it == m_by_record.end() ? std::nullopt
                                       : std::optional{it->second};
    }

private:
    std::unordered_map<WaterfallChannelRecordKey, core::SidescanRangeCoordinate,
                       WaterfallChannelRecordKeyHash> m_by_record;
};

} // namespace dolphin::ui
