// SegyReader.Decode.cpp — SegyReader::readArtifact.
#include "io/segy/SegyReader_p.h"
#include "core/SpatialRef.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace dolphin::io {

using namespace detail_segy;

std::optional<core::Artifact>
SegyReader::readArtifact(const core::ArtifactIndexEntry& entry)
{
    if (!m_file) return std::nullopt;
    if (entry.type != core::ArtifactType::SubBottom) return std::nullopt;
    if (entry.byte_length > 0
            && entry.file_offset + entry.byte_length > m_fileSize)
        return std::nullopt;

    if (!detail::seekAbs(m_file, entry.file_offset)) return std::nullopt;

    uint8_t thdr[kTraceHdrBytes];
    if (std::fread(thdr, kTraceHdrBytes, 1, m_file) != 1) return std::nullopt;

    uint32_t ns = static_cast<uint32_t>(rdUint16(&thdr[114], m_littleEndian));
    if (ns == 0) ns = m_fileSamplesPerTrace;
    if (ns == 0) return std::nullopt;

    const uint16_t si_us  = rdUint16(&thdr[116], m_littleEndian);
    const uint16_t eff_si = (si_us > 0) ? si_us : m_fileSampleInterval;
    if (eff_si == 0) return std::nullopt;

    const float sample_rate_hz = 1'000'000.f / static_cast<float>(eff_si);

    // Delay recording time (ms): time from shot to start of recorded trace.
    const int16_t delay_ms      = traceDelayMs(thdr, m_littleEndian);
    const float   delay_s       = std::max(0.f, static_cast<float>(delay_ms) * 0.001f);

    // Full two-way time window = delay + trace duration (follows XTF convention).
    const float two_way_time_s = delay_s + static_cast<float>(ns) / sample_rate_hz;

    // Navigation.
    double lon = 0.0, lat = 0.0;
    bool   is_proj = false;
    parseTraceCoords(thdr, m_littleEndian, lon, lat, is_proj);

    core::NavPoint nav;
    nav.lat          = lat;
    nav.lon          = lon;
    nav.timestamp    = entry.timestamp_us * 1e-6;
    nav.valid        = (lat != 0.0 || lon != 0.0);
    nav.is_projected = is_proj;
    nav.spatial_ref  = is_proj
        ? ((!m_meta.coordinate_ref.empty() && m_meta.coordinate_ref.exact)
           ? m_meta.coordinate_ref
           : core::makeUnknownProjectedSpatialRef("PROJECTED:SEGY"))
        : core::makeWgs84SpatialRef();

    // ── Decode amplitude samples (bulk read) ──────────────────────────────────
    core::SubBottomTrace trace;
    trace.id             = entry.artifact_id;
    trace.timestamp_us   = entry.timestamp_us;
    trace.nav            = nav;
    trace.frequency_hz   = m_meta.frequency_hz;
    trace.sample_rate_hz = sample_rate_hz;
    trace.two_way_time_s = two_way_time_s;
    trace.samples.resize(ns, 0.f);

    const uint32_t bps = bytesPerSample();
    std::vector<uint8_t> raw(static_cast<size_t>(ns) * bps);
    const uint32_t read_count = static_cast<uint32_t>(
        std::fread(raw.data(), bps, ns, m_file));
    if (read_count != ns) return std::nullopt;

    float peak = 0.f;

    switch (m_sampleFormat) {
    case 1: {  // IBM float — always big-endian regardless of file byte order
        for (uint32_t i = 0; i < ns; ++i) {
            const uint32_t ibm = (uint32_t{raw[4*i  ]} << 24)
                               | (uint32_t{raw[4*i+1]} << 16)
                               | (uint32_t{raw[4*i+2]} <<  8)
                               |  uint32_t{raw[4*i+3]};
            trace.samples[i] = ibmToIeee(ibm);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    case 2: {  // 32-bit signed integer
        for (uint32_t i = 0; i < ns; ++i) {
            const int32_t v = rdInt32(&raw[4*i], m_littleEndian);
            trace.samples[i] = static_cast<float>(v) * (1.f / 2147483648.f);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    case 3: {  // 16-bit signed integer
        for (uint32_t i = 0; i < ns; ++i) {
            const int16_t v = rdInt16(&raw[2*i], m_littleEndian);
            trace.samples[i] = static_cast<float>(v) * (1.f / 32768.f);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    case 5: {  // IEEE 32-bit float
        for (uint32_t i = 0; i < ns; ++i) {
            trace.samples[i] = rdf32(&raw[4*i], m_littleEndian);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    case 6: {  // IEEE 64-bit double → float
        for (uint32_t i = 0; i < ns; ++i) {
            trace.samples[i] = rdf64to32(&raw[8*i], m_littleEndian);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    case 7: {  // 24-bit signed integer
        for (uint32_t i = 0; i < ns; ++i) {
            const int32_t v = rdInt24(&raw[3*i], m_littleEndian);
            trace.samples[i] = static_cast<float>(v) * (1.f / 8388608.f);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    case 8: {  // 8-bit signed integer
        for (uint32_t i = 0; i < ns; ++i) {
            const int8_t v = static_cast<int8_t>(raw[i]);
            trace.samples[i] = static_cast<float>(v) * (1.f / 128.f);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    case 9: {  // 64-bit signed integer
        for (uint32_t i = 0; i < ns; ++i) {
            const int64_t v = rdInt64(&raw[8*i], m_littleEndian);
            trace.samples[i] = static_cast<float>(
                static_cast<double>(v) / 9223372036854775808.0);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    case 10: {  // 32-bit unsigned integer — centred at 2^31
        for (uint32_t i = 0; i < ns; ++i) {
            const uint32_t v = rdUint32(&raw[4*i], m_littleEndian);
            trace.samples[i] = (static_cast<float>(v) - 2147483648.f)
                              * (1.f / 2147483648.f);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    case 11: {  // 16-bit unsigned integer — centred at 2^15
        for (uint32_t i = 0; i < ns; ++i) {
            const uint16_t v = rdUint16(&raw[2*i], m_littleEndian);
            trace.samples[i] = (static_cast<float>(v) - 32768.f)
                              * (1.f / 32768.f);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    case 12: {  // 8-bit unsigned integer — centred at 2^7
        for (uint32_t i = 0; i < ns; ++i) {
            trace.samples[i] = (static_cast<float>(raw[i]) - 128.f)
                              * (1.f / 128.f);
            peak = std::max(peak, std::abs(trace.samples[i]));
        }
        break;
    }
    default:
        return std::nullopt;
    }

    if (ns == 0) return std::nullopt;

    // ── Amplitude normalisation ───────────────────────────────────────────────
    // Normalise to [-1, 1] only when samples exceed the unit range, so that
    // data already in [-1, 1] (e.g. format 3 int16) is untouched.
    // Use the 99th-percentile absolute value rather than the raw peak so that
    // a single outlier sample does not compress the entire trace display.
    if (peak > 1.f) {
        std::vector<float> abs_vals(ns);
        for (uint32_t i = 0; i < ns; ++i) abs_vals[i] = std::abs(trace.samples[i]);
        const size_t p99_idx = ns * 99 / 100;
        std::nth_element(abs_vals.begin(), abs_vals.begin() + p99_idx, abs_vals.end());
        const float p99 = abs_vals[p99_idx];
        const float inv = (p99 > 1e-6f) ? (1.f / p99) : (1.f / peak);
        for (auto& s : trace.samples)
            s = std::clamp(s * inv, -1.f, 1.f);
    }

    // ── Seabed first-return detection ─────────────────────────────────────────
    trace.bottom_sample_idx = detectBottomSample(
        trace.samples.data(), static_cast<int>(ns));

    return trace;
}

} // namespace dolphin::io
