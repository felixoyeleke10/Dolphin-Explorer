// SSSNavModel.cpp — field definitions and SSSNavModel table-model implementation.
#include "ui/features/metadata/SSSMetadataWindow.h"

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <limits>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  Field definitions  (order must match rawValue() switch)
// ─────────────────────────────────────────────────────────────────────────────
const SSSNavModel::FieldDef SSSNavModel::kFieldDefs[SSSNavModel::kFieldCount] = {
    // ── Identification ────────────────────────────────────────────────────────
    { "Ping #",        "",       0 },
    { "Ping ID",       "",       0 },
    { "Channel",       "",      -1 },   // formatted as "Port" / "Stbd"
    // ── Timestamps ────────────────────────────────────────────────────────────
    { "Time",          "",      -1 },   // HH:MM:SS
    { "Nav Time",      "",      -1 },   // HH:MM:SS
    // ── Resolved position ─────────────────────────────────────────────────────
    { "Latitude",      "°",      6 },
    { "Longitude",     "°",      6 },
    // ── Raw source positions ──────────────────────────────────────────────────
    { "Fish Lat",      "°",      6 },
    { "Fish Lon",      "°",      6 },
    { "Vessel Lat",    "°",      6 },
    { "Vessel Lon",    "°",      6 },
    // ── Depth / dynamics ──────────────────────────────────────────────────────
    { "Depth",         "m",      2 },
    { "Altitude",      "m",      2 },
    { "Roll",          "°",      2 },
    { "Pitch",         "°",      2 },
    // ── Heading sources ───────────────────────────────────────────────────────
    { "Heading",       "°",      1 },
    { "Sensor Hdg",    "°",      1 },
    { "Ship Hdg",      "°",      1 },
    // ── Motion ────────────────────────────────────────────────────────────────
    { "Speed",         "kn",     2 },
    { "Heave",         "m",      2 },
    // ── Sonar geometry ────────────────────────────────────────────────────────
    { "Slant Range",   "m",      1 },
    { "Sample Rate",   "Hz",     0 },
    { "Blanking",      "m",      1 },
    { "Layback",       "m",      1 },
    { "Cable Out",     "m",      1 },
    { "Fish dX",       "m",      2 },
    { "Fish dY",       "m",      2 },
    { "KP",            "m",      0 },
    // ── Sonar acoustics ───────────────────────────────────────────────────────
    { "Frequency",     "Hz",     0 },
    { "Sound Vel.",    "m/s",    1 },
    { "Bandwidth",     "Hz",     0 },
    // ── Gain / calibration ────────────────────────────────────────────────────
    { "Gain",          "",       0 },
    { "Init. Gain",    "",       0 },
    { "Volt Scale",    "mV/cnt", 3 },
    // ── Bottom / QC ───────────────────────────────────────────────────────────
    { "Samples",       "",       0 },
    { "Btm Range",     "m",      2 },
    { "Btm Conf",      "",       2 },
    { "QC Flags",      "",       0 },
    { "Corrections",   "",       0 },
};

// ─────────────────────────────────────────────────────────────────────────────
//  SSSNavModel
// ─────────────────────────────────────────────────────────────────────────────

SSSNavModel::SSSNavModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    m_precision.fill(-1, kFieldCount);
    m_visible.reserve(kFieldCount);
    for (int i = 0; i < kFieldCount; ++i) m_visible.append(i);
}

void SSSNavModel::setPings(std::vector<core::SidescanPing> pings)
{
    std::vector<core::SidescanPing> nav;
    nav.reserve(pings.size());
    for (auto& p : pings)
        if (p.channel == core::SidescanChannel::Port) nav.push_back(std::move(p));
    if (nav.empty()) nav = std::move(pings);

    std::sort(nav.begin(), nav.end(),
        [](const core::SidescanPing& a, const core::SidescanPing& b){
            return a.timestamp_us < b.timestamp_us; });

    beginResetModel();
    m_pings = std::move(nav);
    endResetModel();
}

void SSSNavModel::setVisibleFields(const QVector<int>& field_indices)
{
    beginResetModel();
    m_visible = field_indices;
    endResetModel();
}

void SSSNavModel::setFieldPrecision(int fi, int precision)
{
    if (fi >= 0 && fi < kFieldCount) m_precision[fi] = precision;
}

void SSSNavModel::setCoordinatesProjected(bool projected)
{
    if (m_coords_projected == projected) return;
    m_coords_projected = projected;
    emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
}

QVector<double> SSSNavModel::fieldValues(int fi) const
{
    QVector<double> out;
    out.reserve(static_cast<int>(m_pings.size()));
    for (int i = 0; i < static_cast<int>(m_pings.size()); ++i)
        out.append(rawValue(i, fi));
    return out;
}

int SSSNavModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_pings.size());
}

int SSSNavModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant SSSNavModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return {};
    const int col = index.column();
    if (col < 0 || col >= m_visible.size()) return {};
    if (role == Qt::DisplayRole)
        return formatValue(rawValue(index.row(), m_visible[col]), m_visible[col]);
    if (role == Qt::TextAlignmentRole)
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::UserRole + 1) {
        const double v = rawValue(index.row(), m_visible[col]);
        return std::isfinite(v) ? v : 0.0;
    }
    return {};
}

