// XtfPayload.cpp — XtfReader::readArtifact implementation

#include "io/xtf/XtfReader.h"
#include "io/xtf/XtfReader_p.h"
#include "io/AmplitudeScale.h"
#include "io/FileIo.h"
#include "io/detail/SubBottomDetect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace dolphin::io {

// Physical sound-velocity range for seawater (m/s).
// Values outside this window indicate a missing or corrupt SV field.
static constexpr float kSoundVelocityMin_mps     = 1350.f;
static constexpr float kSoundVelocityMax_mps     = 1700.f;
static constexpr float kSoundVelocityDefault_mps = 1500.f;

std::optional<core::Artifact>
XtfReader::readArtifact(const core::ArtifactIndexEntry& entry)
{
    if (!m_file) return std::nullopt;

    if (!detail::seekAbs(m_file, entry.file_offset))
        return std::nullopt;

    XtfPacketHeader pkt{};
    if (fread(&pkt, sizeof(pkt), 1, m_file) != 1) return std::nullopt;
    if (pkt.MagicNumber != XTF_MAGIC) return std::nullopt;

    int64_t ts_us = packetTimestampUs(pkt);

    // Both raw position sources are preserved separately so the user can choose
    // fish/sensor vs vessel/ship navigation at georeferencing time.
    const bool sensor_pos_ok = hasUsableCoordinate(pkt.SensorYcoordinate, pkt.SensorXcoordinate);
    const bool ship_pos_ok   = hasUsableCoordinate(pkt.ShipYcoordinate,   pkt.ShipXcoordinate);

    // Resolved best-available position (existing fallback order unchanged).
    double nav_lat = sensor_pos_ok ? pkt.SensorYcoordinate : pkt.ShipYcoordinate;
    double nav_lon = sensor_pos_ok ? pkt.SensorXcoordinate : pkt.ShipXcoordinate;

    // -- Magnetometer ----------------------------------------------------------
    if (entry.type == core::ArtifactType::Magnetometer) {
        core::MagSample mag;
        mag.id              = entry.artifact_id;
        mag.timestamp_us    = ts_us;
        mag.nav.lat = hasUsableCoordinate(nav_lat, nav_lon) ? nav_lat : entry.lat;
        mag.nav.lon = hasUsableCoordinate(nav_lat, nav_lon) ? nav_lon : entry.lon;
        if (mag.nav.lat == 0.0 && mag.nav.lon == 0.0) {
            ensureNavFixes();
            interpolateNav(ts_us, mag.nav.lat, mag.nav.lon);
        }
        mag.nav.fish_lat        = sensor_pos_ok ? pkt.SensorYcoordinate : 0.0;
        mag.nav.fish_lon        = sensor_pos_ok ? pkt.SensorXcoordinate : 0.0;
        mag.nav.fish_nav_valid  = sensor_pos_ok;
        mag.nav.vessel_lat      = ship_pos_ok ? pkt.ShipYcoordinate : 0.0;
        mag.nav.vessel_lon      = ship_pos_ok ? pkt.ShipXcoordinate : 0.0;
        mag.nav.vessel_nav_valid = ship_pos_ok;
        mag.nav.sensor_heading_deg = pkt.SensorHeading;
        mag.nav.ship_heading_deg   = pkt.ShipGyro;
        mag.nav.heading_deg  = (pkt.SensorHeading != 0.0f) ? pkt.SensorHeading : pkt.ShipGyro;
        mag.nav.speed_kn     = pkt.SensorSpeed;
        mag.nav.altitude_m   = pkt.SensorPrimaryAltitude;
        mag.nav.pitch_deg    = pkt.SensorPitch;
        mag.nav.roll_deg     = pkt.SensorRoll;
        mag.nav.heave_m      = pkt.Heave;
        mag.nav.timestamp    = ts_us * 1e-6;
        mag.nav.valid        = isFiniteCoordinate(mag.nav.lat, mag.nav.lon);
        mag.nav.spatial_ref  = !m_meta.coordinate_ref.empty()
            ? m_meta.coordinate_ref
            : coordinateRefFromFlags(entry.is_projected, true);
        mag.nav.is_projected = entry.is_projected;
        mag.x_nT     = pkt.MagX;
        mag.y_nT     = pkt.MagY;
        mag.z_nT     = pkt.MagZ;
        mag.total_nT = std::sqrt(pkt.MagX * pkt.MagX
                               + pkt.MagY * pkt.MagY
                               + pkt.MagZ * pkt.MagZ);
        return mag;
    }

    // -- Sonar ping (SSS or SBP) -----------------------------------------------
    const bool has_subrecord_offset =
        entry.subrecord_offset >= sizeof(XtfPacketHeader)
     && entry.subrecord_offset + sizeof(XtfPingChanHeader) <= pkt.NumBytesThisRecord;
    const uint64_t chan_offset = entry.file_offset
        + (has_subrecord_offset ? entry.subrecord_offset
                                : static_cast<uint32_t>(sizeof(XtfPacketHeader)));
    if (!detail::seekAbs(m_file, chan_offset))
        return std::nullopt;

    XtfPingChanHeader chan{};
    if (fread(&chan, sizeof(chan), 1, m_file) != 1) return std::nullopt;

    core::NavPoint nav;
    // Priority 1: packet header has explicit coords.
    // Priority 2: index entry has pre-interpolated coords (set by buildIndex).
    // Priority 3: lazy scan of file headers for PACKET_NAV fixes (existing
    //             projects loaded from JSON that pre-dates the interpolation).
    nav.lat = hasUsableCoordinate(nav_lat, nav_lon) ? nav_lat : entry.lat;
    nav.lon = hasUsableCoordinate(nav_lat, nav_lon) ? nav_lon : entry.lon;
    if (nav.lat == 0.0 && nav.lon == 0.0) {
        ensureNavFixes();
        interpolateNav(ts_us, nav.lat, nav.lon);
    }
    nav.fish_lat         = sensor_pos_ok ? pkt.SensorYcoordinate : 0.0;
    nav.fish_lon         = sensor_pos_ok ? pkt.SensorXcoordinate : 0.0;
    nav.fish_nav_valid   = sensor_pos_ok;
    nav.vessel_lat       = ship_pos_ok ? pkt.ShipYcoordinate : 0.0;
    nav.vessel_lon       = ship_pos_ok ? pkt.ShipXcoordinate : 0.0;
    nav.vessel_nav_valid = ship_pos_ok;
    nav.sensor_heading_deg = pkt.SensorHeading;
    nav.ship_heading_deg   = pkt.ShipGyro;
    // Resolved heading: towfish AHRS preferred; fall back to vessel compass.
    nav.heading_deg  = (pkt.SensorHeading != 0.0f) ? pkt.SensorHeading : pkt.ShipGyro;
    nav.speed_kn     = pkt.SensorSpeed;
    nav.altitude_m   = pkt.SensorPrimaryAltitude;
    nav.pitch_deg    = pkt.SensorPitch;
    nav.roll_deg     = pkt.SensorRoll;
    nav.heave_m      = pkt.Heave;
    nav.timestamp    = ts_us * 1e-6;
    nav.valid        = isFiniteCoordinate(nav.lat, nav.lon);
    nav.spatial_ref  = !m_meta.coordinate_ref.empty()
        ? m_meta.coordinate_ref
        : coordinateRefFromFlags(entry.is_projected, true);
    nav.is_projected = entry.is_projected;

    // Resolve the m_chan_info index for this channel.  Edgetech 4200-style dual-frequency
    // files reuse ChannelNumber 0/1 for every ping and use pkt.SubChannelNumber to
    // identify the frequency band, so the real channel info lives at an offset.
    const uint16_t chan_meta_idx = [&]() -> uint16_t {
        if (pkt.SubChannelNumber > 0) {
            const uint16_t adj = chan.ChannelNumber
                + static_cast<uint16_t>(pkt.SubChannelNumber) * pkt.NumChansToFollow;
            if (adj < static_cast<uint16_t>(m_chan_info.size()))
                return adj;
        }
        return chan.ChannelNumber;
    }();

    // NumSamples == 0 means the writer relied on SamplesPerChannel in the file header.
    uint32_t num_samples = chan.NumSamples;
    if (num_samples == 0
        && chan_meta_idx < static_cast<uint16_t>(m_chan_info.size())) {
        num_samples = m_chan_info[chan_meta_idx].samples_per_channel;
    }
    if (num_samples == 0) return std::nullopt;

    // Clamp to the bytes available in this channel so malformed headers do not
    // send us past the actual sample payload.
    const uint32_t payload_bytes = [&]() -> uint32_t {
        if (has_subrecord_offset) {
            if (entry.byte_length <= sizeof(XtfPingChanHeader)) return 0;
            return entry.byte_length - static_cast<uint32_t>(sizeof(XtfPingChanHeader));
        }

        const uint32_t legacy_header_bytes = static_cast<uint32_t>(
            sizeof(XtfPacketHeader) + sizeof(XtfPingChanHeader));
        if (entry.byte_length <= legacy_header_bytes) return 0;
        return entry.byte_length - legacy_header_bytes;
    }();
    if (payload_bytes == 0) return std::nullopt;

    // Resolve bps: use file header if known, else infer from whole-record geometry.
    uint16_t bps;
    if (chan_meta_idx < static_cast<uint16_t>(m_chan_info.size())
        && !m_chan_info[chan_meta_idx].bytes_per_sample_unknown) {
        bps = m_chan_info[chan_meta_idx].bytes_per_sample;
    } else {
        const uint16_t inferred = inferBpsFromRecord(
            pkt.NumBytesThisRecord, pkt.NumChansToFollow, num_samples);
        bps = inferred ? inferred : 1;
    }

    const uint32_t max_samples = (bps > 0) ? (payload_bytes / bps) : 0;
    num_samples = std::min(num_samples, max_samples);
    if (num_samples == 0) return std::nullopt;

    // -- Sub-bottom trace ------------------------------------------------------
    if (entry.type == core::ArtifactType::SubBottom) {
        core::SubBottomTrace trace;
        trace.id             = entry.artifact_id;
        trace.timestamp_us   = ts_us;
        trace.nav            = nav;
        trace.frequency_hz   = (chan.Frequency > 0)
            ? ((chan.Frequency > 2000u) ? static_cast<float>(chan.Frequency)
                                       : static_cast<float>(chan.Frequency) * 1000.f)
            : entry.frequency_hz;
        trace.sample_rate_hz = (chan.TimeDuration > 0.f)
                             ? static_cast<float>(num_samples) / chan.TimeDuration
                             : 0.f;
        trace.tow_depth_m    = pkt.SensorDepth;
        trace.two_way_time_s = (chan.TimeDelay + chan.TimeDuration);
        trace.samples.reserve(num_samples);

        for (uint32_t s = 0; s < num_samples; ++s) {
            float normalised = 0.f;
            if (bps == 1) {
                uint8_t raw = 0;
                if (fread(&raw, 1, 1, m_file) != 1) break;
                normalised = (static_cast<float>(raw) - 128.f) / 128.f;
            } else {
                uint16_t raw = 0;
                if (fread(&raw, 2, 1, m_file) != 1) break;
                normalised = (static_cast<float>(raw) - 32768.f) / 32768.f;
            }
            trace.samples.push_back(normalised);
        }
        trace.bottom_sample_idx = detectBottomSample(
            trace.samples.data(), static_cast<int>(trace.samples.size()));
        return trace;
    }

    // -- Sidescan ping ---------------------------------------------------------
    core::SidescanPing ping;
    ping.id             = entry.artifact_id;
    ping.timestamp_us   = ts_us;
    ping.ping_number    = pkt.PingNumber;
    ping.nav            = nav;
    // Prefer the per-ping channel header frequency; fall back to the index-entry
    // value which was resolved in buildIndex with the SubChannelNumber-adjusted
    // m_chan_info lookup.  This ensures the cache stores a non-zero frequency even
    // when the writer left XtfPingChanHeader::Frequency == 0 (e.g. Edgetech 4200E).
    if (chan.Frequency > 0) {
        ping.frequency_hz = (chan.Frequency > 2000u)
            ? static_cast<float>(chan.Frequency)
            : static_cast<float>(chan.Frequency) * 1000.f;
    } else {
        ping.frequency_hz = entry.frequency_hz;
    }
    ping.sample_rate_hz = (chan.TimeDuration > 0.f)
                        ? static_cast<float>(num_samples) / chan.TimeDuration
                        : 0.f;
    ping.slant_range_m  = (chan.SlantRange > 0.f) ? chan.SlantRange : chan.GroundRange;
    // Sound velocity: prefer measured, fall back to CTD-computed.
    // Reject values outside the physical range for water (1350–1700 m/s) — some
    // acquisition systems store 0, garbage, or half-speed values in this field.
    {
        const float raw_sv = (pkt.SoundVelocity > 0.f)
                           ? pkt.SoundVelocity
                           : pkt.ComputedSoundVelocity;
        ping.sound_velocity_ms = (raw_sv >= kSoundVelocityMin_mps &&
                                   raw_sv <= kSoundVelocityMax_mps) ? raw_sv : 0.f;
    }
    ping.tow_depth_m    = pkt.SensorDepth;
    // Blanking: first sample starts at TimeDelay seconds into the record window.
    // Compute the near-field dead-zone distance using the ping's own SV, or 1500 m/s.
    {
        const float sv = (ping.sound_velocity_ms > 0.f) ? ping.sound_velocity_ms
                                                         : kSoundVelocityDefault_mps;
        ping.blanking_m = chan.TimeDelay * sv * 0.5f;
    }
    ping.gain_code         = chan.GainCode;
    ping.initial_gain_code = chan.InitialGainCode;
    ping.bandwidth_hz      = static_cast<uint32_t>(chan.BandWidth) * 100u;
    if (chan_meta_idx < static_cast<uint16_t>(m_chan_info.size()))
        ping.volt_scale = m_chan_info[chan_meta_idx].volt_scale;
    // Towfish geometry
    ping.kp_m          = pkt.KP;
    ping.layback_m     = pkt.Layback;
    ping.cable_out_m   = pkt.CableOut * 0.1f + pkt.CableOutHundredths * 0.01f;
    ping.fish_delta_x_m = pkt.FishPositionDeltaX * 0.1f;
    ping.fish_delta_y_m = pkt.FishPositionDeltaY * 0.1f;

    ping.channel = sidescanChannel(chan.ChannelNumber, pkt.SubChannelNumber, pkt.NumChansToFollow);

    // For 32-bit samples: read raw bytes and auto-detect float32 vs uint32.
    if (bps == 4) {
        // Many writers get this wrong and put BytesPerSample=4 but mean 2 bytes per sample per channel
        // or something else. If the data looks like 16-bit, we can fall back to 2-byte reading.
        if (payload_bytes == num_samples * 2) {
            bps = 2; // Override BPS to 2 since payload matches 16-bit samples
        }

        if (bps == 4) {
            std::vector<uint32_t> raw32(num_samples, 0);
            if (fread(raw32.data(), sizeof(uint32_t), num_samples, m_file) != num_samples)
                return std::nullopt;

            // Probe float32 interpretation.
            const float* fptr = reinterpret_cast<const float*>(raw32.data());
            float fmax = 0.f;
            uint32_t finite_positive = 0;
            for (uint32_t s = 0; s < num_samples; ++s) {
                if (std::isfinite(fptr[s]) && fptr[s] > 0.f) {
                    if (fptr[s] > fmax) fmax = fptr[s];
                    ++finite_positive;
                }
            }
            const bool looks_like_float = (finite_positive > num_samples / 2)
                                       && (fmax > 1e-20f);

            ping.samples.resize(num_samples);

            std::vector<uint16_t> amp_buf(num_samples);
            if (looks_like_float)
                normalizeFloat32Samples(fptr, amp_buf.data(), num_samples);
            else
                normalizeUint32Samples(raw32.data(), amp_buf.data(), num_samples);

            for (uint32_t s = 0; s < num_samples; ++s) {
                ping.samples[s].amplitude = amp_buf[s];
                ping.samples[s].range_m   = (num_samples > 1)
                    ? ping.blanking_m + (ping.slant_range_m - ping.blanking_m)
                        * static_cast<float>(s) / static_cast<float>(num_samples - 1)
                    : ping.blanking_m;
            }

            // Port samples are far-to-near in XTF — reverse to nadir-outward.
            if (ping.channel == core::SidescanChannel::Port && ping.samples.size() > 1) {
                std::reverse(ping.samples.begin(), ping.samples.end());
                const uint32_t ns = static_cast<uint32_t>(ping.samples.size());
                for (uint32_t s = 0; s < ns; ++s)
                    ping.samples[s].range_m = ping.blanking_m
                        + (ping.slant_range_m - ping.blanking_m)
                        * static_cast<float>(s) / static_cast<float>(ns - 1);
            }
            return ping;
        }
    }

    ping.samples.reserve(num_samples);
    for (uint32_t s = 0; s < num_samples; ++s) {
        uint16_t raw = 0;
        if (bps == 1) {
            uint8_t b = 0;
            if (fread(&b, 1, 1, m_file) != 1) break;
            raw = static_cast<uint16_t>(b) * 257u;  // scale 0-255 → 0-65535
        } else {
            if (fread(&raw, 2, 1, m_file) != 1) break;
        }
        core::SidescanSample sample;
        sample.amplitude = raw;
        sample.range_m   = (num_samples > 1)
                         ? ping.blanking_m + (ping.slant_range_m - ping.blanking_m)
                             * static_cast<float>(s) / static_cast<float>(num_samples - 1)
                         : ping.blanking_m;
        ping.samples.push_back(sample);
    }

    // XTF convention: port samples are stored far-to-near (outermost range first),
    // starboard are stored nadir-to-far.  Reverse port so both are nadir-outward,
    // then recompute range_m in ascending order.
    if (ping.channel == core::SidescanChannel::Port && ping.samples.size() > 1) {
        std::reverse(ping.samples.begin(), ping.samples.end());
        const uint32_t ns = static_cast<uint32_t>(ping.samples.size());
        for (uint32_t s = 0; s < ns; ++s)
            ping.samples[s].range_m = ping.blanking_m
                + (ping.slant_range_m - ping.blanking_m)
                * static_cast<float>(s) / static_cast<float>(ns - 1);
    }

    return ping;
}

} // namespace dolphin::io
