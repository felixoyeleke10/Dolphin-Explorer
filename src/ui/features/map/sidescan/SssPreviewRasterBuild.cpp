// SssPreviewRasterBuild.cpp — buildSwathPreviewImage implementation.
// Georefs pings → stitched quadrilateral raster → QImage stored in LayerMapData.
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "ui/features/map/sidescan/SidescanSwathGeoreferencer.h"
#include "ui/features/map/sidescan/SssContinuity.h"
#include "ui/features/map/sidescan/SwathRasterizer.h"
#include "render/sonar/SonarDisplayParams.h"
#include "geo/GeoUtils.h"
#include "ui/shared/processing/SssImagingAlgorithms.h"

#include <QColor>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>

namespace dolphin::ui {

namespace {
constexpr double kDegToRad = std::numbers::pi / 180.0;

ssscontinuity::Thresholds stitchThresholds(
    const std::vector<const SssStrip*>& strips,
    bool is_projected)
{
    std::vector<double> nav_deltas;
    std::vector<double> time_deltas;
    std::vector<double> ping_deltas;
    if (strips.size() > 1) {
        nav_deltas.reserve(strips.size() - 1);
        time_deltas.reserve(strips.size() - 1);
        ping_deltas.reserve(strips.size() - 1);
    }

    for (size_t i = 1; i < strips.size(); ++i) {
        const auto& previous = *strips[i - 1];
        const auto& current  = *strips[i];

        core::NavPoint a{}, b{};
        a.lon = previous.nav_lon; a.lat = previous.nav_lat;
        b.lon = current.nav_lon;  b.lat = current.nav_lat;
        a.valid = b.valid = true;
        a.is_projected = b.is_projected = is_projected;
        nav_deltas.push_back(geo::navDistanceMetres(a, b));

        if (previous.timestamp_us > 0
                && current.timestamp_us > previous.timestamp_us)
            time_deltas.push_back(static_cast<double>(
                current.timestamp_us - previous.timestamp_us));

        // Near-simultaneous multi-frequency records can log a few ping
        // numbers out of strict order, so learn the cadence from the
        // magnitude of the step rather than only forward-increasing pairs.
        if (previous.ping_number > 0 && current.ping_number > 0)
            ping_deltas.push_back(std::fabs(
                static_cast<double>(current.ping_number)
                - static_cast<double>(previous.ping_number)));
    }

    return ssscontinuity::fromDeltas(
        std::move(nav_deltas), std::move(time_deltas), std::move(ping_deltas));
}

SssPoint interpolateStripPointAtRange(const std::vector<SssPoint>& points,
                                      double ground_range_m)
{
    if (points.size() == 1)
        return points.front();
    if (ground_range_m <= points.front().ground_range_m)
        return points.front();
    if (ground_range_m >= points.back().ground_range_m)
        return points.back();

    const auto upper = std::upper_bound(
        points.begin(), points.end(), ground_range_m,
        [](double range_m, const SssPoint& point) {
            return range_m < point.ground_range_m;
        });
    if (upper == points.begin() || upper == points.end())
        return upper == points.begin() ? points.front() : points.back();
    const auto& b = *upper;
    const auto& a = *(upper - 1);
    const double span_m = b.ground_range_m - a.ground_range_m;
    if (!(span_m > 1e-9))
        return b;
    const double fraction = std::clamp(
        (ground_range_m - a.ground_range_m) / span_m, 0.0, 1.0);
    const double amplitude = static_cast<double>(a.amplitude)
        + (static_cast<double>(b.amplitude) - static_cast<double>(a.amplitude))
          * fraction;
    return {
        a.lon + (b.lon - a.lon) * fraction,
        a.lat + (b.lat - a.lat) * fraction,
        static_cast<uint16_t>(std::clamp(std::llround(amplitude), 0LL, 65535LL)),
        static_cast<float>(a.ground_range_m + span_m * fraction),
        // A geometry-only inner anchor suppresses the exact nadir and the cell
        // that starts there, not the entire interval to the first real sample.
        // This matters for coarse/unequal strips where that interval can span
        // much of the swath.
        b.renderable && (a.renderable || fraction > 1e-9)
    };
}
} // namespace

bool buildSwathPreviewImage(const std::vector<core::SidescanPing>& pings,
                            LayerMapData& ld,
                            int max_image_dim,
                            int palette_index,
                            const std::atomic_bool& cancelled,
                            const SssGeorefParams& georef_params,
                            double min_strip_cos,
                            int    cell_budget_div,
                            bool   ping_lines_only,
                            const std::function<void(float)>& progress,
                            float canonical_stretch_low,
                            float canonical_stretch_high)
{
    // Throttle progress to integer-percent buckets so a callback that marshals
    // across threads isn't flooded; no-op when no callback was supplied.
    int last_pct = -1;
    auto report = [&](float f) {
        if (!progress) return;
        const int p = std::clamp(static_cast<int>(f * 100.f), 0, 100);
        if (p != last_pct) { last_pct = p; progress(f); }
    };

    ld.preview_image   = QImage{};
    ld.preview_reduced = false;

    // Reset stitch counters populated below; nav-track counters are left as-is.
    ld.nav_stats.stitch_nav_rejects     = 0;
    ld.nav_stats.stitch_time_rejects    = 0;
    ld.nav_stats.stitch_ping_rejects    = 0;
    ld.nav_stats.stitch_heading_rejects = 0;
    ld.nav_stats.cells_attempted        = 0;
    ld.nav_stats.cells_rasterized       = 0;
    ld.nav_stats.preview_pixels_written = 0;
    ld.nav_stats.preview_pixels_filled  = 0;

    if (pings.empty()) return false;
    if (ld.lon_min >= ld.lon_max || ld.lat_min >= ld.lat_max) return false;

    // -- Compute image dimensions in metre-correct proportions ----------------
    // Apply cos(center_lat) to the longitude extent so the image has equal
    // pixels-per-metre in both axes.  MapView's Mercator geoToPixel applies the
    // same factor when placing the image on screen, giving a distortion-free mosaic.
    const double dlon     = ld.lon_max - ld.lon_min;
    const double dlat     = ld.lat_max - ld.lat_min;
    const double cen_lat  = (ld.lat_min + ld.lat_max) * 0.5;
    const double cos_cen  = ld.is_projected
        ? 1.0
        : std::max(0.01, std::cos(cen_lat * kDegToRad));
    const double dlon_m   = ld.is_projected ? dlon : dlon * cos_cen * 111320.0;
    const double dlat_m   = ld.is_projected ? dlat : dlat * 111320.0;
    const double scale    = static_cast<double>(max_image_dim) / std::max(dlon_m, dlat_m);

    int img_w = std::clamp(static_cast<int>(std::round(dlon_m * scale)), 4, max_image_dim);
    int img_h = std::clamp(static_cast<int>(std::round(dlat_m * scale)), 4, max_image_dim);

    // -- Hard memory cap: 64 MB (4 bytes per pixel) ----------------------------
    constexpr int64_t kMaxBytes = 64LL * 1024 * 1024;
    if (static_cast<int64_t>(img_w) * img_h * 4 > kMaxBytes) {
        const double factor = std::sqrt(static_cast<double>(kMaxBytes) /
                              (static_cast<double>(img_w) * img_h * 4));
        img_w = std::max(4, static_cast<int>(img_w * factor));
        img_h = std::max(4, static_cast<int>(img_h * factor));
        ld.preview_reduced = true;
    }

    // Per-cell pixel budget: 0 = disabled (Full quality)
    const int max_cell_pix = (cell_budget_div > 0)
        ? std::max(8, std::min(img_w, img_h) / cell_budget_div)
        : std::numeric_limits<int>::max();

    // Record resolved image dimensions before any early return.
    ld.nav_stats.image_width  = img_w;
    ld.nav_stats.image_height = img_h;

    // -- Georeference pings → strips -------------------------------------------
    auto gr = georeferenceSidescanPings(pings, georef_params);
    ld.nav_stats.skipped_no_position = gr.skipped_no_position;
    ld.nav_stats.skipped_no_heading  = gr.skipped_no_heading;
    ld.nav_stats.skipped_no_samples  = gr.skipped_no_samples;
    ld.nav_stats.strips_built        = gr.strips.size();

    // Strip centre bbox — where the georeferenced data actually landed.
    // Comparing this against nav_lon/lat_min/max reveals CRS or placement bugs.
    {
        double slo =  1e18, shi = -1e18, sla =  1e18, sla2 = -1e18;
        for (const auto& st : gr.strips) {
            slo  = std::min(slo,  st.nav_lon);
            shi  = std::max(shi,  st.nav_lon);
            sla  = std::min(sla,  st.nav_lat);
            sla2 = std::max(sla2, st.nav_lat);
        }
        ld.nav_stats.strip_lon_min = slo;
        ld.nav_stats.strip_lon_max = shi;
        ld.nav_stats.strip_lat_min = sla;
        ld.nav_stats.strip_lat_max = sla2;
    }

    if (gr.strips.empty()) return false;
    if (cancelled.load(std::memory_order_relaxed)) return false;
    report(0.15f);   // georeferencing done; stitch/raster next (the bulk)

    // -- Create transparent image + intensity cache ----------------------------
    // Rasterized cells write fully-opaque pixels; cells with no data stay
    // transparent (alpha 0). The paint path applies only the layer's explicit
    // opacity; it does not silently fade valid sonar pixels into the basemap.
    QImage img(img_w, img_h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    // Parallel uint16 intensity buffer: 0 = no data, 1-65535 = raw amplitude+1.
    // Populated alongside the colour image so palette changes never need to
    // re-read pings from disk or redo any geometry work.
    ld.intensity_cache.assign(static_cast<size_t>(img_w) * img_h, 0);
    ld.intensity_w = img_w;
    ld.intensity_h = img_h;
    uint16_t* amp_buf = ld.intensity_cache.data();

    const double sx = static_cast<double>(img_w) / dlon;
    const double sy = static_cast<double>(img_h) / dlat;

    auto geoToImg = [&](double lon, double lat) -> QPointF {
        return { (lon - ld.lon_min) * sx,
                 (ld.lat_max - lat) * sy };
    };

    // A supplied line-context stretch is the cross-view contrast authority.
    // Local computation is retained only for callers without a context;
    // geometry sampling must never define a second contrast policy.
    const auto stretch = imaging::computeAutoStretch(pings);
    const bool canonical_stretch = std::isfinite(canonical_stretch_low)
        && std::isfinite(canonical_stretch_high)
        && canonical_stretch_low >= 0.f
        && canonical_stretch_high > canonical_stretch_low;
    const float disp_low = canonical_stretch
        ? canonical_stretch_low : stretch.low;
    const float disp_high = canonical_stretch
        ? canonical_stretch_high : stretch.high;
    ld.intensity_disp_low  = disp_low;
    ld.intensity_disp_high = disp_high;

    SonarDisplayParams params;
    params.display_low  = disp_low;
    params.display_high = disp_high;
    params.palette      = palette_index;

    SwathRasterizer rast;
    rast.buildLut(params, palette_index);

    QRgb* pixels = reinterpret_cast<QRgb*>(img.bits());

    // -- Debug mode: skip stitching; render each strip as individual sample dots --
    // Enabled by the explicit flag or by georef_params.debug_ping_lines_only.
    // If stretching disappears in this mode, the stitch logic is the root cause.
    if (ping_lines_only || georef_params.debug_ping_lines_only) {
        size_t dots_written = 0;
        for (const auto& st : gr.strips) {
            for (const auto& pt : st.points) {
                if (!pt.renderable) continue;
                const QPointF px = geoToImg(pt.lon, pt.lat);
                const int ix = static_cast<int>(px.x());
                const int iy = static_cast<int>(px.y());
                if (ix >= 0 && ix < img_w && iy >= 0 && iy < img_h) {
                    const size_t idx = static_cast<size_t>(iy) * img_w + ix;
                    if (amp_buf[idx] == 0)
                        ++dots_written;
                    const float norm = SSSAmplitudeProcessor::displayIntensity(pt.amplitude, params);
                    pixels[idx] = SSSPalette::color(norm, palette_index);
                    const int ai = static_cast<int>(pt.amplitude);
                    amp_buf[idx] =
                        static_cast<uint16_t>(ai < 65535 ? ai + 1 : 65535);
                }
            }
        }
        ld.nav_stats.preview_pixels_written = dots_written;
        ld.preview_image = std::move(img);
        report(1.0f);
        return true;
    }

    // -- Rasterize per-channel: group strip indices by channel first -----------
    // Strips are interleaved port/stbd — separate before iterating pairs.
    //
    // Gap decision uses the actual vessel-centre nav stored on each strip
    // (strip.nav_lon/nav_lat from ping.nav) rather than the near-field swath
    // point, which is offset perpendicularly from the vessel and cannot serve
    // as a reliable distance proxy.
    //
    // Continuity is estimated independently for each retained channel sequence.
    // This is essential after index thinning: consecutive decoded records may
    // legitimately be tens of ping numbers and several seconds apart.
    const bool is_proj = gr.is_projected;

    // Stitch progress spans 0.15 → 0.85 across all strips of both channels.
    const size_t strips_total = std::max<size_t>(1, gr.strips.size());
    size_t       strips_done  = 0;

    for (const auto ch : {core::SidescanChannel::Port, core::SidescanChannel::Starboard}) {
        std::vector<const SssStrip*> ch_strips;
        ch_strips.reserve(gr.strips.size() / 2 + 1);
        for (const auto& st : gr.strips)
            if (st.channel == ch) ch_strips.push_back(&st);

        const ssscontinuity::Thresholds thresholds =
            stitchThresholds(ch_strips, is_proj);

        for (size_t si = 1; si < ch_strips.size(); ++si) {
            if (cancelled.load(std::memory_order_relaxed)) return false;
            if ((++strips_done & 0x3F) == 0)   // every 64 strips
                report(0.15f + 0.70f * static_cast<float>(strips_done) / strips_total);

            const auto& prev = *ch_strips[si - 1];
            const auto& curr = *ch_strips[si];

            if (prev.points.empty() || curr.points.empty()) continue;
            if (prev.points.size() < 2 || curr.points.size() < 2) continue;

            // Decimation omissions are intentionally stitchable, but a decoded
            // record that was explicitly rejected or could not produce geometry
            // advances this per-channel segment in the georeferencer. Never paint
            // across that known data-quality hole.
            if (prev.continuity_segment != curr.continuity_segment)
                continue;

            // Nav-centre distance guard.
            {
                core::NavPoint nav_prev{}, nav_curr{};
                nav_prev.lon = prev.nav_lon;  nav_prev.lat = prev.nav_lat;
                nav_prev.valid = true;
                nav_prev.is_projected = is_proj;
                nav_curr.lon = curr.nav_lon;  nav_curr.lat = curr.nav_lat;
                nav_curr.valid = true;
                nav_curr.is_projected = is_proj;
                if (geo::navDistanceMetres(nav_prev, nav_curr) > thresholds.nav_gap_m) {
                    ++ld.nav_stats.stitch_nav_rejects;
                    continue;
                }
            }

            // Timestamp cadence guard (adaptive after preview thinning).
            if (prev.timestamp_us > 0 && curr.timestamp_us > 0
                    && curr.timestamp_us - prev.timestamp_us > thresholds.time_gap_us) {
                ++ld.nav_stats.stitch_time_rejects;
                continue;
            }

            // Ping-number cadence guard (XTF only; JSF has ping_number == 0).
            // Near-simultaneous multi-frequency records can log a few ping
            // numbers out of strict order, so gate on step magnitude rather
            // than direction — a small backward step is the same cadence
            // noise as a small forward one; only a step past the learned
            // tolerance is a real stitch break.
            if (prev.ping_number > 0 && curr.ping_number > 0
                    && std::fabs(static_cast<double>(curr.ping_number)
                                 - static_cast<double>(prev.ping_number))
                       > thresholds.ping_gap) {
                ++ld.nav_stats.stitch_ping_rejects;
                continue;
            }

            // Heading-change guard: dot-product of near→far cross-track vectors.
            {
                const auto& pp0 = prev.points.front();
                const auto& ppL = prev.points.back();
                const auto& cp0 = curr.points.front();
                const auto& cpL = curr.points.back();
                const double apx = ppL.lon - pp0.lon,  apy = ppL.lat - pp0.lat;
                const double acx = cpL.lon - cp0.lon,  acy = cpL.lat - cp0.lat;
                const double lenA = std::hypot(apx, apy);
                const double lenC = std::hypot(acx, acy);
                if (lenA > 1e-12 && lenC > 1e-12 && min_strip_cos > 0.0) {
                    const double dot = (apx * acx + apy * acy) / (lenA * lenC);
                    if (dot < min_strip_cos) {
                        ++ld.nav_stats.stitch_heading_rejects;
                        continue;
                    }
                }
            }

            // Pair adjacent strips on their authoritative physical ground-range
            // coordinate. Index-normalized pairing warps non-uniform/unequal
            // sample grids, connecting different seabed ranges to one another.
            // Clamp the shorter strip at its recorded inner/outer edge so range
            // changes taper as triangles without extrapolating amplitude.
            const double range_min = std::min(
                prev.points.front().ground_range_m,
                curr.points.front().ground_range_m);
            const double range_max = std::max(
                prev.points.back().ground_range_m,
                curr.points.back().ground_range_m);
            const double range_span_m = range_max - range_min;
            if (!std::isfinite(range_span_m) || range_span_m <= 1e-9)
                continue;

            const size_t source_segments =
                std::max(prev.points.size(), curr.points.size()) - 1;
            const auto stripLengthPixels = [&](const SssStrip& strip) {
                const QPointF first = geoToImg(
                    strip.points.front().lon, strip.points.front().lat);
                const QPointF last = geoToImg(
                    strip.points.back().lon, strip.points.back().lat);
                return std::hypot(last.x() - first.x(), last.y() - first.y());
            };
            const auto stripPixelsPerMetre = [&](const SssStrip& strip) {
                const double span_m = strip.points.back().ground_range_m
                                    - strip.points.front().ground_range_m;
                return span_m > 1e-9 ? stripLengthPixels(strip) / span_m : 0.0;
            };
            // At most two cells per output pixel across-track. More source
            // samples cannot add spatial detail to this raster and would make a
            // high-tier overview perform millions of redundant sub-pixel writes.
            const double domain_pixels = range_span_m * std::max(
                stripPixelsPerMetre(prev), stripPixelsPerMetre(curr));
            size_t segment_count = source_segments;
            const double desired_segments = 2.0 * domain_pixels;
            if (std::isfinite(desired_segments) && desired_segments > 0.0) {
                // Clamp in floating point before converting to size_t. This
                // avoids undefined/out-of-range conversion for malformed
                // geometry while never requesting more source detail.
                const double bounded = std::min(
                    desired_segments, static_cast<double>(source_segments));
                segment_count = std::max<size_t>(1,
                    static_cast<size_t>(std::ceil(bounded)));
            }

            for (size_t j = 0; j < segment_count; ++j) {
                const double t0 = static_cast<double>(j)
                                / static_cast<double>(segment_count);
                const double t1 = static_cast<double>(j + 1)
                                / static_cast<double>(segment_count);
                const double range0_m = range_min + range_span_m * t0;
                const double range1_m = range_min + range_span_m * t1;
                const SssPoint pa = interpolateStripPointAtRange(
                    prev.points, range0_m);
                const SssPoint na = interpolateStripPointAtRange(
                    prev.points, range1_m);
                const SssPoint pb = interpolateStripPointAtRange(
                    curr.points, range0_m);
                const SssPoint nb = interpolateStripPointAtRange(
                    curr.points, range1_m);
                if (!pa.renderable || !na.renderable
                        || !pb.renderable || !nb.renderable)
                    continue;
                ++ld.nav_stats.cells_attempted;
                const size_t writes = rast.rasterizeCell(
                    pixels, img_w, img_h,
                    geoToImg(pa.lon, pa.lat), geoToImg(na.lon, na.lat),
                    geoToImg(pb.lon, pb.lat), geoToImg(nb.lon, nb.lat),
                    pa.amplitude, na.amplitude, pb.amplitude, nb.amplitude,
                    max_cell_pix,
                    amp_buf);
                if (writes > 0)
                    ++ld.nav_stats.cells_rasterized;
            }
        }
    }

    if (cancelled.load(std::memory_order_relaxed)) return false;
    report(0.85f);   // stitch done; pixel accounting next

    // Count raw non-transparent pixels.  If nothing was rasterized, discard the
    // image so MapView falls back to drawing the coverage ribbons instead of
    // painting a fully-transparent rectangle that silently hides them.
    size_t px_written = 0;
    {
        const QRgb* bits  = reinterpret_cast<const QRgb*>(img.constBits());
        const int   total = img_w * img_h;
        for (int k = 0; k < total; ++k)
            if (qAlpha(bits[k]) > 0) ++px_written;
    }

    // No post-hoc hole filling: conservative quad coverage handles sub-pixel
    // geometry directly. Any remaining transparency represents an actual break
    // or rejected cell and must stay visible for honest QC.
    constexpr size_t px_filled = 0;

    ld.nav_stats.preview_pixels_written = px_written;
    ld.nav_stats.preview_pixels_filled  = px_filled;
    report(1.0f);   // raster complete

    if (px_written > 0) {
        ld.preview_image = std::move(img);
    } else {
        // No data rasterized — discard intensity cache too so MapView falls back
        // to drawing coverage ribbons.
        ld.intensity_cache.clear();
        ld.intensity_w = 0;
        ld.intensity_h = 0;
    }
    return true;
}

} // namespace dolphin::ui