QVariant SSSNavModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Vertical) {
        if (role == Qt::DisplayRole) return section + 1;
        return {};
    }
    if (section < 0 || section >= m_visible.size()) return {};
    const int fi = m_visible[section];
    if (role == Qt::UserRole)      return fi;
    if (role != Qt::DisplayRole)   return {};
    if (m_coords_projected) {
        if (fi ==  5) return QString("Northing\n(m)");
        if (fi ==  6) return QString("Easting\n(m)");
        if (fi ==  7) return QString("Fish\nNorthing");
        if (fi ==  8) return QString("Fish\nEasting");
        if (fi ==  9) return QString("Vessel\nNorthing");
        if (fi == 10) return QString("Vessel\nEasting");
    }
    QString s = QString::fromUtf8(kFieldDefs[fi].name);
    if (kFieldDefs[fi].unit[0] != '\0')
        s += QString("\n(%1)").arg(QString::fromUtf8(kFieldDefs[fi].unit));
    return s;
}

double SSSNavModel::rawValue(int ping_idx, int fi) const
{
    static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    const auto& p = m_pings[static_cast<size_t>(ping_idx)];
    switch (fi) {
    //  Identification
    case  0: return p.ping_number;
    case  1: return static_cast<double>(p.id);
    case  2: return static_cast<double>(p.channel);   // 0=Port, 1=Stbd
    //  Timestamps
    case  3: return static_cast<double>(p.timestamp_us) / 1e6;
    case  4: return (p.nav.timestamp > 0.0) ? p.nav.timestamp : kNaN;
    //  Resolved position
    case  5: return p.nav.lat;
    case  6: return p.nav.lon;
    //  Raw source positions
    case  7: return p.nav.fish_nav_valid   ? p.nav.fish_lat   : kNaN;
    case  8: return p.nav.fish_nav_valid   ? p.nav.fish_lon   : kNaN;
    case  9: return p.nav.vessel_nav_valid ? p.nav.vessel_lat : kNaN;
    case 10: return p.nav.vessel_nav_valid ? p.nav.vessel_lon : kNaN;
    //  Depth / dynamics
    case 11: return p.tow_depth_m;
    case 12: return p.nav.altitude_m;
    case 13: return p.nav.roll_deg;
    case 14: return p.nav.pitch_deg;
    //  Heading sources
    case 15: return p.nav.heading_deg;
    case 16: return (p.nav.sensor_heading_deg != 0.f)
                    ? static_cast<double>(p.nav.sensor_heading_deg) : kNaN;
    case 17: return (p.nav.ship_heading_deg != 0.f)
                    ? static_cast<double>(p.nav.ship_heading_deg) : kNaN;
    //  Motion
    case 18: return p.nav.speed_kn;
    case 19: return p.nav.heave_m;
    //  Sonar geometry
    case 20: return p.slant_range_m;
    case 21: return p.sample_rate_hz;
    case 22: return p.blanking_m;
    case 23: return p.layback_m;
    case 24: return p.cable_out_m;
    case 25: return p.fish_delta_x_m;
    case 26: return p.fish_delta_y_m;
    case 27: return p.kp_m;
    //  Sonar acoustics
    case 28: return p.frequency_hz;
    case 29: return p.sound_velocity_ms;
    case 30: return p.bandwidth_hz;
    //  Gain / calibration
    case 31: return p.gain_code;
    case 32: return p.initial_gain_code;
    case 33: return p.volt_scale;
    //  Bottom / QC
    case 34: return static_cast<double>(p.samples.size());
    case 35: return p.bottom_pick.valid()
                    ? static_cast<double>(p.bottom_pick.range_m) : kNaN;
    case 36: return p.bottom_pick.valid()
                    ? static_cast<double>(p.bottom_pick.confidence) : kNaN;
    case 37: return static_cast<double>(p.qc_flags);
    case 38: return static_cast<double>(p.correction_flags);
    default: return kNaN;
    }
}

QString SSSNavModel::formatValue(double v, int fi) const
{
    if (!std::isfinite(v)) return "-";

    // Channel → human-readable label
    if (fi == 2)
        return (static_cast<int>(v) == 0) ? QStringLiteral("Port") : QStringLiteral("Stbd");

    // Trace timestamp  →  HH:MM:SS
    if (fi == 3)
        return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(v), Qt::UTC)
                   .toString("HH:mm:ss");

    // Nav timestamp  →  HH:MM:SS
    if (fi == 4)
        return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(v), Qt::UTC)
                   .toString("HH:mm:ss");

    // Fields that show "-" when the value is zero (physically meaningless zero)
    static constexpr int kNeverZero[] = { 20, 21, 28, 29, 33 };
    if (v == 0.0) {
        for (int nz : kNeverZero) if (fi == nz) return "-";
    }

    // Projected coordinate: lat/lon fields need reduced precision header but
    // the value is already in projected metres — show with 1 dp by default.
    if (m_coords_projected && (fi == 5 || fi == 6)) {
        const int prec = (m_precision[fi] >= 0) ? m_precision[fi] : 1;
        return QString::number(v, 'f', prec);
    }
    if (m_coords_projected && (fi >= 7 && fi <= 10)) {
        const int prec = (m_precision[fi] >= 0) ? m_precision[fi] : 1;
        return QString::number(v, 'f', prec);
    }

    const int prec = (fi >= 0 && fi < kFieldCount && m_precision[fi] >= 0)
                     ? m_precision[fi]
                     : kFieldDefs[fi].default_prec;
    if (prec < 0) return QString::number(v);
    return QString::number(v, 'f', prec);
}

} // namespace dolphin::ui
