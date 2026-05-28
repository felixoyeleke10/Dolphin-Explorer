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

    m_meta.format_name   = "JSF";
    m_meta.coordinate_ref = core::makeWgs84SpatialRef();
    return true;
}

void JsfReader::close()
{
    if (m_file) { fclose(m_file); m_file = nullptr; }
}

FormatMeta JsfReader::metadata() { return m_meta; }

core::ArtifactType JsfReader::classifySubsystem(uint8_t subsystem)
{
    if (subsystem == SUBSYS_SBP) return core::ArtifactType::SubBottom;
    return core::ArtifactType::Sidescan;
}

} // namespace dolphin::io
