// XtfReader.cpp — lifecycle, file-header parsing, channel helpers, nav helpers.
//
// Index building lives in XtfIndex.cpp.
// Artifact decoding lives in XtfPayload.cpp.

#include "io/xtf/XtfReader.h"
#include "io/xtf/XtfReader_p.h"
#include "io/FileIo.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace dolphin::io {

// -- Lifecycle ----------------------------------------------------------------

XtfReader::XtfReader() = default;

XtfReader::~XtfReader()
{
    close();
}

bool XtfReader::open(const std::string& path)
{
    close();
    m_path = path;
#ifdef _WIN32
    fopen_s(&m_file, path.c_str(), "rb");
#else
    m_file = fopen(path.c_str(), "rb");
#endif
    if (!m_file) return false;

    if (!detail::seekEnd(m_file) || !detail::tellFile(m_file, m_fileSize)
        || !detail::seekAbs(m_file, 0)) {
        close();
        return false;
    }

    if (!readFileHeader()) { close(); return false; }
    return true;
}

void XtfReader::close()
{
    if (m_file) { fclose(m_file); m_file = nullptr; }
    m_metaRead = false;
    m_dataStartOffset = 1024;
    m_coords_projected = false;
    m_coords_projected_known = false;
    m_nav_units = 0;
    m_chan_info.clear();
    m_nav_table.reset();
}

// -- Sonar type lookup ---------------------------------------------------------

static std::string sonarNameFromType(uint16_t sonar_type)
{
    // XTF SonarType codes sourced from Triton / SonarWiz documentation.
    // Only codes that appear in real files with high confidence are listed.
    static const struct { uint16_t code; const char* name; } kTable[] = {
        {  0, "" },               // Unknown / not set
        {  1, "Edgetech 272-T" },
        {  2, "Edgetech 260" },
        {  3, "Edgetech CHIRP" },
        {  4, "Edgetech 4200" },
        {  5, "Edgetech 4100" },
        {  6, "Edgetech 4125" },
        {  7, "Edgetech 4150" },
        {  8, "Edgetech DW-106" },
        {  9, "Edgetech 2000" },
        { 10, "Klein 2000" },
        { 11, "Klein 3000" },
        { 12, "Klein 5000" },
        { 13, "Klein 5500" },
        { 14, "Klein 595" },
        { 15, "Klein 3900" },
        { 16, "Triton Elics ISIS" },
        { 17, "Imagenex Delta T" },
        { 18, "Imagenex 837" },
        { 19, "Imagenex 872" },
        { 20, "Marine Sonic Sea Scan" },
        { 21, "L3 Straza (SSDS)" },
        { 22, "Desert Star Reef Master" },
        { 30, "Kongsberg EM100" },
        { 31, "Kongsberg EM950" },
        { 32, "Kongsberg EM1000" },
        { 33, "Kongsberg EM1002" },
        { 34, "Kongsberg EM3000" },
        { 35, "Kongsberg EM3002" },
        { 40, "Reson SeaBat 8125" },
        { 41, "Reson SeaBat 8101" },
        { 42, "Reson SeaBat 9001" },
        { 43, "Reson SeaBat 9003" },
        { 50, "Elac Nautik Bottomchart" },
        { 51, "Elac Nautik 1180" },
        { 52, "Elac Nautik 1050D" },
        { 60, "C-MAX CM2" },
        { 61, "C-MAX CM2" },
        { 70, "Benthos SIS-1500" },
        { 80, "Geoacoustics GeoSwath" },
        { 90, "SIMRAD SM2000" },
        {100, "Atlas Fansweep 15" },
        {101, "Atlas Fansweep 20" },
        {110, "Mesotech SM2000" },
        {120, "Williamson & Associates" },
        {130, "OmniTech" },
        {140, "Furuno" },
    };
    for (const auto& e : kTable) {
        if (e.code == sonar_type && e.name[0] != '\0')
            return e.name;
    }
    if (sonar_type != 0)
        return "Unknown sonar (type " + std::to_string(sonar_type) + ")";
    return {};
}

