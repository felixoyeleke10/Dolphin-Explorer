#pragma once
// SubBottomMapDiagnostics.h — post DiagnosticsHub problems and log output
//                              after a sub-bottom map build completes.

#include <QString>

namespace dolphin::ui {
struct TrackStats;
class DiagnosticsHub;

// Clear any existing problems for layer_id and post new ones based on stats.
// Also writes a one-line summary to the diagnostics output log.
// Must be called on the main thread (DiagnosticsHub is not thread-safe).
void postSubBottomMapDiagnostics(DiagnosticsHub*   hub,
                                  const QString&    layer_id,
                                  const TrackStats& stats);

} // namespace dolphin::ui
