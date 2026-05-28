#pragma once
// ImportDiagnostic — structured parse-time diagnostic record.
// Emitted by format readers during buildIndex; surfaced in the problem panel
// and (later) as spatial map overlays.
#include <cstdint>
#include <string>

namespace dolphin::core {

enum class ImportDiagnosticSeverity {
    Info,     // normal parser decision worth recording
    Warning,  // recoverable but suspicious
    Error,    // unrecoverable; data may be lost
};

enum class ImportDiagnosticCode {
    // ── Structural / resync ────────────────────────────────────────────────────
    BadPacketMagic,             // invalid magic encountered; seeking for resync
    ResyncedPacket,             // successfully resynced after skipping N bytes
    TruncatedRecord,            // declared record length exceeds remaining file size
    ImplausibleRecordSize,      // record size beyond sanity limit (> 64 MiB)
    TraceChainBroken,           // expected next trace offset does not hold a plausible header
    // ── Channel / sample encoding ──────────────────────────────────────────────
    UnsupportedPacketType,      // packet HeaderType not recognised or handled
    UnsupportedChannelType,     // channel TypeOfChannel not supported
    InferredBytesPerSample,     // BPS not declared in channel header; inferred from geometry
    ChannelOffsetClamped,       // sample region clamped to record boundary
    UnsupportedSampleEncoding,  // sample format code not supported by this reader
    EncodingInferred,           // byte order or sample format chosen by trace-chain probe
    SamplesPerTraceMismatch,    // per-trace ns differs from binary file header ns
    // ── Navigation ────────────────────────────────────────────────────────────
    MissingNavigation,          // artifact has no usable nav even after backfill
    InterpolatedNavigation,     // nav linearly interpolated between two fixes
    ExtrapolatedNavigation,     // nav extrapolated beyond available fix coverage
    // ── Coordinate reference system ───────────────────────────────────────────
    CoordinateSystemInferred,   // CRS determined from coordinate magnitudes, not declared
    CoordinateSystemOverridden, // declared CRS contradicted by actual data and overridden
    SuspiciousCoordinateMagnitude, // coordinate values outside expected range
    CoordinateEncodingAmbiguous,// units field absent or ambiguous; encoding inferred
    CoordinateSwappedXY,        // X/Y fields appear to be transposed (longitude in Y, latitude in X)
    // ── Miscellaneous ─────────────────────────────────────────────────────────
    MissingFrequency,           // channel frequency not available in header
};

struct ImportDiagnostic {
    ImportDiagnosticSeverity severity     = ImportDiagnosticSeverity::Info;
    ImportDiagnosticCode     code         = ImportDiagnosticCode::BadPacketMagic;
    std::string              message;
    uint64_t                 file_offset  = 0;   // byte offset in file where the issue occurred
    int64_t                  timestamp_us = 0;   // artifact timestamp (0 if not applicable)
    uint32_t                 ping_number  = 0;   // ping/trace number (0 if not applicable)
    uint8_t                  confidence   = 0;   // 0 = unrated; 1–100 = percent confidence in interpretation
};

} // namespace dolphin::core
