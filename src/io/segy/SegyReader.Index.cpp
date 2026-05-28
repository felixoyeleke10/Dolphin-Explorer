// SegyReader.Index.cpp — SegyReader::buildIndex.
#include "io/segy/SegyReader_p.h"
#include "core/SpatialRef.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace dolphin::io {

using namespace detail_segy;

// ── buildIndex ────────────────────────────────────────────────────────────────

core::ArtifactIndex SegyReader::buildIndex(ProgressFn progress)
{
    if (!m_file) return {};

    clearDiagnostics();

    using Sev  = core::ImportDiagnosticSeverity;
    using Code = core::ImportDiagnosticCode;

    // ── Deferred open() diagnostics ────────────────────────────────────────────
    if (m_sampleFormatDefaulted) {
        addDiagnostic(Sev::Warning, Code::InferredBytesPerSample,
             "Binary file header format code is 0 (unset); defaulted to"
             " big-endian IEEE 32-bit float (format 5)");
    }

    if (m_extHdrScanFailed) {
        addDiagnostic(Sev::Warning, Code::ResyncedPacket,
             "Extended header count is -1 but ((SEG: EndText)) was not found;"
             " all candidate extended-header blocks conservatively skipped");
    }

    if (m_endianFromProbe) {
        const char* order  = m_littleEndian ? "little-endian" : "big-endian";
        const int   conf   = static_cast<int>(m_probeScore) * 10;
        addDiagnostic(Sev::Info, Code::EncodingInferred,
             std::string("Byte order resolved to ") + order
             + " (format " + std::to_string(m_sampleFormat) + ") by"
             " trace-chain probe (plausibility score "
             + std::to_string(m_probeScore) + "/10, confidence ~"
             + std::to_string(conf) + "%)",
             0, 0, 0, static_cast<uint8_t>(std::min(conf, 100)));
    }

    auto fmtHex = [](uint64_t v) -> std::string {
        char buf[20];
        std::snprintf(buf, sizeof(buf), "0x%llX",
                      static_cast<unsigned long long>(v));
        return buf;
    };

    core::ArtifactIndex index;
    index.source_id = m_path;

    // ── CRS from text header (decoded in open()) ───────────────────────────────
    {
        const core::SpatialRef text_crs = crsFromTextHeader(m_textHeaderDecoded);
        if (!text_crs.empty())
            m_meta.coordinate_ref = text_crs;
    }

    if (!detail::seekAbs(m_file, m_dataOffset)) return {};

    const uint32_t bps = bytesPerSample();

    uint64_t artifact_id      = 0;
    double   first_ts         = -1.0;
    double   last_ts          =  0.0;
    bool     any_projected    = false;
    bool     any_swapped      = false;
    int      consecutive_bad  = 0;
    bool     ns_mismatch_reported       = false;
    bool     units_contradiction_reported = false;
    constexpr int kMaxBad     = 32;

    uint8_t thdr[kTraceHdrBytes];

    while (true) {
        uint64_t trace_offset = 0;
        if (!detail::tellFile(m_file, trace_offset)) break;
        if (trace_offset + kTraceHdrBytes > m_fileSize) break;

        if (progress && m_fileSize > m_dataOffset) {
            const float frac = static_cast<float>(trace_offset - m_dataOffset)
                             / static_cast<float>(m_fileSize - m_dataOffset);
            progress(std::min(frac, 1.f));
        }

        if (std::fread(thdr, kTraceHdrBytes, 1, m_file) != 1) break;

        // ── Validate geometry ──────────────────────────────────────────────────
        uint32_t ns = static_cast<uint32_t>(rdUint16(&thdr[114], m_littleEndian));
        if (ns == 0) ns = m_fileSamplesPerTrace;

        const uint16_t si_us  = rdUint16(&thdr[116], m_littleEndian);
        const uint16_t eff_si = (si_us > 0) ? si_us : m_fileSampleInterval;

        if (ns == 0 || eff_si == 0) {
            // Geometry looks corrupt — try to recover with the resync scanner.
            addDiagnostic(Sev::Warning, Code::ResyncedPacket,
                 "Trace at " + fmtHex(trace_offset)
                 + " has ns=" + std::to_string(ns)
                 + " si=" + std::to_string(si_us)
                 + "; scanning forward for next plausible trace header",
                 trace_offset);
            ++consecutive_bad;
            if (consecutive_bad >= kMaxBad) break;

            const uint64_t scan_start = trace_offset + kTraceHdrBytes;
            const uint64_t found = scanForNextTrace(m_file, scan_start, m_fileSize,
                                                     m_littleEndian,
                                                     m_fileSamplesPerTrace,
                                                     m_fileSampleInterval);
            if (found == UINT64_MAX) break;
            if (!detail::seekAbs(m_file, found)) break;
            continue;
        }

        const uint64_t sample_bytes = static_cast<uint64_t>(ns) * bps;
        const uint64_t trace_total  = kTraceHdrBytes + sample_bytes;
        if (trace_offset + trace_total > m_fileSize) {
            addDiagnostic(Sev::Warning, Code::TruncatedRecord,
                 "Trace at " + fmtHex(trace_offset) + " declares "
                 + std::to_string(trace_total) + " bytes but only "
                 + std::to_string(m_fileSize - trace_offset) + " remain in file;"
                 " indexing stops here",
                 trace_offset);
            break;
        }

        consecutive_bad = 0;

        // ── Samples-per-trace mismatch (report once) ───────────────────────────
        {
            const uint32_t raw_ns = static_cast<uint32_t>(rdUint16(&thdr[114], m_littleEndian));
            if (!ns_mismatch_reported
                    && m_fileSamplesPerTrace > 0
                    && raw_ns > 0
                    && raw_ns != m_fileSamplesPerTrace) {
                addDiagnostic(Sev::Info, Code::SamplesPerTraceMismatch,
                     "Binary file header declares "
                     + std::to_string(m_fileSamplesPerTrace)
                     + " samples/trace; trace at " + fmtHex(trace_offset)
                     + " declares " + std::to_string(raw_ns)
                     + "; using per-trace value (file header value ignored)",
                     trace_offset);
                ns_mismatch_reported = true;
            }
        }

        // ── Trace identification filter ────────────────────────────────────────
        const int16_t ident = traceIdentCode(thdr, m_littleEndian);
        if (!isSeismicTrace(ident)) {
            if (!detail::seekAbs(m_file, trace_offset + trace_total)) break;
            continue;
        }

        // ── Timestamp ─────────────────────────────────────────────────────────
        const int64_t ts_us = traceTimestampUs(thdr, m_littleEndian);
        const double  ts_s  = ts_us * 1e-6;
        if (ts_us > 0) {
            if (first_ts < 0.0) first_ts = ts_s;
            last_ts = ts_s;
        }

        // ── Coordinates (extended parser with confidence) ──────────────────────
        const CoordResult coord = parseTraceCoordsEx(thdr, m_littleEndian);
        if (coord.is_projected) any_projected = true;
        if (coord.units_contradicted && !units_contradiction_reported) {
            units_contradiction_reported = true;
            addDiagnostic(Sev::Warning, Code::CoordinateSystemOverridden,
                 "Trace at " + fmtHex(trace_offset)
                 + ": coordinate units field declares geographic (decimal degrees)"
                 " but values X=" + std::to_string(coord.lon)
                 + " Y=" + std::to_string(coord.lat)
                 + " exceed WGS-84 bounds — treating as projected metres."
                 " Set a CRS override if the projection is known.",
                 trace_offset);
        }
        if (coord.possibly_swapped && !any_swapped) {
            any_swapped = true;
            addDiagnostic(Sev::Warning, Code::CoordinateSwappedXY,
                 "Trace at " + fmtHex(trace_offset)
                 + ": X=" + std::to_string(coord.lon)
                 + " Y=" + std::to_string(coord.lat)
                 + " with geographic units — X is in latitude range (≤90°) but Y"
                 " is outside latitude range; X/Y fields may be transposed by the"
                 " exporting software",
                 trace_offset);
        }
        if (coord.confidence == CoordConfidence::None && artifact_id < 3) {
            addDiagnostic(Sev::Info, Code::MissingNavigation,
                 "Trace at " + fmtHex(trace_offset)
                 + " has zero source, group, and CDP coordinates;"
                 " position will be unavailable for this record",
                 trace_offset);
        }

        core::ArtifactIndexEntry entry;
        entry.artifact_id      = artifact_id++;
        entry.type             = core::ArtifactType::SubBottom;
        entry.timestamp_us     = ts_us;
        entry.file_offset      = trace_offset;
        entry.byte_length      = static_cast<uint32_t>(
            std::min(trace_total, static_cast<uint64_t>(UINT32_MAX)));
        entry.frequency_hz     = m_meta.frequency_hz;
        entry.lat              = coord.lat;
        entry.lon              = coord.lon;
        entry.is_projected     = coord.is_projected;
        entry.spatial_ref_kind = coord.is_projected
            ? core::SpatialRefKind::Projected
            : core::SpatialRefKind::Geographic;

        index.entries.push_back(entry);

        if (!detail::seekAbs(m_file, trace_offset + trace_total)) break;
    }

    if (progress) progress(1.f);

    m_meta.artifact_count        = static_cast<uint32_t>(index.entries.size());
    m_meta.subbottom_trace_count = m_meta.artifact_count;
    m_meta.start_time     = (first_ts >= 0.0) ? first_ts : 0.0;
    m_meta.end_time       = last_ts;

    // Only upgrade to a projected CRS hint if text-header parsing did not
    // already give us a confirmed CRS.
    if (any_projected && m_meta.coordinate_ref.empty()) {
        m_meta.coordinate_ref = core::makeUnknownProjectedSpatialRef("PROJECTED:SEGY");
        addDiagnostic(Sev::Info, Code::CoordinateSystemInferred,
             "Trace coordinate units indicate projected data but no CRS was found"
             " in the text header; treating as PROJECTED:SEGY — user CRS override"
             " recommended for accurate map placement");
    }

    return index;
}

} // namespace dolphin::io