bool XtfReader::readFileHeader()
{
    XtfFileHeader hdr{};
    if (fread(&hdr, sizeof(hdr), 1, m_file) != 1) return false;
    if (hdr.FileFormat != 0x7B) return false;

    m_meta.format_name = "XTF";
    m_meta.sonar_name  = std::string(hdr.SonarName, strnlen(hdr.SonarName, 16));
    if (m_meta.sonar_name.empty())
        m_meta.sonar_name = sonarNameFromType(hdr.SonarType);
    m_meta.vessel_name = std::string(hdr.NoteString, strnlen(hdr.NoteString, 64));
    m_meta.survey_name = std::string(hdr.ThisFileName, strnlen(hdr.ThisFileName, 64));
    m_nav_units = hdr.NavUnits;
    if (hdr.NavUnits == 3) {
        m_coords_projected = true;
        m_coords_projected_known = true;
    } else if (hdr.NavUnits == 0 || hdr.NavUnits == 1) {
        m_coords_projected = false;
        m_coords_projected_known = true;
    } else {
        m_coords_projected = false;
        m_coords_projected_known = false;
    }
    m_meta.coordinate_ref = coordinateRefFromNavUnits(hdr.NavUnits);

    // Read channel info — each XtfChanInfo block sits immediately after XtfFileHeader,
    // up to byte 1024. Capture TypeOfChannel and BytesPerSample for each channel.
    // Both fields are uint16_t; their int-promoted sum is always >= 0.
    // Clamp to a sane maximum — an untrusted header could otherwise cause a
    // multi-hundred-MB allocation.
    int num_channels = hdr.NumberOfSonarChannels + hdr.NumberOfBathymetryChannels;
    if (num_channels > 64)
        return false;
    m_chan_info.resize(static_cast<size_t>(num_channels));

    const long chan_block_bytes = static_cast<long>(sizeof(XtfChanInfo))
                                * static_cast<long>(num_channels);
    const long header_bytes = std::max<long>(
        1024L,
        ((256L + chan_block_bytes + 1023L) / 1024L) * 1024L);

    for (int c = 0; c < num_channels; ++c) {
        // Channel info blocks begin at byte 256 (immediately after file header),
        // each 128 bytes wide. XTF grows the file header in 1024-byte chunks
        // when more than six channels are present.
        long chan_offset = 256L + c * static_cast<long>(sizeof(XtfChanInfo));
        if (chan_offset + static_cast<long>(sizeof(XtfChanInfo)) > header_bytes) break;

        if (!detail::seekAbs(m_file, static_cast<uint64_t>(chan_offset)))
            break;
        XtfChanInfo ci{};
        if (fread(&ci, sizeof(ci), 1, m_file) != 1) break;

        auto& cm = m_chan_info[static_cast<size_t>(c)];
        cm.type                  = ci.TypeOfChannel;
        cm.sub_channel           = ci.SubChannelNumber;
        cm.samples_per_channel   = ci.SamplesPerChannel;
        cm.channel_name          = std::string(ci.ChannelName, strnlen(ci.ChannelName, 16));
        // BytesPerSample: 1 = 8-bit, 2 = 16-bit.
        // 0 means unset — defer to per-ping inference (inferBps).
        if (ci.BytesPerSample == 1 || ci.BytesPerSample == 2 || ci.BytesPerSample == 4) {
            cm.bytes_per_sample         = ci.BytesPerSample;
            cm.bytes_per_sample_unknown = false;
        } else {
            cm.bytes_per_sample         = 1;
            cm.bytes_per_sample_unknown = true;
        }
        // Calibration and geometry fields
        cm.volt_scale       = (ci.VoltScale > 0.f) ? ci.VoltScale : 1.f;
        cm.horiz_beam_angle = ci.HorizBeamAngle;
        cm.tilt_angle       = ci.TiltAngle;
        cm.beam_width       = ci.BeamWidth;
        cm.offset_x         = ci.OffsetX;
        cm.offset_y         = ci.OffsetY;
        cm.offset_z         = ci.OffsetZ;
        cm.offset_yaw       = ci.OffsetYaw;
        cm.offset_pitch     = ci.OffsetPitch;
        cm.offset_roll      = ci.OffsetRoll;
        // Frequency: XtfChanInfo.Frequency is nominally in kHz, but some acquisition
        // systems write it in Hz.  Values above 2000 can't be kHz (that's 2 MHz —
        // no sidescan sonar runs that high) so treat them as Hz already.
        if (ci.Frequency > 0.f)
            cm.frequency_hz = (ci.Frequency > 2000.f)
                              ? ci.Frequency
                              : ci.Frequency * 1000.f;
    }

    // Collect distinct SSS channel frequencies to detect dual-frequency systems.
    // Sort descending so frequency_hz = highest, low_frequency_hz = lowest (when different).
    std::vector<float> sss_freqs;
    for (const auto& cm : m_chan_info) {
        if ((cm.type == CHAN_PORT_SSS || cm.type == CHAN_STBD_SSS) && cm.frequency_hz > 0.f) {
            // Only add if not already present (same frequency on port+stbd channels).
            bool found = false;
            for (float f : sss_freqs)
                if (std::fabs(f - cm.frequency_hz) < 1.f) { found = true; break; }
            if (!found) sss_freqs.push_back(cm.frequency_hz);
        }
    }
    std::sort(sss_freqs.begin(), sss_freqs.end(), std::greater<float>());
    if (!sss_freqs.empty()) {
        m_meta.frequency_hz = sss_freqs[0];
        if (sss_freqs.size() >= 2)
            m_meta.low_frequency_hz = sss_freqs.back();
    }

    // Position at start of data packets
    m_dataStartOffset = static_cast<uint64_t>(header_bytes);
    if (!detail::seekAbs(m_file, static_cast<uint64_t>(header_bytes)))
        return false;
    m_metaRead = true;
    return true;
}

