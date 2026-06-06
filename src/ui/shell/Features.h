#pragma once

// -----------------------------------------------------------------------------
//  Dolphin Explorer — compile-time feature flags
//
//  Set a flag to false to fully exclude a unit from the build.
//  The compiler dead-strips all guarded code; no runtime overhead.
//
//  Rules:
//   • Activity bar buttons and menus for disabled units are omitted.
//   • The QStackedWidget still receives a placeholder page at the correct
//     index so that ContextPanel enum values stay stable across toggles.
//   • Services (ImportService, ProcessingService) are guarded independently.
// -----------------------------------------------------------------------------

namespace dolphin::Features {

// -- Panels (activity bar + context stack) ------------------------------------
inline constexpr bool kDataLibrary = true;    // PanelDataLibrary — sources, layers, contacts, issues
inline constexpr bool kNodeGraph  = true;    // PanelProcessing + Node menu
inline constexpr bool kContacts   = true;    // PanelContacts  — contact list
inline constexpr bool kAnalyze    = false;   // PanelAnalyze   — statistics  [Phase 2]
inline constexpr bool kAI         = false;   // PanelAI        — auto-detection  [Phase 2]
inline constexpr bool kPresent    = false;   // PanelPresent   — presentation  [Phase 2]
inline constexpr bool kReport     = false;   // PanelReport    — report generator  [Phase 2]

// -- Viewport add-ons ---------------------------------------------------------
inline constexpr bool kWaterfall  = true;    // SSS waterfall + ping scrubber

// -- Services -----------------------------------------------------------------
inline constexpr bool kImport     = true;    // ImportService  — file indexing
inline constexpr bool kProcessing = true;    // ProcessingService — node graph runner

} // namespace dolphin::Features
