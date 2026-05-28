// ParsedCache.cpp — parsedCacheIsValid and ParsedCacheReader lifecycle.
// Index building lives in ParsedCache.Index.cpp.
// Artifact reading and cache writing live in ParsedCache.Read.cpp.
#include "io/cache/ParsedCache_p.h"

namespace dolphin::io {

using namespace detail_cache;

bool parsedCacheIsValid(const std::string& path)
{
    if (path.empty()) return false;
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "rb");
#else
    f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    CacheFileHeader hdr{};
    const bool ok = (std::fread(&hdr, sizeof(hdr), 1, f) == 1)
                 && sameMagic(hdr.magic, kFileMagic)
                 && (hdr.version == kCacheVersion);
    std::fclose(f);
    return ok;
}

ParsedCacheReader::ParsedCacheReader() = default;

ParsedCacheReader::~ParsedCacheReader()
{
    close();
}

bool ParsedCacheReader::open(const std::string& path)
{
    close();
    m_path = path;
#ifdef _WIN32
    fopen_s(&m_file, path.c_str(), "rb");
#else
    m_file = std::fopen(path.c_str(), "rb");
#endif
    if (!m_file) return false;

    if (!detail::seekEnd(m_file) || !detail::tellFile(m_file, m_fileSize)
        || !detail::seekAbs(m_file, 0)) {
        close();
        return false;
    }

    CacheFileHeader header{};
    if (!readPod(m_file, header)) {
        close();
        return false;
    }
    if (!sameMagic(header.magic, kFileMagic) || header.version != kCacheVersion) {
        close();
        return false;
    }

    m_meta = {};
    m_meta.format_name = "DPCACHE";
    loadFileHeaderMetadata(header, m_meta);
    return true;
}

void ParsedCacheReader::close()
{
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_meta = {};
    m_fileSize = 0;
}

FormatMeta ParsedCacheReader::metadata()
{
    return m_meta;
}

} // namespace dolphin::io
