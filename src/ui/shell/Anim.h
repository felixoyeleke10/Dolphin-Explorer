#pragma once
#include <QEasingCurve>

namespace dolphin::ui::Anim {

// ---------------------------------------------------------------------------
// Duration tiers — milliseconds
//
//  kFast     140   instant toggle/check response (WfToggleRow binary state)
//  kIn       160   approach: hover-enter, panel open, picker expand
//  kSection  180   symmetric section expand/collapse (goes either direction)
//  kOut      220   retreat: hover-leave, panel close (longer = more graceful)
// ---------------------------------------------------------------------------
inline constexpr int kFast    = 140;
inline constexpr int kIn      = 160;
inline constexpr int kSection = 180;
inline constexpr int kOut     = 220;

// ---------------------------------------------------------------------------
// Easing curves
//
//  kEaseOut    OutCubic    directional motion (approach / expand)
//  kEaseInOut  InOutCubic  symmetric expand/collapse (same feel either way)
//  kEaseToggle InOutQuad   snappy binary toggle (softer than cubic)
// ---------------------------------------------------------------------------
inline constexpr QEasingCurve::Type kEaseOut    = QEasingCurve::OutCubic;
inline constexpr QEasingCurve::Type kEaseInOut  = QEasingCurve::InOutCubic;
inline constexpr QEasingCurve::Type kEaseToggle = QEasingCurve::InOutQuad;

} // namespace dolphin::ui::Anim
