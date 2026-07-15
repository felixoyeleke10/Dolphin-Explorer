// ParsedCache.cpp — parsedCacheIsValid and ParsedCacheReader lifecycle.
// Index building lives in ParsedCache.Index.cpp.
// Artifact reading and cache writing live in ParsedCache.Read.cpp.
#include "io/cache/ParsedCache_p.h"
#ifdef _WIN32
#include <share.h>   // _SH_DENYNO for shared _fsopen
#endif

namespace dolphin::io {

using namespace detail_cache;

bool parsedCacheIsValid(const std::string& path)
{
    if (path.empty()) return false;
    ParsedCacheReader reader;
    if (!reader.open(path)) return false;
    return !reader.quickIndex().empty();
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
    // Shared read so the index footer can be appended ("r+b") while this handle is
    // still open. A non-shared handle makes that append fail, leaving the cache
    // footerless — and then every project-open re-scans the whole file.
    m_file = _fsopen(path.c_str(), "rb", _SH_DENYNO);
#else
    m_file = std::fopen(path.c_str(), "rb");
#endif
    if (!m_file) return false;
    // Large buffer so a full index scan reads in big sequential chunks instead of
    // many tiny reads — a major difference on spinning disks / network drives.
    std::setvbuf(m_file, nullptr, _IOFBF, 1 << 20);

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
    if (!sameMagic(header.magic, kFileMagic)
            || header.version < kMinAcceptableVersion
            || header.version > kCacheVersion) {
        close();
        return false;
    }

    m_file_version = header.version;
    m_meta = {};
    m_meta.format_name = "DPCACHE";
    loadFileHeaderMetadata(header, m_meta);
    m_cur_pos = sizeof(CacheFileHeader);  // file pointer is here after reading the header
    return true;
}

void ParsedCacheReader::close()
{
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_meta         = {};
    m_fileSize     = 0;
    m_file_version = 0;
    m_cur_pos      = UINT64_MAX;
}

FormatMeta ParsedCacheReader::metadata()
{
    return m_meta;
}

} // namespace dolphin::io
