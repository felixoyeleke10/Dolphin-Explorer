// SBPNavModel.cpp — field definitions and SBPNavModel table-model implementation.
#include "ui/features/metadata/SBPMetadataWindow.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  Field definitions
// ─────────────────────────────────────────────────────────────────────────────
const SBPNavModel::FieldDef SBPNavModel::kFieldDefs[SBPNavModel::kFieldCount] = {
    // ── Identification ────────────────────────────────────────────────────────
    {"#",            "",    0},
    {"Trace ID",     "",    0},
    // ── Timestamps ────────────────────────────────────────────────────────────
    {"Time",         "",   -1},   // trace timestamp HH:MM:SS.mmm
    {"Nav Time",     "",   -1},   // nav fix timestamp HH:MM:SS
    // ── Resolved position ─────────────────────────────────────────────────────
    {"Latitude",     "°",   6},
    {"Longitude",    "°",   6},
    // ── Raw source positions ──────────────────────────────────────────────────
    {"Fish Lat",     "°",   6},
    {"Fish Lon",     "°",   6},
    {"Vessel Lat",   "°",   6},
    {"Vessel Lon",   "°",   6},
    // ── Heading sources ───────────────────────────────────────────────────────
    {"Heading",      "°",   1},   // resolved
    {"Sensor Hdg",   "°",   1},   // fish / towfish AHRS
    {"Ship Hdg",     "°",   1},   // vessel gyro
    // ── Motion / dynamics ─────────────────────────────────────────────────────
    {"Speed",        "kn",  2},
    {"Altitude",     "m",   2},
    {"Roll",         "°",   2},
    {"Pitch",        "°",   2},
    {"Heave",        "m",   2},
    // ── Acoustic acquisition ──────────────────────────────────────────────────
    {"Frequency",    "Hz",  0},
    {"Sample Rate",  "Hz",  0},
    {"Tow Depth",    "m",   2},
    {"Record Time",  "ms",  2},
    {"Samples",      "",    0},   // sample count
    // ── Bottom track ──────────────────────────────────────────────────────────
    {"Btm Sample",   "",    0},
    {"Btm Depth",    "m",   2},
    {"Total Depth",  "m",   2},   // tow depth + btm depth (water column)
};

// ─────────────────────────────────────────────────────────────────────────────
//  SBPNavModel
// ─────────────────────────────────────────────────────────────────────────────
SBPNavModel::SBPNavModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    m_precision.fill(-1, kFieldCount);
    for (int i = 0; i < kFieldCount; ++i) m_visible.append(i);
}

void SBPNavModel::setTraces(std::vector<core::SubBottomTrace> traces)
{
    beginResetModel();
    m_traces = std::move(traces);
    endResetModel();
}

void SBPNavModel::setVisibleFields(const QVector<int>& field_indices)
{
    beginResetModel();
    m_visible = field_indices;
    endResetModel();
}

void SBPNavModel::setFieldPrecision(int fi, int precision)
{
    if (fi >= 0 && fi < kFieldCount) m_precision[fi] = precision;
}

void SBPNavModel::setCoordinatesProjected(bool projected)
{
    if (m_coords_projected == projected) return;
    m_coords_projected = projected;
    emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
}

QVector<double> SBPNavModel::fieldValues(int fi) const
{
    QVector<double> out;
    out.reserve(static_cast<int>(m_traces.size()));
    for (int i = 0; i < static_cast<int>(m_traces.size()); ++i)
        out.append(rawValue(i, fi));
    return out;
}

int SBPNavModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_traces.size());
}

int SBPNavModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

double SBPNavModel::rawValue(int ri, int fi) const
{
    static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    if (ri < 0 || ri >= static_cast<int>(m_traces.size())) return 0.0;
    const auto& t = m_traces[static_cast<size_t>(ri)];
    switch (fi) {
        //  Identification
        case  0: return static_cast<double>(ri + 1);
        case  1: return static_cast<double>(t.id);
        //  Timestamps
        case  2: return static_cast<double>(t.timestamp_us) * 1e-6;
        case  3: return (t.nav.timestamp > 0.0) ? t.nav.timestamp : kNaN;
        //  Resolved position
        case  4: return t.nav.lat;
        case  5: return t.nav.lon;
        //  Raw source positions (NaN when validity flag is false)
        case  6: return t.nav.fish_nav_valid   ? t.nav.fish_lat   : kNaN;
        case  7: return t.nav.fish_nav_valid   ? t.nav.fish_lon   : kNaN;
        case  8: return t.nav.vessel_nav_valid ? t.nav.vessel_lat : kNaN;
        case  9: return t.nav.vessel_nav_valid ? t.nav.vessel_lon : kNaN;
        //  Heading sources (0.0 = not present by XTF/JSF convention)
        case 10: return static_cast<double>(t.nav.heading_deg);
        case 11: return (t.nav.sensor_heading_deg != 0.f)
                        ? static_cast<double>(t.nav.sensor_heading_deg) : kNaN;
        case 12: return (t.nav.ship_heading_deg != 0.f)
                        ? static_cast<double>(t.nav.ship_heading_deg) : kNaN;
        //  Motion / dynamics
        case 13: return static_cast<double>(t.nav.speed_kn);
        case 14: return static_cast<double>(t.nav.altitude_m);
        case 15: return static_cast<double>(t.nav.roll_deg);
        case 16: return static_cast<double>(t.nav.pitch_deg);
        case 17: return static_cast<double>(t.nav.heave_m);
        //  Acoustic acquisition
        case 18: return static_cast<double>(t.frequency_hz);
        case 19: return static_cast<double>(t.sample_rate_hz);
        case 20: return static_cast<double>(t.tow_depth_m);
        case 21: return static_cast<double>(t.two_way_time_s) * 1000.0;
        case 22: return static_cast<double>(t.samples.size());
        //  Bottom track
        case 23: return static_cast<double>(t.bottom_sample_idx);
        case 24: {
            if (t.bottom_sample_idx < 0 || t.sample_rate_hz <= 0.f) return kNaN;
            return (static_cast<double>(t.bottom_sample_idx) / t.sample_rate_hz) * 750.0;
        }
        case 25: {
            if (t.bottom_sample_idx < 0 || t.sample_rate_hz <= 0.f) return kNaN;
            const double btm = (static_cast<double>(t.bottom_sample_idx) / t.sample_rate_hz) * 750.0;
            return static_cast<double>(t.tow_depth_m) + btm;
        }
        default: return 0.0;
    }
}

