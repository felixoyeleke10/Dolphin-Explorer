#pragma once
#include "core/SidescanPing.h"   // SidescanChannel

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  WfContact — a single-point contact pick placed on the waterfall canvas.
//
//  Contacts are point annotations (boulder, debris, anomaly, …).
//  Shape annotations (polygons, polylines) belong to WfFeature (Phase 2).
//
//  Stored relative to the currently loaded window (row_idx is window-local).
//  WaterfallWindow converts to core::Contact (with artifact_id / lat / lon)
//  when persisting to the project.
// ─────────────────────────────────────────────────────────────────────────────

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
};

} // namespace dolphin::ui
