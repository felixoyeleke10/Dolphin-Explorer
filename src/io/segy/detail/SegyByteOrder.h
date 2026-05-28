#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace dolphin::io::detail_segy {

// ── Big-endian integer reads ───────────────────────────────────────────────────
inline int16_t beInt16(const uint8_t* p)
{
    return static_cast<int16_t>((uint16_t{p[0]} << 8) | p[1]);
}
inline uint16_t beUint16(const uint8_t* p)
{
    return static_cast<uint16_t>((uint16_t{p[0]} << 8) | p[1]);
}
inline int32_t beInt32(const uint8_t* p)
{
    return static_cast<int32_t>(
          (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16)
        | (uint32_t{p[2]} <<  8) |  uint32_t{p[3]});
}
inline uint32_t beUint32(const uint8_t* p)
{
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16)
         | (uint32_t{p[2]} <<  8) |  uint32_t{p[3]};
}
inline int32_t beInt24(const uint8_t* p)
{
    uint32_t u = (uint32_t{p[0]} << 16) | (uint32_t{p[1]} << 8) | uint32_t{p[2]};
    if (u & 0x800000u) u |= 0xFF000000u;  // sign-extend from 24 bits
    return static_cast<int32_t>(u);
}
inline int64_t beInt64(const uint8_t* p)
{
    return static_cast<int64_t>(
          (uint64_t{p[0]} << 56) | (uint64_t{p[1]} << 48)
        | (uint64_t{p[2]} << 40) | (uint64_t{p[3]} << 32)
        | (uint64_t{p[4]} << 24) | (uint64_t{p[5]} << 16)
        | (uint64_t{p[6]} <<  8) |  uint64_t{p[7]});
}

// ── Little-endian integer reads ────────────────────────────────────────────────
inline int16_t leInt16(const uint8_t* p)
{
    return static_cast<int16_t>((uint16_t{p[1]} << 8) | p[0]);
}
inline uint16_t leUint16(const uint8_t* p)
{
    return static_cast<uint16_t>((uint16_t{p[1]} << 8) | p[0]);
}
inline int32_t leInt32(const uint8_t* p)
{
    return static_cast<int32_t>(
          (uint32_t{p[3]} << 24) | (uint32_t{p[2]} << 16)
        | (uint32_t{p[1]} <<  8) |  uint32_t{p[0]});
}
inline uint32_t leUint32(const uint8_t* p)
{
    return (uint32_t{p[3]} << 24) | (uint32_t{p[2]} << 16)
         | (uint32_t{p[1]} <<  8) |  uint32_t{p[0]};
}
inline int32_t leInt24(const uint8_t* p)
{
    uint32_t u = (uint32_t{p[2]} << 16) | (uint32_t{p[1]} << 8) | uint32_t{p[0]};
    if (u & 0x800000u) u |= 0xFF000000u;
    return static_cast<int32_t>(u);
}
inline int64_t leInt64(const uint8_t* p)
{
    return static_cast<int64_t>(
          (uint64_t{p[7]} << 56) | (uint64_t{p[6]} << 48)
        | (uint64_t{p[5]} << 40) | (uint64_t{p[4]} << 32)
        | (uint64_t{p[3]} << 24) | (uint64_t{p[2]} << 16)
        | (uint64_t{p[1]} <<  8) |  uint64_t{p[0]});
}

// ── Endian-conditional readers ─────────────────────────────────────────────────
inline int16_t  rdInt16 (const uint8_t* p, bool le) { return le ? leInt16(p)  : beInt16(p);  }
inline uint16_t rdUint16(const uint8_t* p, bool le) { return le ? leUint16(p) : beUint16(p); }
inline int32_t  rdInt32 (const uint8_t* p, bool le) { return le ? leInt32(p)  : beInt32(p);  }
inline uint32_t rdUint32(const uint8_t* p, bool le) { return le ? leUint32(p) : beUint32(p); }
inline int32_t  rdInt24 (const uint8_t* p, bool le) { return le ? leInt24(p)  : beInt24(p);  }
inline int64_t  rdInt64 (const uint8_t* p, bool le) { return le ? leInt64(p)  : beInt64(p);  }

// ── IEEE float reads ───────────────────────────────────────────────────────────
inline float bef32(const uint8_t* p)
{
    uint32_t u = (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16)
               | (uint32_t{p[2]} <<  8) |  uint32_t{p[3]};
    float f; std::memcpy(&f, &u, 4); return f;
}
inline float bef64to32(const uint8_t* p)
{
    uint64_t u = (uint64_t{p[0]} << 56) | (uint64_t{p[1]} << 48)
               | (uint64_t{p[2]} << 40) | (uint64_t{p[3]} << 32)
               | (uint64_t{p[4]} << 24) | (uint64_t{p[5]} << 16)
               | (uint64_t{p[6]} <<  8) |  uint64_t{p[7]};
    double d; std::memcpy(&d, &u, 8); return static_cast<float>(d);
}
inline float lef32(const uint8_t* p)
{
    uint32_t u = (uint32_t{p[3]} << 24) | (uint32_t{p[2]} << 16)
               | (uint32_t{p[1]} <<  8) |  uint32_t{p[0]};
    float f; std::memcpy(&f, &u, 4); return f;
}
inline float lef64to32(const uint8_t* p)
{
    uint64_t u = (uint64_t{p[7]} << 56) | (uint64_t{p[6]} << 48)
               | (uint64_t{p[5]} << 40) | (uint64_t{p[4]} << 32)
               | (uint64_t{p[3]} << 24) | (uint64_t{p[2]} << 16)
               | (uint64_t{p[1]} <<  8) |  uint64_t{p[0]};
    double d; std::memcpy(&d, &u, 8); return static_cast<float>(d);
}
inline float rdf32    (const uint8_t* p, bool le) { return le ? lef32(p)     : bef32(p);     }
inline float rdf64to32(const uint8_t* p, bool le) { return le ? lef64to32(p) : bef64to32(p); }

// ── IBM 32-bit float → IEEE float ─────────────────────────────────────────────
// IBM format is inherently big-endian regardless of file byte order.
inline float ibmToIeee(uint32_t ibm)
{
    if (ibm == 0) return 0.f;
    const int    sign = (ibm >> 31) ? -1 : 1;
    const int    exp  = static_cast<int>((ibm >> 24) & 0x7fu) - 64;
    const double mant = static_cast<double>(ibm & 0x00ffffffu) / 16777216.0;
    return static_cast<float>(sign * mant * std::pow(16.0, exp));
}

} // namespace dolphin::io::detail_segy