std::optional<core::ArtifactType> XtfReader::classifyChannel(uint16_t chan_number) const
{
    if (chan_number >= static_cast<uint16_t>(m_chan_info.size()))
        return std::nullopt;

    uint16_t type = m_chan_info[chan_number].type;
    if (type == CHAN_SUBBOT)   return core::ArtifactType::SubBottom;
    if (type == CHAN_PORT_SSS) return core::ArtifactType::Sidescan;
    if (type == CHAN_STBD_SSS) return core::ArtifactType::Sidescan;
    return std::nullopt;
}

core::SidescanChannel XtfReader::sidescanChannel(uint16_t chan_number,
                                                 uint8_t  pkt_sub_channel,
                                                 uint16_t num_chans_to_follow) const
{
    // Compute the adjusted m_chan_info index.
    // Dual-frequency files (Edgetech 4200-style, num_chans_to_follow > 1 per packet)
    // use SubChannelNumber to identify the frequency band; the adjusted index selects
    // the correct channel-info entry (e.g. index 2/3 for HF in a 4-channel file).
    // Single-channel-per-packet recordings (num_chans_to_follow == 1) sometimes use
    // SubChannelNumber to distinguish port from starboard — handled below as fallback.
    const uint16_t n = static_cast<uint16_t>(m_chan_info.size());
    uint16_t idx = chan_number;
    bool adjusted = false;
    if (pkt_sub_channel > 0 && num_chans_to_follow > 0) {
        const uint16_t adj = chan_number
            + static_cast<uint16_t>(pkt_sub_channel) * num_chans_to_follow;
        if (adj < n) { idx = adj; adjusted = true; }
    }

    if (idx < n) {
        const auto& meta = m_chan_info[idx];

        const std::string name = lowerAscii(meta.channel_name);
        if (name.find("starboard") != std::string::npos || name.find("stbd") != std::string::npos)
            return core::SidescanChannel::Starboard;
        if (name.find("port") != std::string::npos)
            return core::SidescanChannel::Port;

        if (meta.type == CHAN_STBD_SSS) return core::SidescanChannel::Starboard;
        if (meta.type == CHAN_PORT_SSS) {
            // Only treat pkt_sub_channel=1 as starboard when the index was NOT adjusted
            // (adjusted index handles dual-freq; fallback handles single-chan recordings
            // where sub_channel truly encodes port vs stbd).
            if (!adjusted && pkt_sub_channel == 1) return core::SidescanChannel::Starboard;
            return core::SidescanChannel::Port;
        }

        if (meta.sub_channel == 1) return core::SidescanChannel::Starboard;
        if (meta.sub_channel == 0) {
            if (!adjusted && pkt_sub_channel == 1) return core::SidescanChannel::Starboard;
            return core::SidescanChannel::Port;
        }
    }

    // Last resort — pkt_sub_channel as port/stbd hint only for single-channel packets.
    if (pkt_sub_channel == 1 && num_chans_to_follow <= 1)
        return core::SidescanChannel::Starboard;
    if (chan_number == 1) return core::SidescanChannel::Starboard;
    return core::SidescanChannel::Port;
}

uint16_t XtfReader::bytesPerSample(uint16_t chan_number) const
{
    if (chan_number < static_cast<uint16_t>(m_chan_info.size()))
        return m_chan_info[chan_number].bytes_per_sample;
    return 2;
}

FormatMeta XtfReader::metadata()
{
    return m_meta;
}

} // namespace dolphin::io
