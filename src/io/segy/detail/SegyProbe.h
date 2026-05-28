#pragma once
#include "io/segy/detail/SegyByteOrder.h"
#include "io/segy/detail/SegyConstants.h"
#include "io/FileIo.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace dolphin::io::detail_segy {

// ── Trace header plausibility score ───────────────────────────────────────────
// Returns a score in [0, 8] indicating how plausible this 240-byte block is as
// a SEG-Y trace header for the given byte order.  Used by the probe phase and
// the resync scanner.
inline int scoreTraceHeader(const uint8_t* thdr, bool le,
                              uint32_t file_ns = 0, uint16_t file_si = 0)
{
    int score = 0;
    const uint32_t ns    = static_cast<uint32_t>(rdUint16(&thdr[114], le));
    const uint16_t si    = rdUint16(&thdr[116], le);
    const int16_t  year  = rdInt16(&thdr[156], le);
    const int16_t  doy   = rdInt16(&thdr[158], le);
    const int16_t  ident = rdInt16(&thdr[28],  le);
    const int16_t  sc    = rdInt16(&thdr[70],  le);

    const uint32_t eff_ns = (ns > 0) ? ns : file_ns;
    const uint16_t eff_si = (si > 0) ? si : file_si;

    if (eff_ns >= 1    && eff_ns <= 65535)  ++score;
    if (eff_si >= 50   && eff_si <= 100000) ++score;
    if (ident  >= 0    && ident  <= 20)     ++score;
    if (year   >= 1970 && year   <= 2100)   ++score;
    if (year   >= 1970 && year   <= 2100
            && doy >= 1 && doy <= 366)      ++score;

    // SEG-Y spec: valid scalars are ±1, ±10, ±100, ±1000, ±10000, or 0.
    const int abs_sc = std::abs(static_cast<int>(sc));
    if (abs_sc == 0   || abs_sc == 1    || abs_sc == 10
     || abs_sc == 100 || abs_sc == 1000 || abs_sc == 10000) ++score;

    // Bonuses when per-trace values match file-level defaults.
    if (file_ns > 0 && ns == file_ns) ++score;
    if (file_si > 0 && si == file_si) ++score;

    return score;
}

// ── Forward resync scanner ────────────────────────────────────────────────────
// Reads the file in 4 KB chunks and scans (4-byte aligned steps) for a block
// that scores ≥ kThreshold as a trace header.  Searches at most kMaxScan bytes
// forward from [start].  Returns the byte offset, or UINT64_MAX if not found.
inline uint64_t scanForNextTrace(FILE* file, uint64_t start, uint64_t file_end,
                                  bool le,
                                  uint32_t preferred_ns = 0,
                                  uint16_t preferred_si = 0)
{
    constexpr int      kThreshold = 4;
    constexpr uint64_t kChunk     = 4096;
    constexpr uint64_t kMaxScan   = 256 * 1024;
    constexpr uint64_t kStep      = 4;

    const uint64_t search_end = std::min(file_end, start + kMaxScan);
    if (start + kTraceHdrBytes > search_end) return UINT64_MAX;

    std::vector<uint8_t> buf(kChunk + kTraceHdrBytes, 0);

    for (uint64_t chunk_start = start; chunk_start < search_end; chunk_start += kChunk) {
        if (!detail::seekAbs(file, chunk_start)) return UINT64_MAX;
        const size_t to_read = static_cast<size_t>(
            std::min(kChunk + kTraceHdrBytes, search_end - chunk_start + kTraceHdrBytes));
        const size_t got = std::fread(buf.data(), 1, to_read, file);
        if (got < kTraceHdrBytes) break;

        const size_t scan_limit = (got >= kTraceHdrBytes) ? got - kTraceHdrBytes + 1 : 0;
        for (size_t off = 0; off < scan_limit; off += kStep) {
            if (scoreTraceHeader(buf.data() + off, le, preferred_ns, preferred_si)
                    >= kThreshold)
                return chunk_start + off;
        }
    }
    return UINT64_MAX;
}

} // namespace dolphin::io::detail_segy
