#pragma once
#include <cstdint>

namespace dolphin::io::detail_segy {

// ── File layout ────────────────────────────────────────────────────────────────
static constexpr uint64_t kTextHeaderBytes  = 3200;
static constexpr uint64_t kBinHeaderBytes   = 400;
static constexpr uint32_t kTraceHdrBytes    = 240;
static constexpr uint64_t kMinFileBytes     = kTextHeaderBytes + kBinHeaderBytes;
static constexpr uint32_t kMaxExtendedHdrs  = 1000;

// ── Sample format validation ───────────────────────────────────────────────────
// Format 4 (fixed-point with gain) is excluded — it requires per-sample gain
// words that alter the record geometry and cannot be decoded without them.
inline bool isKnownSampleFormat(int fmt)
{
    return fmt == 1  || fmt == 2  || fmt == 3  || fmt == 5  || fmt == 6
        || fmt == 7  || fmt == 8  || fmt == 9  || fmt == 10 || fmt == 11
        || fmt == 12;
}

} // namespace dolphin::io::detail_segy
