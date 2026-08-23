#pragma once
// SwathRasterizer.h — geometry-correct sidescan swath rasterizer.
//
// Corner layout (matches MapViewPaint variable names):
//
//   pa --- na     ← previous ping strip (top edge)
//   |          |
//   pb --- nb     ← current  ping strip (bottom edge)
//
//   a00 = amplitude at pa    a10 = amplitude at na
//   a01 = amplitude at pb    a11 = amplitude at nb
//
// A swath cell is a general quadrilateral: turns, layback, and unequal ping
// spacing mean nb is not generally pa + (na-pa) + (pb-pa).  The rasterizer
// therefore triangulates the actual four corners and interpolates amplitude
// barycentrically. Exact triangle/pixel-square intersection preserves valid
// sub-pixel cells without dilating the sonar footprint.
//
// A 65 536-entry uint16 → QRgb LUT is built once per mosaic build call so
// palette/gain evaluation stays outside the per-pixel geometry loop.

#include "render/sonar/SSSAmplitudeProcessor.h"
#include "render/sonar/SSSPalette.h"
#include "render/sonar/SonarDisplayParams.h"

#include <QPointF>
#include <QRgb>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>

namespace dolphin::ui {

class SwathRasterizer {
public:
    // Build the uint16 → QRgb LUT from the current display parameters.
    // Call once per buildLayerMosaic() invocation before any rasterizeCell calls.
    void buildLut(const SonarDisplayParams& params, int palette_index);

