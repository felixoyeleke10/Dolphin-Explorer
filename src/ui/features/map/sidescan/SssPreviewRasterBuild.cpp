// SssPreviewRasterBuild.cpp — buildSwathPreviewImage implementation.
// Georefs pings → stitched bilinear quad raster → QImage stored in LayerMapData.
#include "ui/features/map/sidescan/SssMapBuild.h"
#include "ui/features/map/sidescan/SidescanSwathGeoreferencer.h"
#include "ui/features/map/sidescan/SwathRasterizer.h"
#include "render/sonar/SonarDisplayParams.h"
#include "geo/GeoUtils.h"

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
constexpr double  kDegToRad          = std::numbers::pi / 180.0;
constexpr double  kMaxNavStepM       = 50.0;
constexpr int64_t kMaxTimestampGapUs = 5'000'000LL;
} // namespace

bool buildSwathPreviewImage(const std::vector<core::SidescanPing>& pings,
                            LayerMapData& ld,
                            int max_image_dim,
                            int palette_index,
                            const std::atomic_bool& cancelled,
                            const SssGeorefParams& georef_params,
                            double min_strip_cos,
                            int    cell_budget_div,
                            bool   ping_lines_only)
{
    ld.preview_image   = QImage{};
    ld.preview_reduced = false;

    // Reset stitch counters populated below; nav-track counters are left as-is.
    ld.nav_stats.stitch_nav_rejects     = 0;
    ld.nav_stats.stitch_time_rejects    = 0;
    ld.nav_stats.stitch_ping_rejects    = 0;
    ld.nav_stats.stitch_heading_rejects = 0;

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

    // -- Create transparent image + intensity cache ----------------------------
    // Rasterized cells write fully-opaque pixels; cells with no data stay
    // transparent (alpha 0).  MapView draws the image at 0.88 opacity, so
    // gaps between pings show the map background rather than a solid fill.
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

    // -- Quick auto-stretch: 1st / 99th percentile of strip amplitudes ---------
    float disp_low = 0.0f, disp_high = 1.0f;
    {
        std::vector<uint16_t> samp;
        samp.reserve(std::min<size_t>(65536, gr.strips.size() * 8));
        for (const auto& st : gr.strips)
            for (size_t j = 0; j < st.points.size();
                 j += std::max<size_t>(1, st.points.size() / 8))
                samp.push_back(st.points[j].amplitude);

        if (samp.size() > 10) {
            std::sort(samp.begin(), samp.end());
            disp_low  = static_cast<float>(samp[samp.size() / 100])      / 65535.0f;
            disp_high = static_cast<float>(samp[samp.size() * 99 / 100]) / 65535.0f;
            if (disp_high <= disp_low + 1e-4f) disp_high = disp_low + 0.01f;
        }
    }
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
        for (const auto& st : gr.strips) {
            for (const auto& pt : st.points) {
                const QPointF px = geoToImg(pt.lon, pt.lat);
                const int ix = static_cast<int>(px.x());
                const int iy = static_cast<int>(px.y());
                if (ix >= 0 && ix < img_w && iy >= 0 && iy < img_h) {
                    const float norm = SSSAmplitudeProcessor::displayIntensity(pt.amplitude, params);
                    pixels[iy * img_w + ix] = SSSPalette::color(norm, palette_index);
                    const int ai = static_cast<int>(pt.amplitude);
                    amp_buf[iy * img_w + ix] =
                        static_cast<uint16_t>(ai < 65535 ? ai + 1 : 65535);
                }
            }
        }
        ld.preview_image = std::move(img);
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
    // Threshold: median same-channel ping spacing × 10, capped at [10 m, 50 m].
    // At typical survey speed (1–3 m spacing), this gives a 10–30 m threshold
    // that passes all intra-line pairs and rejects inter-line gaps cleanly.
    const bool is_proj = gr.is_projected;

    // Compute median intra-line spacing for a sorted channel strip list.
    auto computeMedianSpacing = [&](const std::vector<const SssStrip*>& strips) -> double {
        if (strips.size() < 2) return 10.0;
        std::vector<double> spacings;
        spacings.reserve(strips.size() - 1);
        for (size_t i = 1; i < strips.size(); ++i) {
            core::NavPoint a{}, b{};
            a.lon = strips[i - 1]->nav_lon;  a.lat = strips[i - 1]->nav_lat;
            a.is_projected = is_proj;
            b.lon = strips[i]->nav_lon;       b.lat = strips[i]->nav_lat;
            b.is_projected = is_proj;
            const double d = geo::navDistanceMetres(a, b);
            if (d > 0.0 && d < kMaxNavStepM)   // exclude inter-line spikes
                spacings.push_back(d);
        }
        if (spacings.empty()) return 10.0;
        const size_t mid = spacings.size() / 2;
        std::nth_element(spacings.begin(), spacings.begin() + mid, spacings.end());
        return spacings[mid];
    };

    for (const auto ch : {core::SidescanChannel::Port, core::SidescanChannel::Starboard}) {
        std::vector<const SssStrip*> ch_strips;
        ch_strips.reserve(gr.strips.size() / 2 + 1);
        for (const auto& st : gr.strips)
            if (st.channel == ch) ch_strips.push_back(&st);

        // Adaptive threshold: median × 10, bounded to [10 m, 50 m].
        const double median_m         = computeMedianSpacing(ch_strips);
        const double nav_gap_thresh_m = std::min(50.0, std::max(10.0, median_m * 10.0));

        for (size_t si = 1; si < ch_strips.size(); ++si) {
            if (cancelled.load(std::memory_order_relaxed)) return false;

            const auto& prev = *ch_strips[si - 1];
            const auto& curr = *ch_strips[si];

            if (prev.points.empty() || curr.points.empty()) continue;
            if (prev.points.size() < 2 || curr.points.size() < 2) continue;

            // Nav-centre distance guard.
            {
                core::NavPoint nav_prev{}, nav_curr{};
                nav_prev.lon = prev.nav_lon;  nav_prev.lat = prev.nav_lat;
                nav_prev.is_projected = is_proj;
                nav_curr.lon = curr.nav_lon;  nav_curr.lat = curr.nav_lat;
                nav_curr.is_projected = is_proj;
                if (geo::navDistanceMetres(nav_prev, nav_curr) > nav_gap_thresh_m) {
                    ++ld.nav_stats.stitch_nav_rejects;
                    continue;
                }
            }

            // Timestamp gap guard: 5 s = line end / instrument restart.
            if (prev.timestamp_us > 0 && curr.timestamp_us > 0
                    && curr.timestamp_us - prev.timestamp_us > kMaxTimestampGapUs) {
                ++ld.nav_stats.stitch_time_rejects;
                continue;
            }

            // Ping-number continuity guard (XTF only; JSF has ping_number == 0).
            constexpr uint32_t kMaxPingNumGap = 20;
            if (prev.ping_number > 0 && curr.ping_number > 0
                    && curr.ping_number > prev.ping_number + kMaxPingNumGap) {
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

            const size_t np = std::min(prev.points.size(), curr.points.size());

            for (size_t j = 0; j + 1 < np; ++j) {
                ++ld.nav_stats.cells_attempted;
                rast.rasterizeCell(
                    pixels, img_w, img_h,
                    geoToImg(prev.points[j].lon,     prev.points[j].lat),
                    geoToImg(prev.points[j + 1].lon, prev.points[j + 1].lat),
                    geoToImg(curr.points[j].lon,     curr.points[j].lat),
                    geoToImg(curr.points[j + 1].lon, curr.points[j + 1].lat),
                    prev.points[j].amplitude,     prev.points[j + 1].amplitude,
                    curr.points[j].amplitude,     curr.points[j + 1].amplitude,
                    max_cell_pix,
                    amp_buf);
            }
        }
    }

    if (cancelled.load(std::memory_order_relaxed)) return false;

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

    // -- 3×3 hole-fill pass (normal preview only) ------------------------------
    // Closes pinhole gaps left where rasterized quads nearly meet but leave an
    // isolated transparent pixel.  Skipped in debug/ping-line modes so QC views
    // show the raw rasterization unmodified.
    //
    // Rule: fill a transparent pixel only when ≥5 of its 8 neighbours are opaque.
    // This leaves real survey gaps (open space on multiple sides) untouched.
    // Source pixels are read from a snapshot taken before any writes so no fill
    // pixel influences the neighbour test of another pixel in the same pass.
    size_t px_filled = 0;
    if (px_written > 0 && !ping_lines_only && !georef_params.debug_ping_lines_only) {
        const int total = img_w * img_h;
        std::vector<QRgb> snap(total);
        std::copy(reinterpret_cast<const QRgb*>(img.constBits()),
                  reinterpret_cast<const QRgb*>(img.constBits()) + total,
                  snap.data());

        for (int y = 1; y < img_h - 1; ++y) {
            for (int x = 1; x < img_w - 1; ++x) {
                const int idx = y * img_w + x;
                if (qAlpha(snap[idx]) > 0) continue;

                int      count = 0;
                uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
                uint32_t sum_amp = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int ni = (y + dy) * img_w + (x + dx);
                        const QRgb nb = snap[ni];
                        if (qAlpha(nb) > 0) {
                            ++count;
                            sum_r   += static_cast<uint32_t>(qRed(nb));
                            sum_g   += static_cast<uint32_t>(qGreen(nb));
                            sum_b   += static_cast<uint32_t>(qBlue(nb));
                            sum_amp += static_cast<uint32_t>(amp_buf[ni]);
                        }
                    }
                }
                if (count >= 5) {
                    pixels[idx] = qRgba(sum_r / count, sum_g / count, sum_b / count, 255);
                    amp_buf[idx] = static_cast<uint16_t>(sum_amp / count);
                    ++px_filled;
                }
            }
        }
    }

    ld.nav_stats.preview_pixels_written = px_written;
    ld.nav_stats.preview_pixels_filled  = px_filled;

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
