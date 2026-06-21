// SidescanMapLoadTask.Build.cpp — buildSidescanLoadResult: the off-thread
// sidescan map build (raster fast-path, bounded preview index, ping load + nav
// correction + reprojection, coverage/track/raster, status pre-compute, raster
// persistence). Pure w.r.t. the controller; orchestration lives in
// SidescanMapLoadTask.cpp.
#include <QDebug>
#include "ui/features/map/sidescan/SidescanMapLoadParams.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/sidescan/SidescanEntryFilter.h"
#include "ui/features/map/sidescan/SidescanRasterCache.h"
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "app/services/ImportService.h"
#include "app/layers/DataLayer.h"
#include "app/display/NavCorrection.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"
#include "geo/GeoUtils.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace dolphin::ui {
namespace detail {

SidescanLoadResult buildSidescanLoadResult(const SssLoadInputs&            in,
                                           const std::function<void(int)>& report,
                                           app::CancellationToken          cancel)
{
    try {
    SidescanLoadResult result;
    result.layer_id   = in.layer_id;

    // -- Raster fast path: load the persisted raster, skip ping decode --
    // If a fresh cached raster exists, reconstruct the map data straight
    // from it (no store read, no rasterization). Status-bar summary is
    // persisted alongside so the UI is identical to a freshly-built load.
    {
        rastercache::Summary sum;
        if (rastercache::load(in.cache_path, in.cache_meta, result.layer_data, sum)) {
            result.raw_count          = 1;  // non-zero → not a load failure
            result.has_sample_nav     = sum.has_sample_nav;
            result.sample_lat         = sum.sample_lat;
            result.sample_lon         = sum.sample_lon;
            result.sample_alt_m       = sum.sample_alt;
            result.sample_is_proj     = sum.sample_is_proj;
            result.track_m            = sum.track_m;
            result.total_ssc_entries  = sum.total_ssc_entries;
            result.preview_port_count = sum.preview_port_count;
            result.quality_reduced    = sum.quality_reduced;
            // Colourise from the persisted intensity grid (palette captured
            // at load start; on_done re-LUTs if palette/params changed since).
            if (!result.layer_data.intensity_cache.empty()) {
                IntensityCache ic;
                ic.pixels    = result.layer_data.intensity_cache;
                ic.w         = result.layer_data.intensity_w;
                ic.h         = result.layer_data.intensity_h;
                ic.disp_low  = result.layer_data.intensity_disp_low;
                ic.disp_high = result.layer_data.intensity_disp_high;
                result.layer_data.preview_image =
                    SidescanViewController::colorizeIntensityCache(
                        ic, std::nullopt, in.palette_idx);
            }
            report(100);   // cache hit: instant
            return result;
        }
    }
    report(4);   // beginning the full build (cache miss)

    // -- Build bounded preview index -----------------------------------
    core::ArtifactIndex map_idx = in.idx;

    // For pinned single-band layers, remove the other frequency band.
    if (in.layer_low_freq_hz == 0.f)
        filterSidescanEntriesByBand(map_idx, in.layer_freq_hz);

    result.total_ssc_entries =
        map_idx.byType(core::ArtifactType::Sidescan).size();
    const size_t total_groups = result.total_ssc_entries / 2;

    // Thin to the quality-determined ping group cap.
    // max_ping_groups == 0 (High quality) means "use all pings", but
    // if the file exceeds kFullSafeLimit we fall back to Medium params.
    size_t effective_cap = in.qp.max_ping_groups;
    if (effective_cap == 0) {
        if (total_groups > kFullSafeLimit) {
            effective_cap = paramsForQuality(MapSonarQuality::Medium).max_ping_groups;
            result.quality_reduced = true;
        }
        // else: effective_cap stays 0 → no thinning below
    }

    if (effective_cap > 0) {
        auto thin = thinSidescanEntriesForMap(map_idx, effective_cap);
        map_idx.entries.erase(
            std::remove_if(map_idx.entries.begin(), map_idx.entries.end(),
                [](const core::ArtifactIndexEntry& e) {
                    return e.type == core::ArtifactType::Sidescan;
                }),
            map_idx.entries.end());
        map_idx.entries.insert(map_idx.entries.end(), thin.begin(), thin.end());
    }
    // else: Full quality within safe limit — load all ping groups as-is.

    if (cancel.isCancelled()) {
        result.load_failed = true; return result;
    }

    // Reading pings is the bulk of the work — map its 0..1 fraction to 5–60%.
    auto raw = in.svc->loadAllSidescanPingsFromStore(
        in.store_path, in.store_format, map_idx, in.source_path,
        in.qp.max_samples_per_ping,
        [&report](float f) { report(5 + static_cast<int>(f * 55.f)); });

    result.raw_count = raw.size();
    if (raw.empty()) {
        result.load_failed = true;
        return result;
    }

    if (in.apply_layer_crs) {
        for (auto& ping : raw) {
            if (ping.nav.is_projected && !ping.nav.spatial_ref.exact)
                ping.nav.spatial_ref = in.layer_src_ref;
        }
    }

    if (cancel.isCancelled()) {
        result.load_failed = true; return result;
    }

    // Display-time nav corrections (model-owned) — the SAME correction the
    // waterfall applies via WaterfallView::runNavCorrections, so the map and
    // waterfall agree. No-op when the layer has none; applied to the source
    // nav before normalize/reprojection.
    raw = applySidescanNavCorrections(std::move(raw), in.nav_params);
    report(66);   // pings read + nav-corrected; reprojecting next

    auto map_pings = geo::normalizeSidescanPingsForMap(
        std::move(raw), in.display_ref, &result.unresolved_crs);

    // Gain/imaging corrections (TVG/ARC/AGC + beam/ARN/destripe/ML) — the SAME
    // algorithms the waterfall applies (ui/shared/processing/SssImagingAlgorithms),
    // so the right-panel SSS tools render on the map mosaic, not only in the
    // waterfall. Applied in place, so the cached pings (kept below) reflect the
    // displayed corrected mosaic. No-op when the layer has no enabled corrections;
    // skipped when no raster is built (CoverageOnly).
    if (in.qp.max_image_dim > 0)
        imaging::applySssMapCorrections(map_pings, in.sss_params);

    // -- Coverage + nav track (always built for CoverageOnly+) ---------
    // is_projected must be set before the build calls: both functions use
    // it to pick degree vs. metre gap thresholds and bbox padding units.
    for (const auto& ping : map_pings)
        if (ping.nav.valid) {
            result.layer_data.is_projected = ping.nav.is_projected;
            break;
        }

    buildSwathNavTrack(map_pings, result.layer_data);
    buildSwathCoverage(map_pings, result.layer_data, in.georef_params);
    report(80);   // coverage + nav track built; rasterizing next

    // -- Sonar preview image (quality >= Low) --------------------------
    if (in.qp.max_image_dim > 0 && !cancel.isCancelled()) {
        const bool built = buildSwathPreviewImage(
            map_pings, result.layer_data,
            in.qp.max_image_dim, in.palette_idx, *cancel.flag(),
            in.georef_params, in.qp.min_strip_cos, in.qp.cell_budget_div);
        result.quality_reduced = built && result.layer_data.preview_reduced;
    }
    report(98);   // raster done; placing on the map
    // -- Build / CRS fields for diagnostics ---------------------------
    result.layer_data.nav_stats.quality_used    =
        result.quality_reduced ? MapSonarQuality::Medium : in.current_quality;
    result.layer_data.nav_stats.pings_available = total_groups;
    result.layer_data.nav_stats.memory_reduced  = result.quality_reduced;

    {
        std::string lbl;
        if (in.layer_src_ref.empty()) {
            lbl = "unknown";
        } else {
            const bool is_proj = core::spatialRefIsProjected(in.layer_src_ref);
            if (in.layer_src_ref.exact) {
                // Confirmed CRS — ID is reliable.
                if (!in.layer_src_ref.id.empty()) lbl = in.layer_src_ref.id + " ";
                lbl += is_proj ? "projected exact" : "geographic exact";
            } else {
                // Auto-detected — softer wording to avoid false confidence.
                lbl = is_proj ? "inferred projected" : "inferred geographic";
                if (!in.layer_src_ref.id.empty())
                    lbl += " (from " + in.layer_src_ref.id + ")";
            }
            if (in.apply_layer_crs) lbl += " [user override]";
        }
        result.layer_data.nav_stats.crs_label = std::move(lbl);
    }
    if (!result.unresolved_crs.empty())
        result.layer_data.nav_stats.unsupported_crs_id =
            result.unresolved_crs.front().id;

    // -- Pre-compute status bar values ---------------------------------
    for (const auto& ping : map_pings) {
        if (ping.nav.valid) {
            result.has_sample_nav = true;
            result.sample_lat     = ping.nav.lat;
            result.sample_lon     = ping.nav.lon;
            result.sample_alt_m   = ping.nav.altitude_m;
            result.sample_is_proj = ping.nav.is_projected;
            break;
        }
    }
    {
        bool           have_prev = false;
        core::NavPoint prev_nav;
        for (const auto& ping : map_pings) {
            if (ping.channel != core::SidescanChannel::Port || !ping.nav.valid)
                continue;
            ++result.preview_port_count;
            if (have_prev)
                result.track_m += geo::navDistanceMetres(prev_nav, ping.nav);
            prev_nav  = ping.nav;
            have_prev = true;
        }
    }

    // -- Persist the built raster (parse once, reuse forever) ----------
    // Write the intensity grid + coverage + nav track so the next open
    // takes the fast path above instead of decoding pings again. Derived
    // artifact — safe to delete; the fingerprint in cache_meta invalidates
    // it if the store, nav params, or quality tier change.
    if (!cancel.isCancelled()) {
        rastercache::Summary sum;
        sum.has_sample_nav     = result.has_sample_nav;
        sum.sample_lat         = result.sample_lat;
        sum.sample_lon         = result.sample_lon;
        sum.sample_alt         = result.sample_alt_m;
        sum.sample_is_proj     = result.sample_is_proj;
        sum.track_m            = result.track_m;
        sum.total_ssc_entries  = result.total_ssc_entries;
        sum.preview_port_count = result.preview_port_count;
        sum.quality_reduced    = result.quality_reduced;
        rastercache::save(in.cache_path, in.cache_meta, sum, result.layer_data);
    }

    // Keep the (corrected) normalized pings so a palette change can re-rasterize
    // without re-reading from disk.
    result.map_pings_cache = std::move(map_pings);

    return result;

    } catch (const std::exception& e) {
        qWarning() << "SidescanViewController BG: exception:" << e.what()
                   << "layer=" << QString::fromStdString(in.layer_id);
        SidescanLoadResult err;
        err.layer_id = in.layer_id; err.load_failed = true;
        return err;
    } catch (...) {
        qWarning() << "SidescanViewController BG: unknown exception"
                   << "layer=" << QString::fromStdString(in.layer_id);
        SidescanLoadResult err;
        err.layer_id = in.layer_id; err.load_failed = true;
        return err;
    }
}

} // namespace detail
} // namespace dolphin::ui
