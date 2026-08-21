#pragma once
#include "core/SidescanPing.h"   // SidescanChannel
#include <cstdint>
#include <string>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  WfContact — a single-point contact pick placed on the waterfall canvas.
//
//  Contacts are point annotations (boulder, debris, anomaly, …).
//  Shape annotations (polygons, polylines) belong to WfFeature (Phase 2).
//
//  Stored relative to the currently loaded window (row_idx is window-local).
//  WaterfallWindow converts to core::Contact (with artifact_id / lat / lon)
//  when persisting to the project.
// -----------------------------------------------------------------------------

// Classification labels for point contacts.
enum class ContactClass {
    Boulder,
    Debris,
    Cable,
    Pipeline,   // order matches the classification combo in WaterfallAnalysisPanelContact
    Anomaly,
    Unknown,
};

struct WfContact {
    int                   row_idx        = 0;
    core::SidescanChannel ch             = core::SidescanChannel::Port;
    float                 range_m        = 0.f;
    ContactClass          classification = ContactClass::Unknown;
    uint64_t              id             = 0;   // project contact id (0 = local echo,
                                                //  not yet round-tripped via the bus)
    std::string           symbol;               // shared contact glyph id
    uint32_t              color_rgb      = 0;   // 0xAARRGGBB; 0 = viewer default
};

} // namespace dolphin::ui
