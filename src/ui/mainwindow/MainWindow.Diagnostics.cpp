// MainWindow.Diagnostics.cpp — SSS map diagnostics handler.
// Receives NavStats from SidescanViewController::mapDiagnosticsReady and
// posts Problems + Output log entries to DiagnosticsHub.
#include "ui/mainwindow/MainWindow.h"
#include "ui/bottom/DiagnosticsHub.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/sidescan/SidescanViewController.h"

namespace dolphin::ui {

void MainWindow::onMapDiagnosticsReady(const QString& layer_id, const NavStats& stats)
{
    m_diag_hub->clearProblems(layer_id);

    if (stats.total_pings == 0) {
        m_diag_hub->postProblem(
            tr("Layer contains no pings - file may be empty or unsupported"),
            DiagnosticsHub::Severity::Error, layer_id);
        return;
    }

    const double total = static_cast<double>(stats.total_pings);
    const auto pct = [&](size_t n) {
        return QString::number(100.0 * static_cast<double>(n) / total, 'f', 1);
    };

    // -- CRS -------------------------------------------------------------------
    if (!stats.unsupported_crs_id.empty()) {
        m_diag_hub->postProblem(
            tr("Unsupported coordinate system \"%1\" — positions approximated using "
               "pseudo-degree fallback. Re-import with a known UTM zone to fix.")
                .arg(QString::fromStdString(stats.unsupported_crs_id)),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    // -- Position --------------------------------------------------------------
    if (stats.invalid_nav == stats.total_pings) {
        m_diag_hub->postProblem(
            tr("No valid GPS position in any ping - check vessel nav source or file format"),
            DiagnosticsHub::Severity::Error, layer_id);
    } else if (static_cast<double>(stats.invalid_nav) / total > 0.5) {
        m_diag_hub->postProblem(
            tr("%1 of %2 pings (%3%) missing position - GPS dropout or wrong nav source")
                .arg(stats.invalid_nav).arg(stats.total_pings).arg(pct(stats.invalid_nav)),
            DiagnosticsHub::Severity::Error, layer_id);
    } else if (static_cast<double>(stats.invalid_nav) / total > 0.1) {
        m_diag_hub->postProblem(
            tr("%1 pings (%2%) missing position - partial GPS dropout")
                .arg(stats.invalid_nav).arg(pct(stats.invalid_nav)),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    // -- Nav repair transparency -----------------------------------------------
    // Bounded interpolation between trusted GPS fixes is a normal part of
    // georeferencing slow-cadence GPS (0.1 Hz fixes under 10 Hz pings cannot be
    // placed any other way), but the operator must be able to SEE how much of
    // the track is measured versus repaired. Every repaired ping passed the
    // hard speed/distance/cadence gates and carries kNavFlagInterpolated; no
    // positions are invented across line breaks, resets, or unbounded gaps.
    if (stats.interpolated_nav > 0) {
        m_diag_hub->postProblem(
            tr("%1 of %2 pings (%3%) positioned by bounded interpolation between "
               "GPS fixes - expected for slow GPS cadence; positions are never "
               "invented across line breaks or ping resets")
                .arg(stats.interpolated_nav).arg(stats.total_pings)
                .arg(pct(stats.interpolated_nav)),
            DiagnosticsHub::Severity::Info, layer_id);
    }

    // -- Heading ---------------------------------------------------------------
    if (stats.skipped_no_heading == stats.total_pings) {
        m_diag_hub->postProblem(
            tr("No heading data in any ping - select a heading source in Heading / Georeference settings"),
            DiagnosticsHub::Severity::Error, layer_id);
    } else if (static_cast<double>(stats.skipped_no_heading) / total > 0.5) {
        m_diag_hub->postProblem(
            tr("%1 pings (%2%) missing heading - check heading source selection")
                .arg(stats.skipped_no_heading).arg(pct(stats.skipped_no_heading)),
            DiagnosticsHub::Severity::Error, layer_id);
    } else if (stats.skipped_no_heading > 0) {
        m_diag_hub->postProblem(
            tr("%1 pings missing heading - swath coverage may have gaps")
                .arg(stats.skipped_no_heading),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    // -- Sonar samples ---------------------------------------------------------
    if (static_cast<double>(stats.skipped_no_samples) / total > 0.5) {
        m_diag_hub->postProblem(
            tr("%1 pings (%2%) have no sonar samples - recording may have been interrupted")
                .arg(stats.skipped_no_samples).arg(pct(stats.skipped_no_samples)),
            DiagnosticsHub::Severity::Warning, layer_id);
    } else if (stats.skipped_no_samples > 0) {
        m_diag_hub->postProblem(
            tr("%1 pings have no sonar samples - minor recording gaps")
                .arg(stats.skipped_no_samples),
            DiagnosticsHub::Severity::Info, layer_id);
    }

    // -- GPS fix quality -------------------------------------------------------
    // Repeated fixes are normal: GPS typically updates at 1–5 Hz while
    // sonar pings at 5–20 Hz, so 50–90% repetition is expected.
    // Only warn when GPS is essentially frozen (>90% identical).
    if (static_cast<double>(stats.repeated_fixes) / total > 0.90) {
        m_diag_hub->postProblem(
            tr("GPS nearly static: %1 of %2 pings (%3%) share identical position "
               "- GPS may not be updating or vessel was stationary")
                .arg(stats.repeated_fixes).arg(stats.total_pings).arg(pct(stats.repeated_fixes)),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    if (stats.nav_spikes > 10) {
        m_diag_hub->postProblem(
            tr("%1 isolated GPS spikes detected - position outliers were excluded from swath placement")
                .arg(stats.nav_spikes),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    // Spacing from a decimated preview is not the GPS update cadence. Only
    // issue this warning when every indexed ping group was loaded.
    const size_t loaded_groups = stats.total_pings / 2;
    const bool unthinned = stats.pings_available == 0
        || loaded_groups >= stats.pings_available;
    if (unthinned && stats.avg_spacing_m > 10.0 && stats.total_pings > 10) {
        m_diag_hub->postProblem(
            tr("Average ping spacing %1 m - GPS may be updating too slowly for vessel speed")
                .arg(stats.avg_spacing_m, 0, 'f', 1),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    // -- Preview / coverage output ---------------------------------------------
    if (stats.strips_built > 0 && stats.preview_pixels_written == 0) {
        m_diag_hub->postProblem(
            tr("Sonar preview built but no pixels rendered - heading may be incorrect or swath geometry degenerate"),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    if (stats.cells_attempted > 0
            && stats.cells_rasterized < stats.cells_attempted) {
        const size_t rejected = stats.cells_attempted - stats.cells_rasterized;
        const double rejected_fraction = static_cast<double>(rejected)
                                       / static_cast<double>(stats.cells_attempted);
        if (rejected_fraction > 0.02) {
            m_diag_hub->postProblem(
                tr("%1 of %2 sonar cells could not be rasterized - inspect navigation or swath geometry")
                    .arg(rejected).arg(stats.cells_attempted),
                DiagnosticsHub::Severity::Warning, layer_id);
        }
    }

    // Only fire "no ribbons" if position/heading aren't already flagged as
    // completely absent — those errors already explain it.
    const bool all_nav_failed     = (stats.invalid_nav        == stats.total_pings);
    const bool all_heading_failed = (stats.skipped_no_heading == stats.total_pings);
    if (stats.coverage_ribbons_built == 0 && !all_nav_failed && !all_heading_failed) {
        m_diag_hub->postProblem(
            tr("No coverage ribbons built - check position and heading sources"),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    // -- Stitch / rasterization quality ----------------------------------------
    if (stats.strips_built > 4) {
        const double attempts = static_cast<double>(stats.strips_built - 1);
        if (static_cast<double>(stats.stitch_heading_rejects) / attempts > 0.4) {
            m_diag_hub->postProblem(
                tr("%1 strip pairs rejected by heading mismatch (%2% of pairs) - heading source may be noisy")
                    .arg(stats.stitch_heading_rejects)
                    .arg(static_cast<int>(100.0 * stats.stitch_heading_rejects / attempts)),
                DiagnosticsHub::Severity::Warning, layer_id);
        }
        if (static_cast<double>(stats.stitch_nav_rejects) / attempts > 0.5) {
            m_diag_hub->postProblem(
                tr("%1 strip pairs split by nav gap - %2 survey line segment(s) detected")
                    .arg(stats.stitch_nav_rejects).arg(stats.time_gaps + 1),
                DiagnosticsHub::Severity::Info, layer_id);
        }
    }

    // -- View state problems ---------------------------------------------------
    if (stats.layer_active && !stats.layer_visible) {
        m_diag_hub->postProblem(
            tr("Active layer is hidden - enable visibility in the layer list to see it on the map"),
            DiagnosticsHub::Severity::Warning, layer_id);
    }

    // -- Output log summary ----------------------------------------------------
    const auto fcoord = [](double lo, double hi) -> QString {
        if (lo > 1e17 || hi < -1e17) return QStringLiteral("?");
        return QString("%1..%2").arg(lo, 0, 'f', 5).arg(hi, 0, 'f', 5);
    };

    // Ribbons note: when a raster preview exists, coverage ribbons are hidden
    // by paint order (image draws first, ribbons skipped).
    const QString ribbons_str =
        (stats.coverage_ribbons_built > 0 && stats.preview_pixels_written > 0)
        ? tr("%1 ribbons (hidden by raster)").arg(stats.coverage_ribbons_built)
        : tr("%1 ribbons").arg(stats.coverage_ribbons_built);

    const QString px_str = (stats.preview_pixels_filled > 0)
        ? tr("%1 px + %2 filled")
              .arg(stats.preview_pixels_written).arg(stats.preview_pixels_filled)
        : tr("%1 px").arg(stats.preview_pixels_written);

    const auto qualityName = [](MapSonarQuality q) -> QLatin1StringView {
        switch (q) {
        case MapSonarQuality::Off:          return QLatin1StringView("Off");
        case MapSonarQuality::CoverageOnly: return QLatin1StringView("CoverageOnly");
        case MapSonarQuality::Low:          return QLatin1StringView("Low");
        case MapSonarQuality::Medium:       return QLatin1StringView("Medium");
        case MapSonarQuality::High:         return QLatin1StringView("High");
        }
        return QLatin1StringView("?");
    };

    // Stage 1: build
    // total_pings = loaded channel records (port + stbd combined).
    // pings_available = ping groups in the original index (/ 2).
    m_diag_hub->logOutput(
        tr("[%1] %2 records · %3 strips · %4/%5 cells · %6 · %7 · "
           "%8 repaired-nav · %9 no-heading · %10 no-pos · %11 spikes · %12 gaps")
            .arg(layer_id)
            .arg(stats.total_pings)
            .arg(stats.strips_built)
            .arg(stats.cells_rasterized)
            .arg(stats.cells_attempted)
            .arg(px_str)
            .arg(ribbons_str)
            .arg(stats.interpolated_nav)
            .arg(stats.skipped_no_heading)
            .arg(stats.skipped_no_position)
            .arg(stats.nav_spikes)
            .arg(stats.time_gaps));

    if (stats.preview_pixels_filled > 0
            && stats.image_width > 0 && stats.image_height > 0) {
        const double total_px  = static_cast<double>(stats.image_width) * stats.image_height;
        const double raw_pct   = 100.0 * stats.preview_pixels_written / total_px;
        const double final_pct = 100.0 * (stats.preview_pixels_written
                                          + stats.preview_pixels_filled) / total_px;
        m_diag_hub->logOutput(
            tr("    fill=%1% raw / %2% final")
                .arg(raw_pct, 0, 'f', 1).arg(final_pct, 0, 'f', 1));
    }

    // Stage 2: placement
    m_diag_hub->logOutput(
        tr("    image %1×%2  "
           "nav bbox lon=[%3] lat=[%4]  "
           "strip bbox lon=[%5] lat=[%6]")
            .arg(stats.image_width).arg(stats.image_height)
            .arg(fcoord(stats.nav_lon_min,   stats.nav_lon_max))
            .arg(fcoord(stats.nav_lat_min,   stats.nav_lat_max))
            .arg(fcoord(stats.strip_lon_min, stats.strip_lon_max))
            .arg(fcoord(stats.strip_lat_min, stats.strip_lat_max)));
    m_diag_hub->logOutput(
        tr("    quality=%1  channels=%2  ping-groups=%3%4  CRS=%5")
            .arg(QLatin1String(qualityName(stats.quality_used)))
            .arg(stats.total_pings)
            .arg(stats.pings_available)
            .arg(stats.memory_reduced ? tr("  memory-reduced") : QString{})
            .arg(QString::fromStdString(stats.crs_label)));

    // Stage 3: view
    {
        const bool visible = stats.layer_visible;
        const QString paint_str = !visible
            ? QStringLiteral("skipped (hidden)")
            : QString("[%1,%2 %3×%4]")
                  .arg(stats.paint_rect.x()).arg(stats.paint_rect.y())
                  .arg(stats.paint_rect.width()).arg(stats.paint_rect.height());

        QString stage3 = tr("    visible=%1  active=%2  fit=%3  paint=%4")
            .arg(visible ? "yes" : "no")
            .arg(stats.layer_active ? "yes" : "no")
            .arg(stats.fit_applied  ? "auto" : "skipped (user interacted)")
            .arg(paint_str);

        if (visible) {
            const QRect vp = m_map_view ? m_map_view->rect() : QRect{};
            QLatin1StringView onscreen("no");
            if (!stats.paint_rect.isEmpty() && vp.intersects(stats.paint_rect))
                onscreen = vp.contains(stats.paint_rect)
                         ? QLatin1StringView("full") : QLatin1StringView("partial");
            stage3 += tr("  onscreen=%1").arg(QLatin1String(onscreen));
        }
        m_diag_hub->logOutput(stage3);
    }
}

} // namespace dolphin::ui
