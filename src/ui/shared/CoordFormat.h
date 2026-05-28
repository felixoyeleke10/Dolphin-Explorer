#pragma once
// CoordFormat.h — shared coordinate display helpers used across all UI windows.
// Handles both geographic (decimal degrees) and projected (easting/northing metres).
#include "core/SpatialRef.h"
#include "geo/EpsgDatabase.h"
#include <QLocale>
#include <QString>
#include <cmath>

namespace dolphin::ui {

// Single-coordinate label: "6 353 009.4 m" for projected, "53.123456°N" for geographic.
inline QString formatCoord(double value, bool is_projected,
                           char pos_suffix, char neg_suffix)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    if (is_projected)
        return QString("%1 m").arg(value, 0, 'f', 1);
    const QChar suffix = value >= 0.0 ? pos_suffix : neg_suffix;
    return QString("%1\u00b0%2").arg(std::abs(value), 0, 'f', 6).arg(suffix);
}

// Two-coordinate status bar string.
// Geographic:  "53.123456°N   1.654321°W"
// Projected:   "N 6353009.4 m   E 432458.4 m"
inline QString formatPosition(double lat, double lon, bool is_projected)
{
    if (!std::isfinite(lat) || !std::isfinite(lon)) return {};
    if (is_projected) {
        return QString("N %1 m   E %2 m")
            .arg(lat, 0, 'f', 1)
            .arg(lon, 0, 'f', 1);
    }
    const QChar latS = lat >= 0.0 ? 'N' : 'S';
    const QChar lonS = lon >= 0.0 ? 'E' : 'W';
    return QString("%1\u00b0%2   %3\u00b0%4")
        .arg(std::abs(lat), 0, 'f', 6).arg(latS)
        .arg(std::abs(lon), 0, 'f', 6).arg(lonS);
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
