// JsfReader.cpp — JsfReader lifecycle and classifySubsystem.
// Index building lives in JsfReader.Index.cpp.
// Artifact decoding lives in JsfReader.Decode.cpp.
#include "io/jsf/JsfReader_p.h"

namespace dolphin::io {

JsfReader::JsfReader() = default;

JsfReader::~JsfReader() { close(); }

bool JsfReader::open(const std::string& path)
{
    close();
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

    // JSF has no file-level header, but every message starts with the same
    // 16-byte framed header. Validate the first frame so an arbitrary readable
    // file with a .jsf extension cannot enter the import pipeline.
    if (m_fileSize < sizeof(detail_jsf::JsfPacketHeader)) {
        close();
        return false;
    }
    detail_jsf::JsfPacketHeader first{};
    if (std::fread(&first, sizeof(first), 1, m_file) != 1
            || first.marker != JSF_MARKER
            || first.size > kMaxRecordSz
            || static_cast<uint64_t>(first.size)
                 > m_fileSize - sizeof(detail_jsf::JsfPacketHeader)
            || !detail::seekAbs(m_file, 0)) {
        close();
        return false;
    }

    m_path = path;
    m_meta = {};
    m_meta.format_name   = "JSF";
    return true;
}

void JsfReader::close()
{
    if (m_file) { fclose(m_file); m_file = nullptr; }
    m_path.clear();
    m_meta = {};
    m_fileSize = 0;
}

FormatMeta JsfReader::metadata() { return m_meta; }

core::ArtifactType JsfReader::classifySubsystem(uint8_t subsystem)
{
    if (subsystem == SUBSYS_SBP) return core::ArtifactType::SubBottom;
    return core::ArtifactType::Sidescan;
}

bool JsfReader::supportsSubsystem(uint8_t subsystem)
{
    return subsystem == SUBSYS_SBP
        || (subsystem >= SUBSYS_SSS_MIN && subsystem <= SUBSYS_SSS_MAX);
}

} // namespace dolphin::io