    // Rasterize one general quad cell into a QRgb pixel buffer.
    //
    // pixels        – flat row-major QImage::Format_RGB32 buffer
    // img_w, img_h  – buffer dimensions in physical pixels
    // pa, na        – physical-pixel corners on the previous ping strip (top edge)
    // pb, nb        – physical-pixel corners on the current  ping strip (bottom edge)
    // a00..a11      – uint16 amplitudes at each corner (see layout above)
    // amp_out       – optional parallel uint16 buffer (same size as pixels).
    //                 When non-null, receives the interpolated amplitude at each
    //                 written pixel so callers can rebuild the QImage with a
    //                 different palette without re-rasterizing geometry.
    //                 0 = no data (transparent); values stored as amp+1 so true
    //                 zero amplitude is distinguishable from "not written".
    // Returns the number of pixel writes. Zero means the cell was outside the
    // image, degenerate, or rejected by the explicit per-cell budget.
    size_t rasterizeCell(QRgb*     pixels,
                         int       img_w,
                         int       img_h,
                         QPointF   pa,
                         QPointF   na,
                         QPointF   pb,
                         QPointF   nb,
                         uint16_t  a00,
                         uint16_t  a10,
                         uint16_t  a01,
                         uint16_t  a11,
                         int       max_cell_pix = std::numeric_limits<int>::max(),
                         uint16_t* amp_out      = nullptr) const noexcept;

private:
    std::array<QRgb, 65536> m_lut{};
};

// -----------------------------------------------------------------------------

inline void SwathRasterizer::buildLut(const SonarDisplayParams& params,
                                      int palette_index)
{
    for (int i = 0; i < 65536; ++i) {
        const float intensity = SSSAmplitudeProcessor::displayIntensity(
            static_cast<uint16_t>(i), params);
        m_lut[static_cast<size_t>(i)] =
            SSSPalette::color(intensity, palette_index);
    }
}

inline size_t SwathRasterizer::rasterizeCell(QRgb*     pixels,
                                              int       img_w,
                                              int       img_h,
                                              QPointF   pa,
                                              QPointF   na,
                                              QPointF   pb,
                                              QPointF   nb,
                                              uint16_t  a00,
                                              uint16_t  a10,
                                              uint16_t  a01,
                                              uint16_t  a11,
                                              int       max_cell_pix,
                                              uint16_t* amp_out) const noexcept
{
    if ((!pixels && !amp_out) || img_w <= 0 || img_h <= 0)
        return 0;

    struct Vertex {
        QPointF p;
        double  amplitude = 0.0;
    };
    struct Triangle {
        Vertex a, b, c;
        double area = 0.0;
    };

    const auto edge = [](const QPointF& a, const QPointF& b,
                         const QPointF& p) noexcept {
        return (b.x() - a.x()) * (p.y() - a.y())
             - (b.y() - a.y()) * (p.x() - a.x());
    };

    const Vertex vpa{pa, static_cast<double>(a00)};
    const Vertex vna{na, static_cast<double>(a10)};
    const Vertex vpb{pb, static_cast<double>(a01)};
    const Vertex vnb{nb, static_cast<double>(a11)};

    const double d1a = edge(pa, na, nb);
    const double d1b = edge(pa, nb, pb);
    const double d2a = edge(pa, na, pb);
    const double d2b = edge(na, nb, pb);

    // For a concave quad only one diagonal lies inside the polygon. Prefer the
    // pa→nb diagonal used by convex cells; switch when its triangles disagree
    // in winding and the alternative is coherent.
    const bool diagonal_one_coherent = d1a * d1b >= 0.0;
    const bool diagonal_two_coherent = d2a * d2b >= 0.0;
    if (!diagonal_one_coherent && !diagonal_two_coherent)
        return 0; // self-intersecting (bow-tie) cell
    std::array<Triangle, 2> triangles;
    if (diagonal_one_coherent) {
        triangles = {{{vpa, vna, vnb, d1a}, {vpa, vnb, vpb, d1b}}};
    } else {
        triangles = {{{vpa, vna, vpb, d2a}, {vna, vnb, vpb, d2b}}};
    }

    double max_edge_sq = 0.0;
    const QPointF corners[] = {pa, na, nb, pb};
    for (size_t i = 0; i < 4; ++i) {
        const QPointF delta = corners[(i + 1) % 4] - corners[i];
        max_edge_sq = std::max(max_edge_sq,
            delta.x() * delta.x() + delta.y() * delta.y());
    }
    const double area_epsilon = std::max(1e-12, max_edge_sq * 1e-12);
    const bool first_valid  = std::abs(triangles[0].area) > area_epsilon;
    const bool second_valid = std::abs(triangles[1].area) > area_epsilon;
    if (!first_valid && !second_valid)
        return 0;

    // Pixel-aligned bounding box, clamped to image bounds.
    const float xs[4] = {
        static_cast<float>(pa.x()), static_cast<float>(na.x()),
        static_cast<float>(pb.x()), static_cast<float>(nb.x())
    };
    const float ys[4] = {
        static_cast<float>(pa.y()), static_cast<float>(na.y()),
        static_cast<float>(pb.y()), static_cast<float>(nb.y())
    };

    const int xmin = std::max(0,         static_cast<int>(std::floor(*std::min_element(xs, xs + 4))));
    const int xmax = std::min(img_w - 1, static_cast<int>(std::ceil (*std::max_element(xs, xs + 4))));
    const int ymin = std::max(0,         static_cast<int>(std::floor(*std::min_element(ys, ys + 4))));
    const int ymax = std::min(img_h - 1, static_cast<int>(std::ceil (*std::max_element(ys, ys + 4))));

    if (xmin > xmax || ymin > ymax) return 0;

    // Per-cell pixel-budget guard: caller passes max_cell_pix (INT_MAX = disabled).
    if ((xmax - xmin) > max_cell_pix || (ymax - ymin) > max_cell_pix) return 0;

    const auto writeAmplitude = [&](int px, int py, double amplitude) noexcept {
        const int amp_i = static_cast<int>(std::floor(amplitude + 0.5));
        const int clamped = std::clamp(amp_i, 0, 65535);
        const size_t idx = static_cast<size_t>(py) * static_cast<size_t>(img_w)
                         + static_cast<size_t>(px);
        if (pixels) pixels[idx] = m_lut[static_cast<size_t>(clamped)];
        if (amp_out)
            amp_out[idx] = static_cast<uint16_t>(clamped < 65535
                ? clamped + 1 : 65535);
    };

    const auto sampleTriangle = [&](const Triangle& t, const QPointF& p,
                                    double& amplitude) noexcept {
        if (std::abs(t.area) <= area_epsilon)
            return false;
        const double w0 = edge(t.b.p, t.c.p, p) / t.area;
        const double w1 = edge(t.c.p, t.a.p, p) / t.area;
        const double w2 = edge(t.a.p, t.b.p, p) / t.area;
        constexpr double kBarycentricEpsilon = 1e-9;
        if (w0 < -kBarycentricEpsilon || w1 < -kBarycentricEpsilon
                || w2 < -kBarycentricEpsilon)
            return false;
        amplitude = w0 * t.a.amplitude + w1 * t.b.amplitude
                  + w2 * t.c.amplitude;
        return true;
    };

    // Closest point on a triangle, returned as both squared distance and the
    // matching barycentric amplitude. Used for exact pixel-square coverage:
    // when a valid cell is thinner than a pixel, every pixel square touched by
    // the cell still receives data instead of only one centroid splat.
    const auto closestTriangleSample = [&](const Triangle& t, const QPointF& p,
                                           double& distance_sq,
                                           double& amplitude) noexcept {
        if (std::abs(t.area) <= area_epsilon)
            return false;

        const QPointF ab = t.b.p - t.a.p;
        const QPointF ac = t.c.p - t.a.p;
        const QPointF ap = p - t.a.p;
        const auto dot = [](const QPointF& x, const QPointF& y) noexcept {
            return x.x() * y.x() + x.y() * y.y();
        };
        double u = 0.0, v = 0.0, w = 0.0;
        const double d1 = dot(ab, ap);
        const double d2 = dot(ac, ap);
        if (d1 <= 0.0 && d2 <= 0.0) {
            u = 1.0;
        } else {
            const QPointF bp = p - t.b.p;
            const double d3 = dot(ab, bp);
            const double d4 = dot(ac, bp);
            if (d3 >= 0.0 && d4 <= d3) {
                v = 1.0;
            } else {
                const double vc = d1 * d4 - d3 * d2;
                if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
                    v = d1 / (d1 - d3);
                    u = 1.0 - v;
                } else {
                    const QPointF cp = p - t.c.p;
                    const double d5 = dot(ab, cp);
                    const double d6 = dot(ac, cp);
                    if (d6 >= 0.0 && d5 <= d6) {
                        w = 1.0;
                    } else {
                        const double vb = d5 * d2 - d1 * d6;
                        if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
                            w = d2 / (d2 - d6);
                            u = 1.0 - w;
                        } else {
                            const double va = d3 * d6 - d5 * d4;
                            if (va <= 0.0 && (d4 - d3) >= 0.0
                                    && (d5 - d6) >= 0.0) {
                                w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                                v = 1.0 - w;
                            } else {
                                const double denom = 1.0 / (va + vb + vc);
                                v = vb * denom;
                                w = vc * denom;
                                u = 1.0 - v - w;
                            }
                        }
                    }
                }
            }
        }

