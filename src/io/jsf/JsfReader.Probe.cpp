// JsfReader.Probe.cpp — lightweight header scan for the import wizard.
#include "io/jsf/JsfReader_p.h"
#include "io/FileIo.h"
#include "io/ProbeResult.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace dolphin::io {

using namespace detail_jsf;

ProbeResult JsfReader::probe(const std::string& path)
{
    ProbeResult r;
    r.path        = path;
    r.format_name = "JSF";

    if (!open(path)) {
        r.error_message = "Cannot open JSF file";
        return r;
    }
    r.file_size_bytes = static_cast<int64_t>(m_fileSize);

    if (!detail::seekAbs(m_file, 0)) {
        close();
        r.success = true;
        return r;
    }

    // -- Scan up to 200 messages for modality, nav, and heading --------------
    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double min_y = std::numeric_limits<double>::max();
    double max_y = std::numeric_limits<double>::lowest();
    int samples = 0, scanned = 0;
    bool has_sbp = false, has_sss = false;

    float  hdg_min = std::numeric_limits<float>::max();
    float  hdg_max = std::numeric_limits<float>::lowest();
    double hdg_sum = 0.0;
    int    hdg_n   = 0;

    // Track unique subsystems for the channel table (subsystem_id → ChannelInfo)
    std::map<uint8_t, io::ProbeResult::ChannelInfo> chan_map;

    while (scanned < 200) {
        uint64_t offset = 0;
        if (!detail::tellFile(m_file, offset)) break;
        if (offset >= m_fileSize) break;

        JsfPacketHeader pkt{};
        if (std::fread(&pkt, sizeof(pkt), 1, m_file) != 1) break;

        if (pkt.marker != JSF_MARKER) {
            if (!detail::seekAbs(m_file, offset + 1)) break;
            continue;
        }
        ++scanned;

        if (pkt.size == 0) continue;
        if (pkt.size > kMaxRecordSz) {
            if (!detail::seekAbs(m_file, offset + 1)) break;
            continue;
        }

        if (pkt.type == MSG_SONAR && pkt.size >= kPingHdrSize) {
            if (pkt.subsystem == SUBSYS_SBP) has_sbp = true;
            else                              has_sss = true;

            JsfSonarPingHeader ph{};
            const bool hdr_ok = (std::fread(&ph, sizeof(ph), 1, m_file) == 1);
            if (hdr_ok) {
                // Nav
                if (samples < 50) {
                    const bool s_ok = hasUsableSensorCoord(ph.sensor_lat, ph.sensor_lon);
                    double lat = s_ok ? static_cast<double>(ph.sensor_lat) : ph.ship_lat;
                    double lon = s_ok ? static_cast<double>(ph.sensor_lon) : ph.ship_lon;
                    if (hasUsableCoordinate(lat, lon)) {
                        r.coord_valid = true;
                        if (r.coord_samples.size() < 5)
                            r.coord_samples.push_back({lon, lat});
                        min_x = std::min(min_x, lon);
                        max_x = std::max(max_x, lon);
                        min_y = std::min(min_y, lat);
                        max_y = std::max(max_y, lat);
                        ++samples;
                    }
                }

                // Heading
                const float hdg = ph.heading_deg;
                if (std::isfinite(hdg) && hdg >= 0.f && hdg <= 360.f) {
                    hdg_min = std::min(hdg_min, hdg);
                    hdg_max = std::max(hdg_max, hdg);
                    hdg_sum += hdg;
                    ++hdg_n;
                }

                // Channel table — one entry per unique subsystem
                if (chan_map.find(pkt.subsystem) == chan_map.end()) {
                    io::ProbeResult::ChannelInfo ci;
                    if (pkt.subsystem == SUBSYS_SBP) {
                        ci.name     = "Sub-Bottom";
                        ci.modality = "Sub-Bottom";
                    } else if (pkt.subsystem == SUBSYS_PORT) {
                        ci.name     = "Port Sidescan";
                        ci.modality = "Sidescan";
                    } else if (pkt.subsystem == SUBSYS_STBD) {
                        ci.name     = "Starboard Sidescan";
                        ci.modality = "Sidescan";
                    } else {
                        ci.name     = "Subsystem " + std::to_string(pkt.subsystem);
                        ci.modality = "Unknown";
                    }
                    ci.frequency_khz    = (ph.frequency_hz > 0) ? ph.frequency_hz / 1000.f : 0.f;
                    ci.range_m          = ph.range_m;
                    ci.samples_per_ping = ph.num_samples;
                    chan_map[pkt.subsystem] = std::move(ci);
                }
            }
        }

        if (!detail::seekAbs(m_file, offset + sizeof(JsfPacketHeader) + pkt.size)) break;
    }

    r.has_sidescan  = has_sss;
    r.has_subbottom = has_sbp;
    if (!r.has_sidescan && !r.has_subbottom) r.has_sidescan = true;

    // Heading
    if (hdg_n > 0) {
        r.heading_valid = true;
        r.heading_min   = hdg_min;
        r.heading_max   = hdg_max;
        r.heading_mean  = static_cast<float>(hdg_sum / hdg_n);
    }

    // Channel table — preserve subsystem order (port=0, stbd=1, sbp=20)
    for (auto& [id, ci] : chan_map)
        r.channels.push_back(std::move(ci));

    // JSF nav is always WGS-84 geographic
    r.is_projected = false;
    r.declared_crs = core::makeWgs84SpatialRef();

    if (r.coord_valid) {
        r.coord_min_x = min_x;
        r.coord_max_x = max_x;
        r.coord_min_y = min_y;
        r.coord_max_y = max_y;

        const bool looks_proj = (std::abs(min_x) > 180.0 || std::abs(min_y) > 90.0
                              || std::abs(max_x) > 180.0 || std::abs(max_y) > 90.0);
        if (looks_proj) {
            r.is_projected     = true;
            r.needs_crs_review = true;
            r.warnings.push_back("Coordinate values appear projected (unusual for JSF)");
        }
    } else {
        r.needs_crs_review = true;
    }

    close();
    r.success = true;
    return r;
}

} // namespace dolphin::io
