#pragma once
#include "core/NavPoint.h"
#include <cstdint>

namespace dolphin::core {

// One magnetometer reading — extracted from XTF packet headers or dedicated
// mag files. Total-field magnetometry is the primary use case.
struct MagSample {
    uint64_t id           = 0;
    int64_t  timestamp_us = 0;  // µs since Unix epoch
    NavPoint nav;

    // Raw sensor values (nT). Components may be 0 if sensor is scalar-only.
    float    total_nT     = 0.f;
    float    x_nT         = 0.f;
    float    y_nT         = 0.f;
    float    z_nT         = 0.f;

    // Corrections (0 if not yet applied)
    float    diurnal_nT   = 0.f;  // diurnal drift removed from base station
    float    igrf_nT      = 0.f;  // IGRF reference field at this location
    float    residual_nT  = 0.f;  // total - diurnal - igrf (the useful signal)
};

} // namespace dolphin::core