        const QPointF closest = t.a.p * u + t.b.p * v + t.c.p * w;
        const QPointF delta = p - closest;
        distance_sq = dot(delta, delta);
        amplitude = u * t.a.amplitude + v * t.b.amplitude + w * t.c.amplitude;
        return true;
    };

    const auto triangleIntersectsPixel = [&](const Triangle& t,
                                             int px, int py) noexcept {
        if (std::abs(t.area) <= area_epsilon)
            return false;
        const double left   = static_cast<double>(px);
        const double right  = left + 1.0;
        const double top    = static_cast<double>(py);
        const double bottom = top + 1.0;
        const std::array<QPointF, 4> square = {
            QPointF(left, top), QPointF(right, top),
            QPointF(right, bottom), QPointF(left, bottom)
        };
        const std::array<QPointF, 3> triangle = {t.a.p, t.b.p, t.c.p};

        for (const QPointF& point : triangle)
            if (point.x() >= left && point.x() <= right
                    && point.y() >= top && point.y() <= bottom)
                return true;

        for (const QPointF& point : square) {
            double ignored = 0.0;
            if (sampleTriangle(t, point, ignored))
                return true;
        }

        const auto onSegment = [&](const QPointF& a, const QPointF& b,
                                   const QPointF& p) noexcept {
            return std::abs(edge(a, b, p)) <= area_epsilon
                && p.x() >= std::min(a.x(), b.x()) - area_epsilon
                && p.x() <= std::max(a.x(), b.x()) + area_epsilon
                && p.y() >= std::min(a.y(), b.y()) - area_epsilon
                && p.y() <= std::max(a.y(), b.y()) + area_epsilon;
        };
        const auto segmentsIntersect = [&](const QPointF& a, const QPointF& b,
                                           const QPointF& c, const QPointF& d) noexcept {
            const double ab_c = edge(a, b, c);
            const double ab_d = edge(a, b, d);
            const double cd_a = edge(c, d, a);
            const double cd_b = edge(c, d, b);
            if (((ab_c > area_epsilon && ab_d < -area_epsilon)
                    || (ab_c < -area_epsilon && ab_d > area_epsilon))
                && ((cd_a > area_epsilon && cd_b < -area_epsilon)
                    || (cd_a < -area_epsilon && cd_b > area_epsilon)))
                return true;
            return onSegment(a, b, c) || onSegment(a, b, d)
                || onSegment(c, d, a) || onSegment(c, d, b);
        };

        for (size_t ti = 0; ti < triangle.size(); ++ti)
            for (size_t si = 0; si < square.size(); ++si)
                if (segmentsIntersect(
                        triangle[ti], triangle[(ti + 1) % triangle.size()],
                        square[si], square[(si + 1) % square.size()]))
                    return true;
        return false;
    };

    size_t writes = 0;

    for (int py = ymin; py <= ymax; ++py) {
        for (int px = xmin; px <= xmax; ++px) {
            const QPointF centre(static_cast<double>(px) + 0.5,
                                 static_cast<double>(py) + 0.5);
            double amplitude = 0.0;
            if (sampleTriangle(triangles[0], centre, amplitude)
                    || sampleTriangle(triangles[1], centre, amplitude)) {
                writeAmplitude(px, py, amplitude);
                ++writes;
                continue;
            }

            double best_distance_sq = std::numeric_limits<double>::infinity();
            double best_amplitude = 0.0;
            bool intersects = false;
            for (const Triangle& triangle : triangles) {
                double distance_sq = 0.0;
                double edge_amplitude = 0.0;
                if (!closestTriangleSample(
                        triangle, centre, distance_sq, edge_amplitude))
                    continue;

                // A unit pixel square is bounded by the circle of radius
                // sqrt(0.5) around its centre and contains the circle of radius
                // 0.5. Distance therefore gives two cheap exact decisions:
                // outside the outer circle cannot intersect; inside the inner
                // circle certainly intersects. Only the narrow corner annulus
                // needs the expensive triangle/square edge test.
                constexpr double kInnerRadiusSq = 0.25;
                constexpr double kOuterRadiusSq = 0.5;
                if (distance_sq > kOuterRadiusSq + area_epsilon)
                    continue;
                if (distance_sq > kInnerRadiusSq + area_epsilon
                        && !triangleIntersectsPixel(triangle, px, py))
                    continue;

                if (distance_sq < best_distance_sq) {
                    intersects = true;
                    best_distance_sq = distance_sq;
                    best_amplitude = edge_amplitude;
                }
            }
            if (intersects) {
                writeAmplitude(px, py, best_amplitude);
                ++writes;
            }
        }
    }

    // Numerical last resort for a valid cell whose conservative footprint fell
    // exactly between clamped image pixels. Degenerate cells never reach here.
    if (writes == 0) {
        const QPointF centroid = (pa + na + pb + nb) * 0.25;
        const int px = static_cast<int>(std::floor(centroid.x()));
        const int py = static_cast<int>(std::floor(centroid.y()));
        if (px >= 0 && px < img_w && py >= 0 && py < img_h) {
            writeAmplitude(px, py,
                (static_cast<double>(a00) + static_cast<double>(a10)
                 + static_cast<double>(a01) + static_cast<double>(a11)) * 0.25);
            writes = 1;
        }
    }

    return writes;
}

} // namespace dolphin::ui
