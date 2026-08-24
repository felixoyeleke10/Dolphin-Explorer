#pragma once
// CoordFormat.h — shared coordinate display helpers used across all UI windows.
// Handles both geographic (decimal degrees) and projected (easting/northing metres).
#include "core/SpatialRef.h"
#include "geo/EpsgDatabase.h"
#include "geo/GeoUtils.h"
#include <QLocale>
#include <QString>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace dolphin::ui {

enum class CoordinateDisplayFormat {
    DecimalDegrees = 0,
    DegreesMinutesSeconds = 1,
    Utm = 2,
};

// AppState updates this process-wide presentation preference. Keeping it beside
// the formatter makes every consumer (main status, viewers, inspectors, reports)
// obey the same live setting without querying QSettings on every mouse move.
inline std::atomic_int g_coordinate_display_format{0};

inline void setCoordinateDisplayFormat(int format) noexcept
{
    g_coordinate_display_format.store(
        std::clamp(format, 0, 2), std::memory_order_relaxed);
}

inline CoordinateDisplayFormat coordinateDisplayFormat() noexcept
{
    return static_cast<CoordinateDisplayFormat>(
        g_coordinate_display_format.load(std::memory_order_relaxed));
}

inline QString formatDms(double value, char positive, char negative)
{
    const double absolute = std::abs(value);
    const int degrees = static_cast<int>(std::floor(absolute));
    const double minutes_total = (absolute - degrees) * 60.0;
    const int minutes = static_cast<int>(std::floor(minutes_total));
    const double seconds = (minutes_total - minutes) * 60.0;
    return QStringLiteral("%1° %2′ %3″%4")
        .arg(degrees)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 0, 'f', 2)
        .arg(QChar(value >= 0.0 ? positive : negative));
}

// Single-coordinate label: "6 353 009.4 m" for projected, "53.123456°N" for geographic.
inline QString formatCoord(double value, bool is_projected,
                           char pos_suffix, char neg_suffix)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    if (is_projected)
        return QString("%1 m").arg(value, 0, 'f', 1);
    if (coordinateDisplayFormat()
            == CoordinateDisplayFormat::DegreesMinutesSeconds)
        return formatDms(value, pos_suffix, neg_suffix);
    const QChar suffix = value >= 0.0 ? pos_suffix : neg_suffix;
    return QString("%1\u00b0%2").arg(std::abs(value), 0, 'f', 6).arg(suffix);
}

struct FormattedCoordinatePair {
    QString first;   // latitude or northing (including its label/suffix)
    QString second;  // longitude or easting (including its label/suffix)
};

inline FormattedCoordinatePair formatPositionComponents(
    double lat, double lon, bool is_projected)
{
    if (!std::isfinite(lat) || !std::isfinite(lon)) return {};
    if (is_projected) {
        return {
            QStringLiteral("N %1 m").arg(lat, 0, 'f', 1),
            QStringLiteral("E %1 m").arg(lon, 0, 'f', 1)};
    }
    if (coordinateDisplayFormat()
            == CoordinateDisplayFormat::DegreesMinutesSeconds)
        return {formatDms(lat, 'N', 'S'), formatDms(lon, 'E', 'W')};
    if (coordinateDisplayFormat() == CoordinateDisplayFormat::Utm) {
        int zone = 0;
        bool north = true;
        double easting = 0.0;
        double northing = 0.0;
        if (geo::latLonToUtm(lat, lon, zone, north, easting, northing)) {
            return {
                QStringLiteral("UTM %1%2  N %3 m")
                    .arg(zone).arg(north ? QChar('N') : QChar('S'))
                    .arg(northing, 0, 'f', 1),
                QStringLiteral("E %1 m").arg(easting, 0, 'f', 1)};
        }
    }
    return {
        formatCoord(lat, false, 'N', 'S'),
        formatCoord(lon, false, 'E', 'W')};
}

// Two-coordinate status bar string.
// Geographic:  "53.123456°N   1.654321°W"
// Projected:   "N 6353009.4 m   E 432458.4 m"
inline QString formatPosition(double lat, double lon, bool is_projected)
{
    const auto components = formatPositionComponents(lat, lon, is_projected);
    if (components.first.isEmpty() || components.second.isEmpty()) return {};
    return components.first + QStringLiteral("   ") + components.second;
}

// Human-readable CRS label for UI display.
// Delegates to EpsgDatabase so all windows show the same authoritative names.
inline QString spatialRefDisplayName(const core::SpatialRef& ref)
{
    return QString::fromStdString(geo::epsgDisplayName(ref));
}

// Graticule UTM label formatters — used by both the 2D and 3D map grid renderers.
inline QString fmtUtmE(double e)
{
    static const QLocale loc;
    return QStringLiteral("E ") + loc.toString(qRound(e));
}
inline QString fmtUtmN(double n)
{
    static const QLocale loc;
    return QStringLiteral("N ") + loc.toString(qRound(n));
}

} // namespace dolphin::ui
