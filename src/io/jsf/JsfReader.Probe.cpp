// JsfReader.Probe.cpp — bounded, structurally validated scan for the import wizard.
#include "io/jsf/JsfReader_p.h"
#include "io/ProbeResult.h"

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace dolphin::io {

using namespace detail_jsf;

ProbeResult JsfReader::probe(const std::string& path)
{
    ProbeResult r;
    r.path        = path;
    r.format_name = "JSF";

    if (!open(path)) {
        r.error_message = "Cannot open JSF file or invalid first message frame";
        return r;
    }
    r.file_size_bytes = static_cast<int64_t>(m_fileSize);

    if (!detail::seekAbs(m_file, 0)) {
        close();
        r.error_message = "Cannot seek in JSF file";
        return r;
    }

    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double min_y = std::numeric_limits<double>::max();
    double max_y = std::numeric_limits<double>::lowest();
    int coord_count = 0;
    int framed_messages = 0;
    int valid_sonar_messages = 0;
    bool saw_geographic = false;
    bool saw_projected  = false;
    bool has_sbp = false;
    bool has_sss = false;

    float  hdg_min = std::numeric_limits<float>::max();
    float  hdg_max = std::numeric_limits<float>::lowest();
    double hdg_sum = 0.0;
    int    hdg_n   = 0;

    std::map<std::pair<uint8_t, uint8_t>, ProbeResult::ChannelInfo> chan_map;
    std::string first_sonar_error;
    std::string structural_error;

    while (framed_messages < 200) {
        uint64_t offset = 0;
        if (!detail::tellFile(m_file, offset)) {
            structural_error = "Cannot read JSF message offset";
            break;
        }
        if (offset >= m_fileSize) break;
        if (m_fileSize - offset < sizeof(JsfPacketHeader)) {
            structural_error = "Truncated JSF message header at byte "
                             + std::to_string(offset);
            break;
        }

        JsfPacketHeader pkt{};
        if (std::fread(&pkt, sizeof(pkt), 1, m_file) != 1) {
            structural_error = "Cannot read JSF message header at byte "
                             + std::to_string(offset);
            break;
        }
        if (pkt.marker != JSF_MARKER) {
            structural_error = "Invalid JSF message marker at byte "
                             + std::to_string(offset);
            break;
        }
        if (pkt.size > kMaxRecordSz) {
            structural_error = "Implausible JSF message size at byte "
                             + std::to_string(offset);
            break;
        }
        const uint64_t body_start = offset + sizeof(JsfPacketHeader);
        if (static_cast<uint64_t>(pkt.size) > m_fileSize - body_start) {
            structural_error = "Truncated JSF message body at byte "
                             + std::to_string(offset);
            break;
        }
        const uint64_t next = body_start + pkt.size;
        ++framed_messages;

        if (pkt.type == MSG_SONAR) {
            if (pkt.size < kPingHdrSize) {
                if (first_sonar_error.empty())
                    first_sonar_error = "JSF sonar message is shorter than 240 bytes";
            } else {
                JsfSonarPingHeader ph{};
                if (std::fread(&ph, sizeof(ph), 1, m_file) != 1) {
                    structural_error = "Cannot read JSF sonar header at byte "
                                     + std::to_string(body_start);
                    break;
                }

                JsfSampleLayout layout;
                std::string layout_error;
                if (!supportsSubsystem(pkt.subsystem)) {
                    if (first_sonar_error.empty()) {
                        first_sonar_error = "unsupported JSF sonar subsystem "
                                          + std::to_string(pkt.subsystem);
                    }
                } else if (!sampleLayout(
                               ph, pkt.version, pkt.size, layout, &layout_error)) {
                    if (first_sonar_error.empty()) first_sonar_error = layout_error;
                } else {
                    ++valid_sonar_messages;
                    const auto artifact_type = classifySubsystem(pkt.subsystem);
                    if (artifact_type == core::ArtifactType::SubBottom) has_sbp = true;
                    else                                                has_sss = true;

                    const JsfCoordinate pos = coordinate(ph);
                    if (pos.valid && coord_count < 50) {
                        r.coord_valid = true;
                        if (r.coord_samples.size() < 5)
                            r.coord_samples.push_back({pos.lon, pos.lat});
                        min_x = std::min(min_x, pos.lon);
                        max_x = std::max(max_x, pos.lon);
                        min_y = std::min(min_y, pos.lat);
                        max_y = std::max(max_y, pos.lat);
                        ++coord_count;
                        saw_projected  = saw_projected  || pos.projected;
                        saw_geographic = saw_geographic || !pos.projected;
                    }

                    const float hdg = headingDeg(ph);
                    if ((validityFlags(ph) & (1u << 3))
                            && std::isfinite(hdg) && hdg >= 0.f && hdg <= 360.f) {
                        hdg_min = std::min(hdg_min, hdg);
                        hdg_max = std::max(hdg_max, hdg);
                        hdg_sum += hdg;
                        ++hdg_n;
                    }

                    const auto key = std::make_pair(pkt.subsystem, pkt.channel);
                    if (!chan_map.contains(key)) {
                        ProbeResult::ChannelInfo ci;
                        if (artifact_type == core::ArtifactType::SubBottom) {
                            ci.name     = "Sub-Bottom";
                            ci.modality = "Sub-Bottom";
                        } else if (pkt.channel == CHANNEL_PORT) {
                            ci.name     = "Port Sidescan";
                            ci.modality = "Sidescan";
                        } else if (pkt.channel == CHANNEL_STBD) {
                            ci.name     = "Starboard Sidescan";
                            ci.modality = "Sidescan";
                        } else {
                            ci.name     = "Sidescan channel " + std::to_string(pkt.channel);
                            ci.modality = "Sidescan";
                        }
                        ci.frequency_khz = static_cast<float>(frequencyHz(ph, pkt.version)) / 1000.f;
                        ci.samples_per_ping = layout.count;
                        const uint32_t interval_ns = sampleIntervalNs(ph);
                        ci.range_m = interval_ns > 0
                            ? static_cast<float>(layout.count) * interval_ns * 1.0e-9f
                                * 1500.0f * 0.5f
                            : 0.0f;
                        chan_map.emplace(key, std::move(ci));
                    }
                }
            }
        }

        if (!detail::seekAbs(m_file, next)) {
            structural_error = "Cannot seek to the next JSF message";
            break;
        }
    }

    close();

    if (valid_sonar_messages == 0) {
        if (!structural_error.empty()) r.error_message = structural_error;
        else if (!first_sonar_error.empty()) r.error_message = first_sonar_error;
        else r.error_message = "JSF contains no supported sonar messages";
        return r;
    }

    if (!structural_error.empty())
        r.warnings.push_back(structural_error);
    if (!first_sonar_error.empty())
        r.warnings.push_back(first_sonar_error);

    r.has_sidescan  = has_sss;
    r.has_subbottom = has_sbp;
    r.estimated_record_count = static_cast<uint32_t>(valid_sonar_messages);

    if (hdg_n > 0) {
        r.heading_valid = true;
        r.heading_min   = hdg_min;
        r.heading_max   = hdg_max;
        r.heading_mean  = static_cast<float>(hdg_sum / hdg_n);
    }

    for (auto& [key, ci] : chan_map) {
        (void)key;
        r.channels.push_back(std::move(ci));
    }

    if (r.coord_valid) {
        r.coord_min_x = min_x;
        r.coord_max_x = max_x;
        r.coord_min_y = min_y;
        r.coord_max_y = max_y;
    }

    if (saw_projected) {
        r.is_projected = true;
        r.declared_crs = core::makeUnknownProjectedSpatialRef("PROJECTED:JSF");
        r.needs_crs_review = true;
        if (saw_geographic)
            r.warnings.push_back("JSF mixes geographic and projected coordinates");
    } else if (saw_geographic) {
        r.declared_crs = core::makeWgs84SpatialRef();
    } else {
        r.needs_crs_review = true;
    }

    r.success = true;
    return r;
}

} // namespace dolphin::io
