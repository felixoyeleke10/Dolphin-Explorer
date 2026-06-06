// XtfReader.Probe.cpp — lightweight header scan for the import wizard.
#include "io/xtf/XtfReader.h"
#include "io/xtf/XtfReader_p.h"
#include "io/FileIo.h"
#include "io/ProbeResult.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dolphin::io {

ProbeResult XtfReader::probe(const std::string& path)
{
    ProbeResult r;
    r.path        = path;
    r.format_name = "XTF";

    if (!open(path)) {
        r.error_message = "Cannot open or parse XTF file";
        return r;
    }
    r.file_size_bytes = static_cast<int64_t>(m_fileSize);

    // -- Modalities + channel table from file header --------------------------
    for (const auto& ch : m_chan_info) {
        if (ch.type == CHAN_PORT_SSS || ch.type == CHAN_STBD_SSS) r.has_sidescan  = true;
        if (ch.type == CHAN_SUBBOT)                                r.has_subbottom = true;

        io::ProbeResult::ChannelInfo ci;
        if (!ch.channel_name.empty())
            ci.name = ch.channel_name;
        else if (ch.type == CHAN_SUBBOT)    ci.name = "Sub-Bottom";
        else if (ch.type == CHAN_PORT_SSS)  ci.name = "Port Sidescan";
        else if (ch.type == CHAN_STBD_SSS)  ci.name = "Starboard Sidescan";
        else                                ci.name = "Channel " + std::to_string(r.channels.size() + 1);

        ci.modality         = (ch.type == CHAN_SUBBOT) ? "Sub-Bottom" : "Sidescan";
        ci.frequency_khz    = (ch.frequency_hz > 0.f) ? ch.frequency_hz / 1000.f : 0.f;
        ci.samples_per_ping = ch.samples_per_channel;
        r.channels.push_back(std::move(ci));
    }
    if (!r.has_sidescan && !r.has_subbottom)
        r.has_sidescan = true;  // default: XTF is predominantly SSS

    // -- CRS from file header -------------------------------------------------
    r.is_projected = m_coords_projected;
    r.declared_crs = m_meta.coordinate_ref;

    // -- Scan up to 50 PING packets for nav coordinates -----------------------
    if (!detail::seekAbs(m_file, m_dataStartOffset)) {
        close();
        r.success = true;
        return r;
    }

    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double min_y = std::numeric_limits<double>::max();
    double max_y = std::numeric_limits<double>::lowest();
    int    samples = 0;

    float  hdg_min = std::numeric_limits<float>::max();
    float  hdg_max = std::numeric_limits<float>::lowest();
    double hdg_sum = 0.0;
    int    hdg_n   = 0;

    while (samples < 50) {
        uint64_t offset = 0;
        if (!detail::tellFile(m_file, offset)) break;
        if (offset + sizeof(XtfPacketHeader) > m_fileSize) break;

        XtfPacketHeader pkt{};
        if (std::fread(&pkt, sizeof(pkt), 1, m_file) != 1) break;

        if (pkt.MagicNumber != XTF_MAGIC) {
            if (!detail::seekAbs(m_file, offset + 1)) break;
            continue;
        }

        const uint32_t record_bytes = pkt.NumBytesThisRecord;
        if (record_bytes < sizeof(XtfPacketHeader)) break;
        if (record_bytes > 64u * 1024u * 1024u) {
            if (!detail::seekAbs(m_file, offset + 1)) break;
            continue;
        }

        if (pkt.HeaderType == PACKET_PING) {
            double lat = pkt.SensorYcoordinate;
            double lon = pkt.SensorXcoordinate;
            if (!hasUsableCoordinate(lat, lon)) {
                lat = pkt.ShipYcoordinate;
                lon = pkt.ShipXcoordinate;
            }
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

            // Heading — only collect finite, plausible values (0–360°)
            const float hdg = pkt.SensorHeading;
            if (std::isfinite(hdg) && hdg >= 0.f && hdg <= 360.f) {
                hdg_min = std::min(hdg_min, hdg);
                hdg_max = std::max(hdg_max, hdg);
                hdg_sum += hdg;
                ++hdg_n;
            }
        }

        if (!detail::seekAbs(m_file, offset + record_bytes)) break;
    }

    if (hdg_n > 0) {
        r.heading_valid = true;
        r.heading_min   = hdg_min;
        r.heading_max   = hdg_max;
        r.heading_mean  = static_cast<float>(hdg_sum / hdg_n);
    }

    if (r.coord_valid) {
        r.coord_min_x = min_x;
        r.coord_max_x = max_x;
        r.coord_min_y = min_y;
        r.coord_max_y = max_y;

        // Check if coordinate magnitudes contradict the file header declaration
        const bool looks_proj = (std::abs(min_x) > 180.0 || std::abs(min_y) > 90.0
                              || std::abs(max_x) > 180.0 || std::abs(max_y) > 90.0);
        if (!r.is_projected && looks_proj) {
            r.is_projected       = true;
            r.units_contradicted = true;
            r.warnings.push_back(
                "Header declares geographic nav but coordinate values are projected-sized");
        }
        r.needs_crs_review = !m_coords_projected_known || r.units_contradicted;
    } else {
        r.needs_crs_review = true;
    }

    close();
    r.success = true;
    return r;
}

} // namespace dolphin::io
