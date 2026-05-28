#pragma once

#include <cstdint>
#include <cstdio>

#ifndef _WIN32
#include <sys/types.h>
#endif

namespace dolphin::io::detail {

inline bool seekAbs(FILE* file, uint64_t offset)
{
#ifdef _WIN32
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

inline bool seekEnd(FILE* file)
{
#ifdef _WIN32
    return _fseeki64(file, 0, SEEK_END) == 0;
#else
    return fseeko(file, 0, SEEK_END) == 0;
#endif
}

inline bool tellFile(FILE* file, uint64_t& offset)
{
#ifdef _WIN32
    const __int64 pos = _ftelli64(file);
    if (pos < 0) return false;
    offset = static_cast<uint64_t>(pos);
    return true;
#else
    const off_t pos = ftello(file);
    if (pos < 0) return false;
    offset = static_cast<uint64_t>(pos);
    return true;
#endif
}

} // namespace dolphin::io::detail