QString SBPNavModel::formatValue(double v, int fi) const
{
    // NaN sentinel = field not available for this trace
    if (!std::isfinite(v)) return QString{};

    // Trace index
    if (fi == 0) return QString::number(static_cast<int>(v));

    // Trace ID
    if (fi == 1) return QString::number(static_cast<uint64_t>(v));

    // Trace timestamp  →  HH:MM:SS.mmm
    if (fi == 2) {
        const int64_t epoch_ms = static_cast<int64_t>(v * 1000.0);
        const int ms  = static_cast<int>(epoch_ms % 1000);
        const int64_t secs = epoch_ms / 1000;
        const int sec = static_cast<int>(secs % 60);
        const int min = static_cast<int>((secs / 60) % 60);
        const int hr  = static_cast<int>((secs / 3600) % 24);
        return QString("%1:%2:%3.%4")
            .arg(hr,  2, 10, QChar('0'))
            .arg(min, 2, 10, QChar('0'))
            .arg(sec, 2, 10, QChar('0'))
            .arg(ms,  3, 10, QChar('0'));
    }

    // Nav timestamp  →  HH:MM:SS
    if (fi == 3) {
        const int64_t secs = static_cast<int64_t>(v);
        const int sec = static_cast<int>(secs % 60);
        const int min = static_cast<int>((secs / 60) % 60);
        const int hr  = static_cast<int>((secs / 3600) % 24);
        return QString("%1:%2:%3")
            .arg(hr,  2, 10, QChar('0'))
            .arg(min, 2, 10, QChar('0'))
            .arg(sec, 2, 10, QChar('0'));
    }

    // Bottom sample: empty when not detected (-1 maps to kNaN now, so unreachable,
    // but guard against legacy zero paths)
    if (fi == 23) return v < 0.0 ? QString{} : QString::number(static_cast<int>(v));

    const int prec = (fi >= 0 && fi < kFieldCount && m_precision[fi] >= 0)
                     ? m_precision[fi]
                     : kFieldDefs[fi].default_prec;
    if (prec < 0) return QString::number(v);
    return QString::number(v, 'f', prec);
}

QVariant SBPNavModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return {};
    const int ri  = index.row();
    const int col = index.column();
    if (ri < 0 || ri >= rowCount() || col < 0 || col >= m_visible.size()) return {};
    const int fi = m_visible[col];

    const double v = rawValue(ri, fi);
    if (role == Qt::DisplayRole)       return formatValue(v, fi);
    if (role == Qt::UserRole + 1)      return v;
    if (role == Qt::TextAlignmentRole) {
        const int align = (fi == 1) ? Qt::AlignLeft : Qt::AlignRight;
        return int(align | Qt::AlignVCenter);
    }
    return {};
}

QVariant SBPNavModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Vertical) {
        if (role == Qt::DisplayRole) return section + 1;
        return {};
    }
    if (section < 0 || section >= m_visible.size()) return {};
    const int fi = m_visible[section];
    if (role == Qt::UserRole)    return fi;
    if (role != Qt::DisplayRole) return {};

    if (m_coords_projected) {
        if (fi == 4) return QString("Northing\n(m)");
        if (fi == 5) return QString("Easting\n(m)");
        if (fi == 6) return QString("Fish\nNorthing");
        if (fi == 7) return QString("Fish\nEasting");
        if (fi == 8) return QString("Vessel\nNorthing");
        if (fi == 9) return QString("Vessel\nEasting");
    }

    const auto& fd = kFieldDefs[fi];
    if (fd.unit[0] == '\0') return QString(fd.name);
    return QString("%1\n(%2)").arg(fd.name).arg(fd.unit);
}

} // namespace dolphin::ui
