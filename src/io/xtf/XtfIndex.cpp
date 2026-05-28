// XtfIndex.cpp — XtfReader::buildIndex implementation

#include "io/xtf/XtfReader.h"
#include "io/xtf/XtfReader_p.h"
#include "io/FileIo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace dolphin::io {

core::ArtifactIndex XtfReader::buildIndex(ProgressFn progress)
{
    if (!m_file) return {};

    clearDiagnostics();

    using Sev  = core::ImportDiagnosticSeverity;
    using Code = core::ImportDiagnosticCode;

    // Hex-format a file offset for diagnostic messages.
    auto fmtHex = [](uint64_t v) -> std::string {
        char buf[20];
        std::snprintf(buf, sizeof(buf), "0x%llX",
                      static_cast<unsigned long long>(v));
        return buf;
    };

    // Emit one diagnostic per channel with unknown BPS so the user knows
    // inference is happening before the first ping is processed.
    for (size_t ci = 0; ci < m_chan_info.size(); ++ci) {
        if (m_chan_info[ci].bytes_per_sample_unknown) {
            addDiagnostic(Sev::Info, Code::InferredBytesPerSample,
                 "Channel " + std::to_string(ci)
                 + " (" + m_chan_info[ci].channel_name + ")"
                 + ": BytesPerSample not declared in file header;"
                 + " will infer from record geometry");
        }
    }

    core::ArtifactIndex index;
    index.source_id = m_path;

    if (!detail::seekAbs(m_file, m_dataStartOffset))
        return {};

    uint64_t artifact_id = 0;
    double   first_ts    = -1.0;
    double   last_ts     = 0.0;

    // GPS fixes collected from PACKET_NAV records and from PACKET_PING headers
    // that carry non-zero coordinates.  Used after the scan to backfill
    // lat/lon for the majority of pings whose headers contain (0, 0).
    // Written directly to m_nav_table so readArtifact can reuse them without
    // a second file scan when the project is loaded from a saved JSON index.
    m_nav_table.fixes.clear();
    auto& nav_fixes = m_nav_table.fixes;

    // Resync-tracking: collapse multiple bad-magic advances into one diagnostic.
    uint64_t resync_start = 0;
    bool     in_resync    = false;

    while (true) {
        uint64_t offset = 0;
        if (!detail::tellFile(m_file, offset))
            break;
        if (offset >= m_fileSize) break;

        if (progress && m_fileSize > 0)
            progress(static_cast<float>(offset) / static_cast<float>(m_fileSize));

        XtfPacketHeader pkt{};
        if (fread(&pkt, sizeof(pkt), 1, m_file) != 1) break;

        if (pkt.MagicNumber != XTF_MAGIC) {
            if (!in_resync) { resync_start = offset; in_resync = true; }
            if (!detail::seekAbs(m_file, offset + 1)) break;
            continue;
        }

        // Valid magic found — emit one resync diagnostic for the whole skipped region.
        if (in_resync) {
            addDiagnostic(Sev::Warning, Code::ResyncedPacket,
                 "Bad packet magic starting at " + fmtHex(resync_start)
                 + "; resynced at " + fmtHex(offset)
                 + " after skipping " + std::to_string(offset - resync_start) + " bytes",
                 resync_start);
            in_resync = false;
        }

        uint32_t record_bytes = pkt.NumBytesThisRecord;
        if (record_bytes < sizeof(XtfPacketHeader)) break;

        // Sanity: reject implausibly large records (> 64 MiB) to handle corrupt files.
        if (record_bytes > 64u * 1024u * 1024u) {
            addDiagnostic(Sev::Warning, Code::ImplausibleRecordSize,
                 "Record at " + fmtHex(offset) + " declares "
                 + std::to_string(record_bytes) + " bytes (> 64 MiB); skipping",
                 offset);
            if (!detail::seekAbs(m_file, offset + 1)) break;
            continue;
        }

        // Guard against seeking past EOF on truncated files.
        if (offset + record_bytes > m_fileSize) {
            addDiagnostic(Sev::Warning, Code::TruncatedRecord,
                 "Record at " + fmtHex(offset) + " declares "
                 + std::to_string(record_bytes) + " bytes but only "
                 + std::to_string(m_fileSize - offset) + " remain in file",
                 offset);
            break;
        }

        if (pkt.HeaderType == PACKET_PING && pkt.NumChansToFollow > 0) {
            int64_t ts_us = packetTimestampUs(pkt);
            double  ts_s  = ts_us * 1e-6;
            if (first_ts < 0.0) first_ts = ts_s;
            last_ts = ts_s;

            // Nav fallback: prefer sensor position (acquisition system already
            // applies layback); fall back to ship position when absent.
            double lat = pkt.SensorYcoordinate;
            double lon = pkt.SensorXcoordinate;
            if (lat == 0.0 && lon == 0.0) {
                lat = pkt.ShipYcoordinate;
                lon = pkt.ShipXcoordinate;
            }
            // Collect as a nav anchor for post-scan interpolation.
            if (hasUsableCoordinate(lat, lon))
                nav_fixes.push_back({ts_us, lat, lon});

            const uint64_t record_end = offset + record_bytes;
            uint64_t chan_offset = offset + sizeof(XtfPacketHeader);

            // Determine bps once for the whole packet using record geometry.
            // Read the first channel header to get a representative NumSamples.
            uint32_t packet_samples_per_chan = 0;
            {
                uint64_t first_chan_off = chan_offset;
                if (first_chan_off + sizeof(XtfPingChanHeader) <= record_end) {
                    if (detail::seekAbs(m_file, first_chan_off)) {
                        XtfPingChanHeader first_hdr{};
                        if (fread(&first_hdr, sizeof(first_hdr), 1, m_file) == 1) {
                            packet_samples_per_chan = first_hdr.NumSamples;
                            if (packet_samples_per_chan == 0) {
                                // Fall back to SamplesPerChannel from file header.
                                uint16_t cn = first_hdr.ChannelNumber;
                                if (cn < static_cast<uint16_t>(m_chan_info.size()))
                                    packet_samples_per_chan = m_chan_info[cn].samples_per_channel;
                            }
                        }
                    }
                }
            }

            // Resolve bps: use file header if known, else infer from record geometry.
            auto packetBps = [&](uint16_t chan_number) -> uint16_t {
                if (chan_number < static_cast<uint16_t>(m_chan_info.size())
                    && !m_chan_info[chan_number].bytes_per_sample_unknown) {
                    return m_chan_info[chan_number].bytes_per_sample;
                }
                const uint16_t inferred = inferBpsFromRecord(
                    record_bytes, pkt.NumChansToFollow, packet_samples_per_chan);
                return inferred ? inferred : 1;
            };

            for (uint16_t chan_idx = 0; chan_idx < pkt.NumChansToFollow; ++chan_idx) {
                if (chan_offset + sizeof(XtfPingChanHeader) > record_end)
                    break;

                if (!detail::seekAbs(m_file, chan_offset))
                    break;

                XtfPingChanHeader chan_hdr{};
                if (fread(&chan_hdr, sizeof(chan_hdr), 1, m_file) != 1)
                    break;

                const uint16_t chan_number = chan_hdr.ChannelNumber;

                // Resolve the m_chan_info index once for all lookups in this channel.
                // Edgetech 4200-style dual-frequency files reuse chan numbers 0/1 for
                // every packet and use pkt.SubChannelNumber to identify the frequency
                // band.  The adjusted index maps to the correct channel-info entry.
                const uint16_t chan_meta_idx = [&]() -> uint16_t {
                    if (pkt.SubChannelNumber > 0) {
                        const uint16_t adj = chan_number
                            + static_cast<uint16_t>(pkt.SubChannelNumber)
                            * pkt.NumChansToFollow;
                        if (adj < static_cast<uint16_t>(m_chan_info.size()))
                            return adj;
                    }
                    return chan_number;
                }();

                const uint64_t samples_offset = chan_offset + sizeof(XtfPingChanHeader);
                if (samples_offset > record_end)
                    break;

                // NumSamples == 0 means the writer relied on SamplesPerChannel in the
                // file header.  Fall back to that value.
                uint32_t num_samples = chan_hdr.NumSamples;
                if (num_samples == 0
                    && chan_meta_idx < static_cast<uint16_t>(m_chan_info.size())) {
                    num_samples = m_chan_info[chan_meta_idx].samples_per_channel;
                }

                const uint16_t bps = packetBps(chan_meta_idx);

                uint64_t sample_bytes = static_cast<uint64_t>(num_samples) * bps;
                if (samples_offset + sample_bytes > record_end)
                    sample_bytes = record_end - samples_offset;

                if (sample_bytes > 0) {
                    if (auto art_type = classifyChannel(chan_meta_idx)) {
                        core::ArtifactIndexEntry entry;
                        entry.artifact_id  = artifact_id++;
                        entry.type         = *art_type;
                        entry.timestamp_us = ts_us;
                        entry.file_offset  = offset;
                        entry.subrecord_offset =
                            static_cast<uint32_t>(chan_offset - offset);
                        entry.byte_length  = static_cast<uint32_t>(
                            sizeof(XtfPingChanHeader) + sample_bytes);
                        entry.lat          = lat;
                        entry.lon          = lon;
                        entry.ping_number  = pkt.PingNumber;
                        if (chan_hdr.Frequency > 0) {
                            entry.frequency_hz = (chan_hdr.Frequency > 2000u)
                                ? static_cast<float>(chan_hdr.Frequency)
                                : static_cast<float>(chan_hdr.Frequency) * 1000.f;
                        } else if (chan_meta_idx < static_cast<uint16_t>(m_chan_info.size())) {
                            entry.frequency_hz = m_chan_info[chan_meta_idx].frequency_hz;
                        }
                        entry.spatial_ref_kind = m_meta.coordinate_ref.kind;
                        entry.is_projected = m_coords_projected;
                        index.entries.push_back(entry);
                    }
                }

                chan_offset = samples_offset + sample_bytes;
            }

            // Emit a magnetometer sample if the packet carries mag data.
            bool has_mag = (pkt.MagX != 0.f || pkt.MagY != 0.f || pkt.MagZ != 0.f);
            if (has_mag) {
                core::ArtifactIndexEntry mag_entry;
                mag_entry.artifact_id  = artifact_id++;
                mag_entry.type         = core::ArtifactType::Magnetometer;
                mag_entry.timestamp_us = ts_us;
                mag_entry.file_offset  = offset;
                mag_entry.subrecord_offset = 0;
                mag_entry.byte_length  = record_bytes;
                mag_entry.lat          = lat;
                mag_entry.lon          = lon;
                mag_entry.spatial_ref_kind = m_meta.coordinate_ref.kind;
                mag_entry.is_projected = m_coords_projected;
                index.entries.push_back(mag_entry);
            }
        } else if (pkt.HeaderType == PACKET_NAV) {
            // Dedicated navigation packet — always carries a GPS fix.
            int64_t ts_us = packetTimestampUs(pkt);
            double  nlat  = pkt.SensorYcoordinate;
            double  nlon  = pkt.SensorXcoordinate;
            if (nlat == 0.0 && nlon == 0.0) {
                nlat = pkt.ShipYcoordinate;
                nlon = pkt.ShipXcoordinate;
            }
            if (hasUsableCoordinate(nlat, nlon))
                nav_fixes.push_back({ts_us, nlat, nlon});
        }

        if (!detail::seekAbs(m_file, offset + record_bytes))
            break;
    }

    // Mark as loaded so readArtifact won't trigger a redundant second scan.
    m_nav_table.loaded = true;

    // Backfill lat/lon for entries that had zero coordinates in their ping
    // packet headers.  Linearly interpolate from the collected nav_fixes table.
    if (!nav_fixes.empty()) {
        std::sort(nav_fixes.begin(), nav_fixes.end(),
                  [](const XtfNavFix& a, const XtfNavFix& b) {
                      return a.timestamp_us < b.timestamp_us;
                  });

        const core::SpatialRef inferred_ref =
            coordinateRefFromMagnitude(nav_fixes.front().lat, nav_fixes.front().lon);

        if (!m_coords_projected_known) {
            // NavUnits was ambiguous — infer from coordinate magnitudes.
            m_coords_projected = core::spatialRefIsProjected(inferred_ref);
            m_coords_projected_known = true;
            m_meta.coordinate_ref = inferred_ref;
            addDiagnostic(Sev::Info, Code::CoordinateSystemInferred,
                 std::string("NavUnits value is ambiguous; inferred ")
                 + (m_coords_projected ? "Projected" : "Geographic")
                 + " coordinate system from coordinate magnitudes");
        } else if (m_coords_projected) {
            // Symmetric validation: some raw XTF writers set NavUnits=3 even
            // though packet coordinates are already geographic degrees. If the
            // actual fix magnitudes look like lat/lon, trust the data over the
            // header so map placement does not collapse near zero.
            if (core::spatialRefIsGeographic(inferred_ref)) {
                m_coords_projected = false;
                m_meta.coordinate_ref = inferred_ref;
                addDiagnostic(Sev::Warning, Code::CoordinateSystemOverridden,
                     "NavUnits declared Projected, but coordinate magnitudes fit"
                     " WGS-84 bounds; overriding to Geographic");
            } else if (m_meta.coordinate_ref.empty()) {
                m_meta.coordinate_ref =
                    coordinateRefFromFlags(m_coords_projected, m_coords_projected_known);
            }
        } else if (!m_coords_projected) {
            // NavUnits claimed geographic — verify against actual magnitudes.
            // Many XTF writers set NavUnits=0 but store UTM/projected coordinates.
            if (core::spatialRefIsProjected(inferred_ref)) {
                m_coords_projected = true;
                m_meta.coordinate_ref = inferred_ref;
                addDiagnostic(Sev::Warning, Code::CoordinateSystemOverridden,
                     "NavUnits declared Geographic, but coordinate magnitudes exceed"
                     " WGS-84 bounds; overriding to Projected");
            }
        } else if (m_meta.coordinate_ref.empty()) {
            m_meta.coordinate_ref =
                coordinateRefFromFlags(m_coords_projected, m_coords_projected_known);
        }

        uint32_t nav_interpolated = 0;
        uint32_t nav_extrapolated = 0;

        for (auto& e : index.entries) {
            if (hasUsableCoordinate(e.lat, e.lon)) {
                e.spatial_ref_kind = m_meta.coordinate_ref.kind;
                e.is_projected = m_coords_projected;
                continue;
            }

            // Find first fix with timestamp >= e.timestamp_us.
            auto it = std::lower_bound(
                nav_fixes.begin(), nav_fixes.end(), e.timestamp_us,
                [](const XtfNavFix& f, int64_t ts) { return f.timestamp_us < ts; });

            if (it == nav_fixes.end()) {
                // Beyond last known fix — extrapolate with the last fix.
                ++nav_extrapolated;
                e.lat = nav_fixes.back().lat;
                e.lon = nav_fixes.back().lon;
            } else if (it == nav_fixes.begin()) {
                // Before (or at) the first fix — extrapolate with the first fix.
                ++nav_extrapolated;
                e.lat = nav_fixes.front().lat;
                e.lon = nav_fixes.front().lon;
            } else {
                // Straddle two fixes — linear interpolation.
                ++nav_interpolated;
                const auto& a = *(it - 1);
                const auto& b = *it;
                const double dt = static_cast<double>(b.timestamp_us - a.timestamp_us);
                const double t  = (dt > 0.0)
                    ? static_cast<double>(e.timestamp_us - a.timestamp_us) / dt
                    : 0.0;
                e.lat = a.lat + t * (b.lat - a.lat);
                e.lon = a.lon + t * (b.lon - a.lon);
            }
            e.spatial_ref_kind = m_meta.coordinate_ref.kind;
            e.is_projected = m_coords_projected;
        }

        if (nav_interpolated > 0)
            addDiagnostic(Sev::Info, Code::InterpolatedNavigation,
                 std::to_string(nav_interpolated)
                 + " artifact(s) used linearly interpolated navigation");
        if (nav_extrapolated > 0)
            addDiagnostic(Sev::Warning, Code::ExtrapolatedNavigation,
                 std::to_string(nav_extrapolated)
                 + " artifact(s) used extrapolated navigation"
                 + " (beyond available fix coverage)");

    } else if (m_meta.coordinate_ref.empty()) {
        m_meta.coordinate_ref =
            coordinateRefFromFlags(m_coords_projected, m_coords_projected_known);
        for (auto& e : index.entries)
            e.spatial_ref_kind = m_meta.coordinate_ref.kind;
    }

    m_meta.artifact_count        = static_cast<uint32_t>(index.entries.size());
    m_meta.sidescan_ping_count   = static_cast<uint32_t>(index.byType(core::ArtifactType::Sidescan).size());
    m_meta.subbottom_trace_count = static_cast<uint32_t>(index.byType(core::ArtifactType::SubBottom).size());
    m_meta.mag_sample_count      = static_cast<uint32_t>(index.byType(core::ArtifactType::Magnetometer).size());
    m_meta.start_time     = first_ts;
    m_meta.end_time       = last_ts;

    // If readFileHeader() could not derive both frequencies from channel-info blocks,
    // derive them from the per-ping frequencies already recorded in the index.
    // Must also run when frequency_hz is set but low_frequency_hz is still 0:
    // the header may have reported only one band (e.g. the LF channel) while the
    // file actually contains two bands — the entry scan is the only way to detect
    // the second band and set low_frequency_hz so the dual-freq split fires.
    if (m_meta.frequency_hz == 0.f || m_meta.low_frequency_hz == 0.f) {
        std::vector<float> sss_freqs;
        for (const auto& e : index.entries) {
            if (e.type == core::ArtifactType::Sidescan && e.frequency_hz > 0.f) {
                bool found = false;
                for (float f : sss_freqs)
                    if (std::fabs(f - e.frequency_hz) < 1.f) { found = true; break; }
                if (!found) sss_freqs.push_back(e.frequency_hz);
            }
        }
        std::sort(sss_freqs.begin(), sss_freqs.end(), std::greater<float>());
        if (!sss_freqs.empty()) {
            m_meta.frequency_hz = sss_freqs[0];
            if (sss_freqs.size() >= 2)
                m_meta.low_frequency_hz = sss_freqs.back();
        }
    }

    // For unambiguously single-frequency files, stamp every zero-frequency
    // sidescan entry with the file frequency.  This ensures entries carry a
    // valid frequency even when the writer left XtfPingChanHeader::Frequency=0
    // and the m_chan_info lookup returned 0 (e.g. chan number out of range).
    if (m_meta.frequency_hz > 0.f && m_meta.low_frequency_hz == 0.f) {
        for (auto& e : index.entries) {
            if (e.type == core::ArtifactType::Sidescan && e.frequency_hz == 0.f)
                e.frequency_hz = m_meta.frequency_hz;
        }
    }

    return index;
}

} // namespace dolphin::io
