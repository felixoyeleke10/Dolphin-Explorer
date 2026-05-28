// XtfNav.cpp — nav fix loading and interpolation for XTF files.
#include "io/xtf/XtfNav.h"
#include "io/xtf/XtfReader_p.h"
#include "io/FileIo.h"
#include <algorithm>
#include <cstring>

namespace dolphin::io {

void XtfNavTable::load(FILE* file, uint64_t data_start, uint64_t file_size) const
{
    if (loaded) return;
    loaded = true;
    if (!file) return;

    uint64_t saved_pos = 0;
    if (!detail::tellFile(file, saved_pos)) return;
    if (!detail::seekAbs(file, data_start)) {
        detail::seekAbs(file, saved_pos);
        return;
    }

    while (true) {
        uint64_t offset = 0;
        if (!detail::tellFile(file, offset)) break;
        if (offset >= file_size) break;

        XtfPacketHeader pkt{};
        if (fread(&pkt, sizeof(pkt), 1, file) != 1) break;
        if (pkt.MagicNumber != XTF_MAGIC) {
            if (!detail::seekAbs(file, offset + 1)) break;
            continue;
        }

        uint32_t record_bytes = pkt.NumBytesThisRecord;
        if (record_bytes < sizeof(XtfPacketHeader)) break;
        if (record_bytes > 64u * 1024u * 1024u) {
            if (!detail::seekAbs(file, offset + 1)) break;
            continue;
        }
        if (offset + record_bytes > file_size) break;

        if (pkt.HeaderType == PACKET_NAV || pkt.HeaderType == PACKET_PING) {
            double lat = pkt.SensorYcoordinate;
            double lon = pkt.SensorXcoordinate;
            if (lat == 0.0 && lon == 0.0) {
                lat = pkt.ShipYcoordinate;
                lon = pkt.ShipXcoordinate;
            }
            if (hasUsableCoordinate(lat, lon))
                fixes.push_back({packetTimestampUs(pkt), lat, lon});
        }

        if (!detail::seekAbs(file, offset + record_bytes)) break;
    }

    std::sort(fixes.begin(), fixes.end(),
              [](const XtfNavFix& a, const XtfNavFix& b){
                  return a.timestamp_us < b.timestamp_us;
              });

    detail::seekAbs(file, saved_pos);
}

void XtfNavTable::interpolate(int64_t timestamp_us,
                               double& out_lat, double& out_lon) const
{
    if (fixes.empty()) return;

    auto it = std::lower_bound(fixes.begin(), fixes.end(), timestamp_us,
        [](const XtfNavFix& f, int64_t ts){ return f.timestamp_us < ts; });

    if (it == fixes.end()) {
        out_lat = fixes.back().lat; out_lon = fixes.back().lon;
    } else if (it == fixes.begin()) {
        out_lat = fixes.front().lat; out_lon = fixes.front().lon;
    } else {
        const auto& a = *(it - 1);
        const auto& b = *it;
        const double dt = static_cast<double>(b.timestamp_us - a.timestamp_us);
        const double t  = (dt > 0.0)
            ? static_cast<double>(timestamp_us - a.timestamp_us) / dt
            : 0.0;
        out_lat = a.lat + t * (b.lat - a.lat);
        out_lon = a.lon + t * (b.lon - a.lon);
    }
}

} // namespace dolphin::io
