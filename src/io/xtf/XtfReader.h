#pragma once
#include "io/IFormatReader.h"
#include "io/ProbeResult.h"
#include "io/xtf/XtfNav.h"
#include <cstdio>
#include <vector>
#include <cstdint>

namespace dolphin::io {

// XTF (eXtended Triton Format) reader.
// Supports SSS pings, SBP traces, and magnetometer samples.
// XTF spec Rev 40.
class XtfReader : public IFormatReader {
public:
    XtfReader();
    ~XtfReader() override;

    bool        open(const std::string& path) override;
    void        close() override;
    FormatMeta  metadata() override;

    core::ArtifactIndex buildIndex(ProgressFn progress = {}) override;

    std::optional<core::Artifact>
        readArtifact(const core::ArtifactIndexEntry& entry) override;

    bool        isOpen()     const override { return m_file != nullptr; }
    std::string formatName() const override { return "XTF"; }

    ProbeResult probe(const std::string& path);

private:
    FILE*       m_file     = nullptr;
    std::string m_path;
    FormatMeta  m_meta;
    bool        m_metaRead = false;
    uint64_t    m_fileSize = 0;
    uint64_t    m_dataStartOffset = 1024;

    struct ChanMeta {
        uint16_t type                     = 1;
        uint8_t  sub_channel              = 0;
        uint16_t bytes_per_sample         = 2;
        bool     bytes_per_sample_unknown = false;
        uint32_t samples_per_channel      = 0;
        std::string channel_name;
        float    frequency_hz      = 0.f;
        float    volt_scale        = 1.f;
        float    horiz_beam_angle  = 0.f;
        float    tilt_angle        = 0.f;
        float    beam_width        = 0.f;
        float    offset_x          = 0.f;
        float    offset_y          = 0.f;
        float    offset_z          = 0.f;
        float    offset_yaw        = 0.f;
        float    offset_pitch      = 0.f;
        float    offset_roll       = 0.f;
    };
    std::vector<ChanMeta> m_chan_info;

    bool readFileHeader();
    std::optional<core::ArtifactType> classifyChannel(uint16_t chan_number) const;
    core::SidescanChannel sidescanChannel(uint16_t chan_number,
                                          uint8_t  pkt_sub_channel,
                                          uint16_t num_chans_to_follow) const;
    uint16_t bytesPerSample(uint16_t chan_number) const;

    bool     m_coords_projected       = false;
    bool     m_coords_projected_known = false;
    uint16_t m_nav_units              = 0;

    // Nav fix table — populated lazily on first readArtifact call with zero nav.
    XtfNavTable m_nav_table;

    // Delegate wrappers used by XtfPayload.cpp
    void ensureNavFixes() const   { m_nav_table.load(m_file, m_dataStartOffset, m_fileSize); }
    void interpolateNav(int64_t ts, double& lat, double& lon) const
        { m_nav_table.interpolate(ts, lat, lon); }

    static constexpr uint8_t PACKET_PING     = 0;
    static constexpr uint8_t PACKET_ATTITUDE = 3;
    static constexpr uint8_t PACKET_NOTES    = 6;
    static constexpr uint8_t PACKET_NAV      = 42;
};

} // namespace dolphin::io
