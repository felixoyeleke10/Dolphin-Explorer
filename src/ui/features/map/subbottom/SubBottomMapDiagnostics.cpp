// SubBottomMapDiagnostics.cpp — postSubBottomMapDiagnostics implementation.
// Analyses TrackStats produced by buildTrackLayerMapData and posts
// actionable problems + a diagnostic log line to DiagnosticsHub.
#include "ui/features/map/subbottom/SubBottomMapDiagnostics.h"
#include "ui/features/map/MapTypes.h"
#include "ui/bottom/DiagnosticsHub.h"

#include <QCoreApplication>

namespace dolphin::ui {

namespace {
// Localisation wrapper for a free-function context.
inline QString T(const char* s)
{
    return QCoreApplication::translate("SubBottomMapDiagnostics", s);
}
} // namespace

void postSubBottomMapDiagnostics(DiagnosticsHub*   hub,
                                  const QString&    layer_id,
                                  const TrackStats& s)
{
    hub->clearProblems(layer_id);

    // -- Emptiness -------------------------------------------------------------
    if (s.total_traces == 0) {
        hub->postProblem(
            T("Layer contains no sub-bottom traces — file may be empty or unsupported"),
            DiagnosticsHub::Severity::Error, layer_id);
        return;
    }

    const double total = static_cast<double>(s.total_traces);
    auto pct = [&](size_t n) -> int {
        return static_cast<int>(100.0 * static_cast<double>(n) / total + 0.5);
    };

    // -- Navigation quality ----------------------------------------------------
    const size_t bad_nav = s.invalid_nav + s.zero_coords;
    if (bad_nav == s.total_traces) {
        hub->postProblem(
            T("No valid GPS position in any trace — check nav source or CRS setting"),
            DiagnosticsHub::Severity::Error, layer_id);
    } else if (static_cast<double>(bad_nav) / total > 0.5) {
        hub->postProblem(
            QCoreApplication::translate("SubBottomMapDiagnostics",
                "%1 of %2 traces (%3%) have unusable position — GPS dropout or wrong nav source")
                .arg(bad_nav).arg(s.total_traces).arg(pct(bad_nav)),
            DiagnosticsHub::Severity::Error, layer_id);
    } else if (static_cast<double>(bad_nav) / total > 0.1) {
        hub->postProblem(
            QCoreApplication::translate("SubBottomMapDiagnostics",
                "%1 traces (%2%) have unusable position — partial GPS dropout")
                .arg(bad_nav).arg(pct(bad_nav)),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    if (s.zero_coords > 0) {
        hub->postProblem(
            QCoreApplication::translate("SubBottomMapDiagnostics",
                "%1 trace(s) have zero coordinates (0°, 0°) — typical of recording gaps or missing nav")
                .arg(s.zero_coords),
            DiagnosticsHub::Severity::Info, layer_id);
    }

    // -- Repeated fixes --------------------------------------------------------
    const size_t plotted_plus_rep = s.track_points + s.repeated_fixes;
    if (plotted_plus_rep > 0 && s.repeated_fixes > 0) {
        const double rep_pct =
            100.0 * static_cast<double>(s.repeated_fixes) /
            static_cast<double>(plotted_plus_rep);
        if (rep_pct > 90.0) {
            hub->postProblem(
                QCoreApplication::translate("SubBottomMapDiagnostics",
                    "GPS nearly static: %1 of %2 traces (%3%) share an identical position — "
                    "GPS may not be updating or vessel was stationary")
                    .arg(s.repeated_fixes).arg(plotted_plus_rep)
                    .arg(static_cast<int>(rep_pct + 0.5)),
                DiagnosticsHub::Severity::Warning, layer_id);
        }
    }

    // -- Trace spacing ---------------------------------------------------------
    if (s.avg_spacing_m > 10.0 && s.track_points > 10) {
        hub->postProblem(
            QCoreApplication::translate("SubBottomMapDiagnostics",
                "Average trace spacing %1 m — GPS may be updating too slowly for vessel speed")
                .arg(s.avg_spacing_m, 0, 'f', 1),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    // -- Segment gaps ----------------------------------------------------------
    if (s.large_jumps > 0) {
        hub->postProblem(
            s.large_jumps == 1
                ? T("1 large coordinate jump — track split into 2 segments")
                : QCoreApplication::translate("SubBottomMapDiagnostics",
                      "%1 large coordinate jumps — track split into %2 segments")
                      .arg(s.large_jumps).arg(s.segment_count),
            DiagnosticsHub::Severity::Info, layer_id);
    }

    // -- View state ------------------------------------------------------------
    if (!s.layer_visible) {
        hub->postProblem(
            T("Layer is hidden — enable visibility in the layer list to see it on the map"),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    // -- Output log summary ----------------------------------------------------
    hub->logOutput(
        QCoreApplication::translate("SubBottomMapDiagnostics",
            "[%1] %2 traces · %3 plotted · %4 invalid nav · %5 zero-coord · "
            "%6 repeated · %7 segment(s) · spacing avg=%8 m max=%9 m · CRS=%10")
            .arg(layer_id)
            .arg(s.total_traces)
            .arg(s.track_points)
            .arg(s.invalid_nav)
            .arg(s.zero_coords)
            .arg(s.repeated_fixes)
            .arg(s.segment_count)
            .arg(s.avg_spacing_m, 0, 'f', 1)
            .arg(s.max_spacing_m, 0, 'f', 1)
            .arg(QString::fromStdString(s.crs_label)));

    if (s.has_endpoints) {
        hub->logOutput(
            QCoreApplication::translate("SubBottomMapDiagnostics",
                "    first=(%1, %2)  last=(%3, %4)")
                .arg(s.first_lat, 0, 'f', 6).arg(s.first_lon, 0, 'f', 6)
                .arg(s.last_lat,  0, 'f', 6).arg(s.last_lon,  0, 'f', 6));
    }
}

} // namespace dolphin::ui
