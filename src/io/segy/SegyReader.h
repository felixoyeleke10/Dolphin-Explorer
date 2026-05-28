#pragma once
#include "io/IFormatReader.h"
#include "io/ProbeResult.h"
#include <cstdio>
#include <cstdint>

namespace dolphin::io {

// SEG-Y (Society of Exploration Geophysicists Y format) reader.
// Handles Rev 0 / Rev 1 / Rev 2 files in big-endian or little-endian byte order.
// Supports sub-bottom profiler (SBP) single-channel traces.
// Sample formats: 1 (IBM float), 2 (int32), 3 (int16), 5 (IEEE float),
//                 6 (IEEE double), 7 (int24), 8 (int8), 9 (int64),
//                 10 (uint32), 11 (uint16), 12 (uint8).
class SegyReader : public IFormatReader {
public:
    SegyReader();
    ~SegyReader() override;

    bool        open(const std::string& path) override;
    void        close() override;
    FormatMeta  metadata() override;

    core::ArtifactIndex buildIndex(ProgressFn progress = {}) override;

    std::optional<core::Artifact>
        readArtifact(const core::ArtifactIndexEntry& entry) override;

    bool        isOpen()     const override { return m_file != nullptr; }
    std::string formatName() const override { return "SEG-Y"; }

    // Lightweight header scan — self-contained (open/close internally).
    // Safe to call from a background thread; no shared mutable state.
    ProbeResult probe(const std::string& path);

private:
    // Number of bytes per sample for the current m_sampleFormat.
    uint32_t bytesPerSample() const;

    FILE*        m_file              = nullptr;
    std::string  m_path;
    FormatMeta   m_meta;
    uint64_t     m_fileSize          = 0;
    uint64_t     m_dataOffset        = 0;   // byte offset of first trace record
    int          m_sampleFormat      = 5;   // data sample format code from binary file header
    uint16_t     m_fileSampleInterval  = 0; // μs per sample (file-level default)
    uint32_t     m_fileSamplesPerTrace = 0; // samples per trace (file-level default; 0 = per-trace)
    bool         m_littleEndian          = false; // true for LE vendor variants
    bool         m_sampleFormatDefaulted = false; // true when format code was 0 (unset)
    bool         m_extHdrScanFailed      = false; // true when ext_raw==-1 and EndText not found
    bool         m_endianFromProbe       = false; // true when byte order was resolved by probe
    std::string  m_textHeaderDecoded;             // decoded 3200-char text header
    uint8_t      m_segyRevision          = 0;     // 0=Rev0, 1=Rev1, 2=Rev2
    uint8_t      m_probeScore            = 0;     // 0–10 confidence score from probe phase
};

} // namespace dolphin::io
