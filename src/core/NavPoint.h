#pragma once
#include <cstdint>
#include "core/SpatialRef.h"

namespace dolphin::core {

struct NavPoint {
    double  lat          = 0.0;   // WGS84 degrees, or projected metres when is_projected == true
    double  lon          = 0.0;

    // Resolved heading (legacy field used by existing code paths).
    // For sidescan georeferencing prefer the explicit source fields below.
    float   heading_deg  = 0.0f;

    // Raw position sources — preserved separately so nav source can be user-controlled.
    // Both are 0.0/false when absent from the file.
    double  fish_lat         = 0.0;   // sensor / towfish GPS latitude
    double  fish_lon         = 0.0;
    bool    fish_nav_valid   = false;
    double  vessel_lat       = 0.0;   // vessel / ship GPS latitude
    double  vessel_lon       = 0.0;
    bool    vessel_nav_valid = false;

    // Distinct heading sources — preserved from raw file fields.
    // 0.0 means "field not populated" (XTF/JSF convention for missing data).
    float   sensor_heading_deg = 0.0f;  // towfish / fish-head AHRS  (XTF: SensorHeading)
    float   ship_heading_deg   = 0.0f;  // vessel gyro / compass     (XTF: ShipGyro)

    float   speed_kn     = 0.0f;
    float   altitude_m   = 0.0f;  // sensor altitude above seabed
    float   pitch_deg    = 0.0f;
    float   roll_deg     = 0.0f;
    float   heave_m      = 0.0f;
    double  timestamp    = 0.0;   // Unix epoch, seconds
    bool    valid        = false;
    SpatialRef spatial_ref;       // explicit coordinate reference for lat/lon
    bool    is_projected = false;  // true when lat/lon are projected metres (UTM etc.), not WGS84 degrees
};

} // namespace dolphin::core
