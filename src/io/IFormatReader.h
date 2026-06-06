#pragma once
#include <string>
#include <functional>
#include <optional>
#include <vector>
#include "core/Artifact.h"
#include "core/ArtifactIndex.h"
#include "core/ImportDiagnostic.h"
#include "core/SpatialRef.h"

namespace dolphin::io {

// Summary information extracted cheaply from the file header.
struct FormatMeta {
    std::string format_name;
    std::string sonar_name;
    std::string vessel_name;
    std::string survey_name;
    double      start_time     = 0.0;   // Unix epoch seconds
    double      end_time       = 0.0;
    uint32_t    artifact_count       = 0;  // all artifact types combined
    uint32_t    sidescan_ping_count  = 0;  // SidescanPing records only
    uint32_t    subbottom_trace_count = 0; // SubBottomTrace records only
    uint32_t    mag_sample_count     = 0;  // MagSample records only
    float       frequency_hz     = 0.0f;  // primary (or only) frequency
    float       low_frequency_hz = 0.0f;  // second frequency for dual-freq systems; 0 = single
    core::SpatialRef coordinate_ref;
    // Extended fields (populated by readers that support them)
    std::string text_header;                    // decoded text / EBCDIC header (SEG-Y: 3200 chars)
    uint8_t     segy_revision           = 0;   // 0=Rev0, 1=Rev1, 2=Rev2 (SEG-Y only)
    uint16_t    binary_sample_interval_us = 0; // sample interval from binary file header (µs)
    uint32_t    binary_samples_per_trace  = 0; // samples/trace from binary file header (0=unset)
    // Bottom pick source summary: bit 0 = any auto-detected picks, bit 1 = any user-edited picks.
    // 0 = no picks found (fresh import or detection not yet run).
    uint8_t     bottom_pick_src_mask = 0;
    // OR of all CorrectionFlag bits seen across artifact payload headers during buildIndex.
    // Non-zero means at least one artifact was written after a processing pipeline run.
    uint32_t    correction_flags_seen = 0;
};

// Progress callback: value in [0.0, 1.0]
using ProgressFn = std::function<void(float)>;

class IFormatReader {
public:
    virtual ~IFormatReader() = default;

    virtual bool             open(const std::string& path) = 0;
    virtual void             close() = 0;
    virtual FormatMeta       metadata() = 0;

    // Scan the file once and build a seek table for all artifact types.
    // May be slow for large files — run on a background thread.
    virtual core::ArtifactIndex buildIndex(ProgressFn progress = {}) = 0;

    // Read one artifact by its index entry.
    // Returns nullopt if the record cannot be decoded.
    virtual std::optional<core::Artifact>
        readArtifact(const core::ArtifactIndexEntry& entry) = 0;

    virtual bool        isOpen()     const = 0;
    virtual std::string formatName() const = 0;

    // Diagnostics produced during the last buildIndex call.
    // Cleared automatically at the start of each buildIndex; caller may also
    // clear explicitly with clearDiagnostics() before open().
    const std::vector<core::ImportDiagnostic>& diagnostics() const
    {
        return m_diagnostics;
    }
    void clearDiagnostics() { m_diagnostics.clear(); }

protected:
    // Convenience helper for subclasses — appends one diagnostic.
    void addDiagnostic(core::ImportDiagnosticSeverity severity,
              core::ImportDiagnosticCode     code,
              std::string                    message,
              uint64_t                       file_offset  = 0,
              int64_t                        timestamp_us = 0,
              uint32_t                       ping_number  = 0,
              uint8_t                        confidence   = 0)
    {
        m_diagnostics.push_back({severity, code, std::move(message),
                                 file_offset, timestamp_us, ping_number, confidence});
    }

    std::vector<core::ImportDiagnostic> m_diagnostics;
};

} // namespace dolphin::io
