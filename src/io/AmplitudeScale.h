#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
//  AmplitudeScale — sonar amplitude normalisation utilities
//
//  Converts raw sonar sample data (uint32 or float32) to the common uint16
//  [0, 65535] representation stored in core::SidescanSample::amplitude.
//
//  All readers (XTF, JSF, …) should use these helpers so the normalisation
//  behaviour is consistent across formats.
//
//  The core strategy is 99th-percentile clipping: we normalise against the
//  99th-percentile value rather than the absolute maximum.  This prevents a
//  single nadir specular spike (the brightest sample in nearly every ping)
//  from compressing all meaningful backscatter data toward zero.
// -----------------------------------------------------------------------------

namespace dolphin::io {

// Returns the value at percentile pct (0.0–1.0) by partial-sorting vec in place.
// O(n) average via std::nth_element.
template <typename T>
T percentileValue(std::vector<T>& vec, float pct)
{
    if (vec.empty()) return T{};
    const size_t idx = std::min(
        static_cast<size_t>(pct * static_cast<float>(vec.size())),
        vec.size() - 1u);
    std::nth_element(vec.begin(), vec.begin() + idx, vec.end());
    return vec[idx];
}

// Clamp-scale a single float value into [0, 65535].
inline uint16_t scaleToUint16(float value, float clip)
{
    if (clip <= 0.f || value <= 0.f) return 0u;
    const float scaled = value * (65535.f / clip);
    return static_cast<uint16_t>(scaled < 65535.f ? scaled : 65535.f);
}

// Probe whether n 32-bit words look like IEEE 754 float32 acoustic data.
// Returns true when more than half the words are finite positive floats and
// the maximum is above subnormal noise (> 1e-20).
inline bool looksLikeFloat32(const uint32_t* data, uint32_t n)
{
    if (n == 0) return false;
    const float* fptr = reinterpret_cast<const float*>(data);
    float    fmax      = 0.f;
    uint32_t finite_pos = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (std::isfinite(fptr[i]) && fptr[i] > 0.f) {
            if (fptr[i] > fmax) fmax = fptr[i];
            ++finite_pos;
        }
    }
    return (finite_pos > n / 2) && (fmax > 1e-20f);
}

// Normalize n float32 samples (src) into dst[] as uint16.
// Non-finite or negative values map to 0.
// pct: percentile used as clip ceiling (default 0.99).
inline void normalizeFloat32Samples(const float*    src,
                                    uint16_t*       dst,
                                    uint32_t        n,
                                    float           pct = 0.99f)
{
    if (n == 0) return;
    std::vector<float> pos_vals;
    pos_vals.reserve(n);
    float fmax = 0.f;
    for (uint32_t i = 0; i < n; ++i) {
        if (std::isfinite(src[i]) && src[i] > 0.f) {
            pos_vals.push_back(src[i]);
            if (src[i] > fmax) fmax = src[i];
        }
    }
    float clip = pos_vals.empty() ? 0.f : percentileValue(pos_vals, pct);
    if (clip <= 0.f) clip = fmax;
    for (uint32_t i = 0; i < n; ++i) {
        const float v = (std::isfinite(src[i]) && src[i] >= 0.f) ? src[i] : 0.f;
        dst[i] = scaleToUint16(v, clip);
    }
}

// Normalize n uint32 samples (src) into dst[] as uint16.
// pct: percentile used as clip ceiling (default 0.99).
inline void normalizeUint32Samples(const uint32_t* src,
                                   uint16_t*       dst,
                                   uint32_t        n,
                                   float           pct = 0.99f)
{
    if (n == 0) return;
    std::vector<uint32_t> tmp(src, src + n);
    float clip = static_cast<float>(percentileValue(tmp, pct));
    if (clip <= 0.f)
        clip = static_cast<float>(*std::max_element(src, src + n));
    for (uint32_t i = 0; i < n; ++i)
        dst[i] = scaleToUint16(static_cast<float>(src[i]), clip);
}

// Normalize n uint8 samples (src) into dst[] as uint16.
// 8-bit sonar data is scaled 0–255 → 0–65535 before percentile normalisation.
inline void normalizeUint8Samples(const uint8_t*  src,
                                  uint16_t*       dst,
                                  uint32_t        n,
                                  float           pct = 0.99f)
{
    if (n == 0) return;
    std::vector<uint32_t> expanded(n);
    for (uint32_t i = 0; i < n; ++i)
        expanded[i] = static_cast<uint32_t>(src[i]) * 257u;   // 0-255 → 0-65535
    normalizeUint32Samples(expanded.data(), dst, n, pct);
}

} // namespace dolphin::io
