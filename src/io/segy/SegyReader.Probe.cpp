// SegyReader.Probe.cpp — lightweight header scan for the import wizard.
// Reuses the existing open() / close() infrastructure; scans only the first
// ~50 trace headers so it completes in milliseconds even for large files.
#include "io/segy/SegyReader_p.h"
#include "io/ProbeResult.h"
#include "core/SpatialRef.h"
#include <algorithm>
#include <limits>

namespace dolphin::io {

using namespace detail_segy;

ProbeResult SegyReader::probe(const std::string& path)
{
    ProbeResult r;
    r.path        = path;
    r.format_name = "SEG-Y";
    r.has_subbottom = true;   // SEG-Y in this app is always sub-bottom profiler

    // SEG-Y is single-channel by convention in this application
    {
        ProbeResult::ChannelInfo ci;
        ci.name     = "Sub-Bottom";
        ci.modality = "Sub-Bottom";
        r.channels.push_back(std::move(ci));
    }

    if (!open(path)) {
        r.error_message = "Cannot open or parse SEG-Y file";
        return r;
    }
    r.file_size_bytes = static_cast<int64_t>(m_fileSize);

    // Text header excerpt (first ~320 chars for the Summary tab)
    if (!m_textHeaderDecoded.empty()) {
        r.text_header_excerpt = m_textHeaderDecoded.substr(
            0, std::min(m_textHeaderDecoded.size(), size_t{320}));
    }

    // ── Scan up to 50 trace headers ──────────────────────────────────────────
    const uint32_t bps    = bytesPerSample();
    uint64_t       offset = m_dataOffset;

    int  n_proj = 0, n_geo = 0, n_declared = 0, n_suspect = 0;
    bool any_swapped = false, any_contradicted = false;

    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double min_y = std::numeric_limits<double>::max();
    double max_y = std::numeric_limits<double>::lowest();

    uint8_t thdr[kTraceHdrBytes];
    for (int i = 0; i < 50 && offset + kTraceHdrBytes <= m_fileSize; ++i) {
        if (!detail::seekAbs(m_file, offset) ||
                std::fread(thdr, kTraceHdrBytes, 1, m_file) != 1)
            break;

        const auto cr = parseTraceCoordsEx(thdr, m_littleEndian);

        if (cr.confidence != CoordConfidence::None) {
            r.coord_valid = true;
            if (r.coord_samples.size() < 5)
                r.coord_samples.push_back({cr.lon, cr.lat});

            min_x = std::min(min_x, cr.lon);
            max_x = std::max(max_x, cr.lon);
            min_y = std::min(min_y, cr.lat);
            max_y = std::max(max_y, cr.lat);

            if (cr.is_projected) ++n_proj; else ++n_geo;
            if (cr.confidence == CoordConfidence::Declared) ++n_declared;
            if (cr.confidence == CoordConfidence::Suspect)  ++n_suspect;
            if (cr.possibly_swapped)   any_swapped      = true;
            if (cr.units_contradicted) any_contradicted = true;
        }

        // Advance to the next trace
        const uint32_t ns     = static_cast<uint32_t>(rdUint16(&thdr[114], m_littleEndian));
        const uint32_t eff_ns = (ns > 0) ? ns : m_fileSamplesPerTrace;
        if (eff_ns == 0 || bps == 0) break;
        offset += kTraceHdrBytes + static_cast<uint64_t>(eff_ns) * bps;
    }

    // ── Summarise ────────────────────────────────────────────────────────────
    if (r.coord_valid) {
        r.coord_min_x        = min_x;
        r.coord_max_x        = max_x;
        r.coord_min_y        = min_y;
        r.coord_max_y        = max_y;
        r.is_projected       = (n_proj > n_geo);
        r.possibly_swapped   = any_swapped;
        r.units_contradicted = any_contradicted;

        // Populate declared_crs from what the trace headers explicitly state.
        // Declared geographic → exact WGS-84 (EPSG:4326).
        // Declared projected  → unknown projected (EPSG code must come from user).
        if (n_declared > 0) {
            r.declared_crs = r.is_projected
                ? core::makeUnknownProjectedSpatialRef("PROJECTED:SEGY_UNITS1")
                : core::makeWgs84SpatialRef();
        }

        // CRS review needed when:
        //   • no explicit declaration (heuristic inference only)
        //   • declared geographic but values are projected-sized (contradicted)
        //   • some traces look suspect (confidence == Suspect)
        //   • declared projected — EPSG code is unknown, user must confirm
        r.needs_crs_review = (n_declared == 0)
                          || any_contradicted
                          || (n_suspect > 0)
                          || (r.is_projected && n_declared > 0);
    } else {
        r.warnings.push_back("No coordinate data found in first 50 traces");
        r.needs_crs_review = true;
    }

    if (any_swapped)
        r.warnings.push_back("X/Y coordinates may be transposed");
    if (any_contradicted)
        r.warnings.push_back("Declared geographic CRS but values appear projected");

    // Back-fill channel samples_per_ping from the binary header value
    if (!r.channels.empty() && m_fileSamplesPerTrace > 0)
        r.channels[0].samples_per_ping = m_fileSamplesPerTrace;

    // Estimate total trace count from file size
    if (m_fileSamplesPerTrace > 0 && bps > 0) {
        const uint64_t trace_sz = kTraceHdrBytes
                                + static_cast<uint64_t>(m_fileSamplesPerTrace) * bps;
        const uint64_t data_sz  = (m_fileSize > m_dataOffset)
                                ? m_fileSize - m_dataOffset : 0;
        r.estimated_record_count = static_cast<uint32_t>(data_sz / trace_sz);
    }

    close();
    r.success = true;
    return r;
}

} // namespace dolphin::io
