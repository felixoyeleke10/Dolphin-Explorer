#pragma once
#include "io/IFormatReader.h"
#include "io/ProbeResult.h"
#include <cstdio>
#include <cstdint>

namespace dolphin::io {

// Edgetech JSF (Joint Strike Format) reader.
// Supports SSS pings (port + starboard) and SBP traces.
// No file header — format is a flat sequence of 16-byte-prefixed messages.
class JsfReader : public IFormatReader {
public:
    JsfReader();
    ~JsfReader() override;

    bool        open(const std::string& path) override;
    void        close() override;
    FormatMeta  metadata() override;

    core::ArtifactIndex buildIndex(ProgressFn progress = {}) override;

    std::optional<core::Artifact>
        readArtifact(const core::ArtifactIndexEntry& entry) override;

    bool        isOpen()     const override { return m_file != nullptr; }
    std::string formatName() const override { return "JSF"; }

    ProbeResult probe(const std::string& path);

private:
    FILE*       m_file     = nullptr;
    std::string m_path;
    FormatMeta  m_meta;
    uint64_t    m_fileSize = 0;

    static core::ArtifactType classifySubsystem(uint8_t subsystem);

    // JSF constants
    static constexpr uint16_t JSF_MARKER    = 0x1601;
    static constexpr uint16_t MSG_SONAR     = 80;    // sonar data (SSS or SBP)
    static constexpr uint8_t  SUBSYS_PORT   = 0;
    static constexpr uint8_t  SUBSYS_STBD   = 1;
    static constexpr uint8_t  SUBSYS_SBP    = 20;    // sub-bottom profiler
    static constexpr uint32_t kPingHdrSize  = 240;   // sonar message body header
    static constexpr uint32_t kMaxRecordSz  = 64u * 1024u * 1024u;  // 64 MiB guard
};

} // namespace dolphin::io
