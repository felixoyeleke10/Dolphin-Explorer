#pragma once
#include "io/segy/detail/SegyByteOrder.h"
#include <cstdint>
#include <cmath>

namespace dolphin::io::detail_segy {

// -- Coordinate scalar ----------------------------------------------------------
// Positive → multiplier; negative → divisor; 0 or 1 → identity.
inline double applyCoordScalar(int32_t raw, int16_t scalar)
{
    if (raw == 0)                    return 0.0;
    if (scalar == 0 || scalar == 1)  return static_cast<double>(raw);
    if (scalar > 0)                  return static_cast<double>(raw) * scalar;
    return static_cast<double>(raw) / static_cast<double>(-scalar);
}

// -- Trace identification code (bytes 29-30, index 28) -------------------------
// 1=seismic  2=dead  3=dummy  4=time-break  5=uphole  6=sweep  7=timing
// 8=water-break  ≥9=vendor-defined
inline int16_t traceIdentCode(const uint8_t* thdr, bool le)
{
    return rdInt16(&thdr[28], le);
}

// Dead (2) and dummy (3) traces carry no signal; skip them in the index.
// Unknown (0) and seismic (1) are both accepted, as are vendor codes (≥9).
inline bool isSeismicTrace(int16_t code)
{
    return code != 2 && code != 3;
}

// -- Delay recording time (bytes 109-110, index 108) --------------------------
// Time in milliseconds from the shot to the start of the recorded trace.
inline int16_t traceDelayMs(const uint8_t* thdr, bool le)
{
    return rdInt16(&thdr[108], le);
}

// -- Timestamp -----------------------------------------------------------------
// Reads year / day-of-year / h / m / s (bytes 157-166).
// Returns µs since Unix epoch, or 0 if header fields look invalid.
inline int64_t traceTimestampUs(const uint8_t* thdr, bool le)
{
    const int16_t yr  = rdInt16(&thdr[156], le);
    const int16_t doy = rdInt16(&thdr[158], le);
    const int16_t h   = rdInt16(&thdr[160], le);
    const int16_t m   = rdInt16(&thdr[162], le);
    const int16_t s   = rdInt16(&thdr[164], le);

    auto isLeap = [](int y) { return (y%4==0 && y%100!=0) || y%400==0; };
    if (yr < 1970 || yr > 2100 || doy < 1
            || doy > (isLeap(yr) ? 366 : 365)) return 0;

    int64_t days = 0;
    for (int y = 1970; y < yr; ++y) days += isLeap(y) ? 366 : 365;
    days += (doy - 1);

    const int64_t ch = (h >= 0 && h < 24) ? h : 0;
    const int64_t cm = (m >= 0 && m < 60) ? m : 0;
    const int64_t cs = (s >= 0 && s < 60) ? s : 0;

    return (days * 86400LL + ch * 3600LL + cm * 60LL + cs) * 1'000'000LL;
}

// -- Coordinate confidence ------------------------------------------------------

enum class CoordConfidence : uint8_t {
    None      = 0,  // all-zero coordinates
    Suspect   = 1,  // present but outside all plausible ranges
    Plausible = 2,  // fits known bounds; units field not explicitly set
    Declared  = 3,  // units field explicitly declares geographic or projected
};

struct CoordResult {
    double          lon;
    double          lat;
    bool            is_projected;
    CoordConfidence confidence;
    bool            possibly_swapped;   // X/Y may be transposed by the exporter
    bool            units_contradicted; // declared geographic but values exceed WGS-84 bounds
};

// Extended coordinate parser — decodes source → group → CDP with priority fallback,
// returns a CoordConfidence level and a swap-suspicion flag.
inline CoordResult parseTraceCoordsEx(const uint8_t* thdr, bool le)
{
    const int16_t scalar = rdInt16(&thdr[70], le);   // bytes 71-72
    const int32_t src_x  = rdInt32(&thdr[72], le);   // bytes 73-76
    const int32_t src_y  = rdInt32(&thdr[76], le);   // bytes 77-80
    const int32_t grp_x  = rdInt32(&thdr[80], le);   // bytes 81-84
    const int32_t grp_y  = rdInt32(&thdr[84], le);   // bytes 85-88
    const int16_t units  = rdInt16(&thdr[88], le);   // bytes 89-90

    int32_t raw_x = src_x, raw_y = src_y;
    if (raw_x == 0 && raw_y == 0) { raw_x = grp_x; raw_y = grp_y; }
    if (raw_x == 0 && raw_y == 0) {
        raw_x = rdInt32(&thdr[180], le);
        raw_y = rdInt32(&thdr[184], le);
    }

    CoordResult out{};
    if (raw_x == 0 && raw_y == 0) {
        out.confidence = CoordConfidence::None;
        return out;
    }

    double x = applyCoordScalar(raw_x, scalar);
    double y = applyCoordScalar(raw_y, scalar);
    if (units == 2) { x /= 3600.0; y /= 3600.0; }  // arc-seconds → degrees

    const bool explicitly_proj = (units == 1);
    const bool explicitly_geo  = (units == 2 || units == 3 || units == 4);
    const bool fits_geo        = (std::abs(x) <= 180.0 && std::abs(y) <= 90.0);

    if (explicitly_proj) {
        out.is_projected = true;
        out.confidence   = CoordConfidence::Declared;
    } else if (explicitly_geo && fits_geo) {
        // Declaration and magnitudes agree — trust the header.
        out.is_projected = false;
        out.confidence   = CoordConfidence::Declared;
    } else if (explicitly_geo && !fits_geo) {
        // Two sub-cases share this branch:
        //
        //  Swap candidate — X fits latitude range (≤90°) while Y is outside latitude
        //  range but still within longitude range (90°<|Y|≤180°).  The exporter most
        //  likely transposed the coordinate fields.  Keep as geographic; the
        //  possibly_swapped flag below will cause a diagnostic to be emitted.
        //
        //  Projected magnitude — at least one value clearly exceeds all degree bounds
        //  (e.g. UTM easting 432241 m with units=3).  The geographic declaration is
        //  wrong; trust the magnitudes and treat as projected.
        const bool swap_candidate = std::abs(x) <= 90.0
                                 && std::abs(y) >  90.0
                                 && std::abs(y) <= 180.0;
        if (swap_candidate) {
            out.is_projected = false;
            out.confidence   = CoordConfidence::Plausible;
        } else {
            out.is_projected       = true;
            out.confidence         = CoordConfidence::Plausible;
            out.units_contradicted = true;
        }
    } else if (fits_geo) {
        out.is_projected = false;
        out.confidence   = CoordConfidence::Plausible;
    } else if (std::abs(x) > 180.0 || std::abs(y) > 90.0) {
        out.is_projected = true;
        out.confidence   = CoordConfidence::Plausible;
    } else {
        out.is_projected = false;
        out.confidence   = CoordConfidence::Suspect;
    }

    // Swap detection: geographic declaration that fits geo bounds but |X|≤90
    // while |Y|>90 (≤180) — longitude written into the latitude field.
    // Only applies when the declaration is trustworthy (fits_geo check above).
    out.possibly_swapped = !out.is_projected && explicitly_geo
        && std::abs(x) <= 90.0
        && std::abs(y) > 90.0 && std::abs(y) <= 180.0;

    out.lon = x;
    out.lat = y;
    return out;
}

// Simple (non-extended) coordinate decode used by Decode.cpp.
inline void parseTraceCoords(const uint8_t* thdr, bool le,
                              double& out_lon, double& out_lat,
                              bool& out_is_projected)
{
    const CoordResult r = parseTraceCoordsEx(thdr, le);
    out_lon          = r.lon;
    out_lat          = r.lat;
    out_is_projected = r.is_projected;
}

} // namespace dolphin::io::detail_segy
