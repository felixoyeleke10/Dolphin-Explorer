// WaterfallInspectorPanel.Data.cpp — refresh, palette accessors.
#include "ui/features/waterfall/panels/WaterfallInspectorPanel.h"
#include "ui/shared/CoordFormat.h"
#include "geo/EpsgDatabase.h"
#include "app/layers/DataLayer.h"
#include "core/SpatialRef.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <cmath>

namespace dolphin::ui {

namespace {

QString formatDuration(double secs)
{
    if (secs <= 0.0) return "—";
    const int total = static_cast<int>(secs);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    if (h > 0) return QString("%1h %2m").arg(h).arg(m, 2, 10, QChar('0'));
    if (m > 0) return QString("%1m %2s").arg(m).arg(s, 2, 10, QChar('0'));
    return QString("%1s").arg(s);
}

} // anonymous namespace

int WaterfallInspectorPanel::currentPaletteIndex() const
{
    return m_palette_combo ? m_palette_combo->currentIndex() : 0;
}

void WaterfallInspectorPanel::setPalette(int idx)
{
    if (!m_palette_combo) return;
    QSignalBlocker sb(m_palette_combo);
    m_palette_combo->setCurrentIndex(idx);
}

void WaterfallInspectorPanel::setAmpBarChecked(bool on)
{
    if (!m_amp_bar_toggle) return;
    QSignalBlocker sb(m_amp_bar_toggle);
    m_amp_bar_toggle->setChecked(on);
    m_amp_bar_toggle->setText(on ? tr("On") : tr("Off"));
}

void WaterfallInspectorPanel::refresh(const app::DataLayer*  layer,
                                       int                    total_ssc_entries,
                                       float                  entries_per_row,
                                       int                    samples_per_ping,
                                       float                  line_length_m,
                                       float                  ping_frequency_hz,
                                       const std::string&     sonar_name,
                                       float                  sound_velocity_ms)
{
    auto dash = [](QLabel* l) { if (l) l->setText("—"); };

    if (!layer) {
        for (auto* l : { m_val_pings, m_val_samples, m_val_duration, m_val_length,
                         m_val_crs, m_val_crs_kind,
                         m_val_sonar_model, m_val_freq, m_val_sound_spd,
                         m_val_survey, m_val_vessel })
            dash(l);
        return;
    }

    // -- Survey data -------------------------------------------------------
    const int survey_pings = total_ssc_entries > 0
        ? static_cast<int>(total_ssc_entries / std::max(entries_per_row, 1.0f))
        : 0;
    m_val_pings->setText(survey_pings > 0
        ? QString("~%L1").arg(survey_pings) : "—");

    m_val_samples->setText(samples_per_ping > 0
        ? QString("%L1").arg(samples_per_ping) : "—");

    const double dur_s = layer->end_time_utc - layer->start_time_utc;
    m_val_duration->setText(formatDuration(dur_s));

    if (line_length_m > 0.f) {
        if (line_length_m >= 1000.f)
            m_val_length->setText(QString("%1 km").arg(line_length_m / 1000.f, 0, 'f', 2));
        else
            m_val_length->setText(QString("%1 m").arg(qRound(line_length_m)));
    } else {
        m_val_length->setText("—");
    }

    // -- Coordinate system -------------------------------------------------
    {
        const core::SpatialRef& ref = layer->source_spatial_ref.empty()
            ? core::SpatialRef{} : layer->source_spatial_ref;

        const bool unconfirmed = geo::epsgIsUnconfirmed(ref);
        const QString crs_text = spatialRefDisplayName(ref);

        m_val_crs->setText(crs_text);

        // Amber tint when CRS has not been confirmed by the user.
        m_val_crs->setStyleSheet(unconfirmed
            ? QStringLiteral("color: %1; font-style: italic;").arg(QLatin1String(Theme::kCaution))
            : "");

        switch (ref.kind) {
        case core::SpatialRefKind::Geographic: m_val_crs_kind->setText(tr("Geographic")); break;
        case core::SpatialRefKind::Projected:  m_val_crs_kind->setText(tr("Projected"));  break;
        case core::SpatialRefKind::Local:      m_val_crs_kind->setText(tr("Local"));      break;
        default:                               m_val_crs_kind->setText("—");              break;
        }

        if (m_btn_set_crs)
            m_btn_set_crs->setVisible(unconfirmed || ref.kind == core::SpatialRefKind::Unknown);
    }

    // -- Sonar parameters --------------------------------------------------
    {
        const std::string& sn = sonar_name.empty() ? layer->sonar_name : sonar_name;
        m_val_sonar_model->setText(sn.empty() ? "—" : QString::fromStdString(sn));
    }

    {
        const float hi_hz  = layer->frequency_hz > 0.f ? layer->frequency_hz : ping_frequency_hz;
        const float lo_hz  = layer->low_frequency_hz;
        auto fmtKhz = [](float hz) {
            float khz = hz / 1000.f;
            return QString("%1 kHz").arg(khz, 0, 'f', khz < 100.f ? 1 : 0);
        };
        if (hi_hz > 0.f) {
            QString s = fmtKhz(hi_hz);
            if (lo_hz > 0.f && std::fabs(lo_hz - hi_hz) > 1000.f)
                s = fmtKhz(lo_hz) + " / " + s;
            m_val_freq->setText(s);
        } else {
            m_val_freq->setText("—");
        }
    }

    m_val_sound_spd->setText(sound_velocity_ms > 0.f
        ? QString("%1 m/s").arg(qRound(sound_velocity_ms))
        : "—");

    // -- Vessel / survey ---------------------------------------------------
    m_val_survey->setText(layer->survey_name.empty()
        ? "—" : QString::fromStdString(layer->survey_name));
    m_val_vessel->setText(layer->vessel_name.empty()
        ? "—" : QString::fromStdString(layer->vessel_name));
}

} // namespace dolphin::ui
