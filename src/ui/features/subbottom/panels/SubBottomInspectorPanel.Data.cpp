// SubBottomInspectorPanel.Data.cpp — refresh() implementation.
#include "ui/features/subbottom/panels/SubBottomInspectorPanel.h"
#include "ui/shared/CoordFormat.h"
#include "app/layers/DataLayer.h"
#include "core/SpatialRef.h"

#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <cmath>

namespace dolphin::ui {

namespace {

QString formatDuration(float secs)
{
    if (secs <= 0.f) return "—";
    const int total = static_cast<int>(secs);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    if (h > 0) return QString("%1h %2m").arg(h).arg(m, 2, 10, QChar('0'));
    if (m > 0) return QString("%1m %2s").arg(m).arg(s, 2, 10, QChar('0'));
    return QString("%1s").arg(s);
}

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0) return "—";
    const float mb = static_cast<float>(bytes) / (1024.f * 1024.f);
    if (mb >= 1000.f) return QString("%1 GB").arg(static_cast<double>(mb / 1024.f), 0, 'f', 2);
    return QString("%1 MB").arg(static_cast<double>(mb), 0, 'f', 1);
}

} // anonymous namespace

void SubBottomInspectorPanel::refresh(const app::DataLayer* layer,
                                       const std::string&    source_path,
                                       uint64_t              source_size_bytes,
                                       int                   trace_count,
                                       int                   samples_per_trace,
                                       float                 duration_s,
                                       float                 line_length_m,
                                       float                 frequency_hz,
                                       float                 sound_speed_ms)
{
    auto dash = [](QLabel* l) { if (l) l->setText("—"); };

    if (!layer) {
        for (auto* l : { m_val_traces, m_val_samples, m_val_duration, m_val_length,
                         m_val_filename, m_val_fmt_size, m_val_crs, m_val_crs_kind,
                         m_val_freq, m_val_speed, m_val_survey, m_val_vessel })
            dash(l);
        return;
    }

    // -- Survey data -------------------------------------------------------
    m_val_traces->setText(trace_count > 0
        ? QString("%L1").arg(trace_count) : "—");

    m_val_samples->setText(samples_per_trace > 0
        ? QString("%L1").arg(samples_per_trace) : "—");

    m_val_duration->setText(formatDuration(duration_s > 0.f
        ? duration_s
        : static_cast<float>(layer->end_time_utc - layer->start_time_utc)));

    if (line_length_m > 0.f) {
        if (line_length_m >= 1000.f)
            m_val_length->setText(
                QString("%1 km").arg(static_cast<double>(line_length_m / 1000.f), 0, 'f', 2));
        else
            m_val_length->setText(QString("%1 m").arg(qRound(line_length_m)));
    } else {
        m_val_length->setText("—");
    }

    // -- Source file -------------------------------------------------------
    const QFileInfo fi(QString::fromStdString(source_path));
    const QString fname = fi.fileName().isEmpty() ? "—" : fi.fileName();
    {
        const int avail = m_val_filename->width() > 0 ? m_val_filename->width() : 180;
        m_val_filename->setText(
            m_val_filename->fontMetrics().elidedText(fname, Qt::ElideMiddle, avail));
        m_val_filename->setToolTip(fname == "—" ? QString{} : fname);
    }

    const uint64_t sz = source_size_bytes > 0
        ? source_size_bytes : static_cast<uint64_t>(fi.size());
    const QString ext  = fi.suffix().toUpper();
    const QString size = formatBytes(static_cast<qint64>(sz));
    m_val_fmt_size->setText(ext.isEmpty() ? size : ext + "  ·  " + size);

    // -- Coordinate system -------------------------------------------------
    const core::SpatialRef& ref = layer->source_spatial_ref;
    m_val_crs->setText(ref.empty() ? "—" : spatialRefDisplayName(ref));
    switch (ref.kind) {
    case core::SpatialRefKind::Geographic: m_val_crs_kind->setText(tr("Geographic")); break;
    case core::SpatialRefKind::Projected:  m_val_crs_kind->setText(tr("Projected"));  break;
    case core::SpatialRefKind::Local:      m_val_crs_kind->setText(tr("Local"));      break;
    default:                               m_val_crs_kind->setText("—");              break;
    }

    // -- Sonar -------------------------------------------------------------
    const float hz = frequency_hz > 0.f ? frequency_hz : layer->frequency_hz;
    if (hz > 0.f) {
        const float khz = hz / 1000.f;
        m_val_freq->setText(QString("%1 kHz").arg(static_cast<double>(khz), 0, 'f', khz < 100.f ? 1 : 0));
    } else {
        m_val_freq->setText("—");
    }

    m_val_speed->setText(sound_speed_ms > 0.f
        ? QString("%1 m/s").arg(qRound(sound_speed_ms))
        : "—");

    // -- Vessel ------------------------------------------------------------
    m_val_survey->setText(layer->survey_name.empty()
        ? "—" : QString::fromStdString(layer->survey_name));
    m_val_vessel->setText(layer->vessel_name.empty()
        ? "—" : QString::fromStdString(layer->vessel_name));
}

void SubBottomInspectorPanel::setViewScale(int px_per_trace, float px_per_sample)
{
    if (m_trace_width_spin) {
        QSignalBlocker sb(m_trace_width_spin);
        m_trace_width_spin->setValue(px_per_trace);
    }
    if (m_depth_scale_spin) {
        QSignalBlocker sb(m_depth_scale_spin);
        m_depth_scale_spin->setValue(static_cast<double>(px_per_sample));
    }
}

} // namespace dolphin::ui
