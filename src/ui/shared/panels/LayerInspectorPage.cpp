#include "ui/shared/panels/LayerInspectorPage.h"
#include "ui/shell/Theme.h"
#include "ui/shared/widgets/CollapsibleSection.h"
#include "app/layers/LayerUtils.h"
#include "core/SpatialRef.h"
#include "render/sonar/SSSPalette.h"
#include "render/sonar/SonarDisplayParams.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shared/dialogs/SettingsDialog.h"
#include <QComboBox>
#include <QDateTime>
#include <QSettings>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>
#include <cmath>

namespace dolphin::ui {

namespace {

QString fmtDuration(double secs)
{
    if (secs <= 0.0) return "—";
    const int total = static_cast<int>(secs);
    const int h     = total / 3600;
    const int m     = (total % 3600) / 60;
    if (h > 0) return QString("%1h %2m").arg(h).arg(m, 2, 10, QChar('0'));
    return QString("%1m").arg(m);
}

QString fmtKhz(float hz)
{
    const float khz = hz / 1000.f;
    return QString("%1 kHz").arg(khz, 0, 'f', khz < 100.f ? 1 : 0);
}

} // namespace

LayerInspectorPage::LayerInspectorPage(QWidget* parent)
    : QWidget(parent)
{
    build();
}

// Returns a CollapsibleSection containing `content`, added to `vl`.
static CollapsibleSection* addSection(QVBoxLayout* vl, const QString& title, QWidget* content)
{
    auto* sec = new CollapsibleSection(title, content->parentWidget());
    sec->setContent(content);
    vl->addWidget(sec);
    return sec;
}

void LayerInspectorPage::makeRow(QVBoxLayout* vl, const QString& key, QLabel*& val_out)
{
    auto* row = new QWidget(this);
    auto* rl  = new QHBoxLayout(row);
    rl->setContentsMargins(Theme::kSpacing4, 3, Theme::kSpacing4, 3);
    rl->setSpacing(Theme::kSpacing3);

    auto* k = new QLabel(key, this);
    k->setObjectName("inspMetaKey");
    k->setFixedWidth(Theme::kKeyLabelW);
    k->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    val_out = new QLabel("—", this);
    val_out->setObjectName("inspMetaVal");
    val_out->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    rl->addWidget(k);
    rl->addWidget(val_out, 1);
    vl->addWidget(row);
}

void LayerInspectorPage::build()
{
    auto* fl = new QVBoxLayout(this);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(0);

    // -- INFO ---------------------------------------------------------------
    {
        auto* content = new QWidget(this);
        auto* vl = new QVBoxLayout(content);
        vl->setContentsMargins(0, 2, 0, Theme::kSpacing2);
        vl->setSpacing(0);
        {
            auto* row = new QWidget(this);
            auto* rl  = new QHBoxLayout(row);
            rl->setContentsMargins(Theme::kSpacing4, 3, Theme::kSpacing4, 3);
            rl->setSpacing(Theme::kSpacing3);
            m_pings_key = new QLabel(tr("Pings"), this);
            m_pings_key->setObjectName("inspMetaKey");
            m_pings_key->setFixedWidth(Theme::kKeyLabelW);
            m_pings_key->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            m_pings_val = new QLabel("—", this);
            m_pings_val->setObjectName("inspMetaVal");
            m_pings_val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rl->addWidget(m_pings_key);
            rl->addWidget(m_pings_val, 1);
            vl->addWidget(row);
        }
        makeRow(vl, tr("Modality"),  m_modality_val);
        makeRow(vl, tr("Frequency"), m_freq_val);
        makeRow(vl, tr("Duration"),  m_duration_val);
        makeRow(vl, tr("Sonar"),     m_sonar_val);
        makeRow(vl, tr("Date"),      m_date_val);
        makeRow(vl, tr("Survey"),    m_survey_val);
        makeRow(vl, tr("Vessel"),    m_vessel_val);
        makeRow(vl, tr("CRS"),       m_crs_val);
        vl->addStretch(1);
        addSection(fl, tr("Info"), content);
    }

    // -- DISPLAY ------------------------------------------------------------
    {
        auto* content = new QWidget(this);
        auto* vl = new QVBoxLayout(content);
        vl->setContentsMargins(0, 2, 0, Theme::kSpacing2);
        vl->setSpacing(0);
        {
            m_palette_row = new QWidget(this);
            auto* rl  = new QHBoxLayout(m_palette_row);
            rl->setContentsMargins(Theme::kSpacing4, 3, Theme::kSpacing4, 5);
            rl->setSpacing(Theme::kSpacing3);
            auto* k = new QLabel(tr("Palette"), this);
            k->setObjectName("inspMetaKey");
            k->setFixedWidth(Theme::kKeyLabelW);
            k->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            m_palette = new QComboBox(this);
            m_palette->setObjectName("avPaletteCombo");
            for (int i = 0; i < PaletteIndex::Count; ++i)
                m_palette->addItem(SSSPalette::name(i));
            {
                QSettings qs;
                const QVariant sss_idx = qs.value(QStringLiteral("sss/paletteIdx"));
                if (sss_idx.isValid()) {
                    m_palette->setCurrentIndex(sss_idx.toInt());
                } else {
                    m_palette->setCurrentIndex(SSSPalette::indexFromName(
                        qs.value(SettingsDialog::kKeyDefaultPalette,
                                 QStringLiteral("Gray")).toString()));
                }
            }
            connect(m_palette, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [this](int idx) { emit paletteChanged(idx); });
            rl->addWidget(k);
            rl->addWidget(m_palette, 1);
            vl->addWidget(m_palette_row);
        }
        vl->addStretch(1);
        m_display_section = addSection(fl, tr("Display"), content);
    }

    fl->addStretch(1);
}

void LayerInspectorPage::refresh(app::DataLayer* layer)
{
    if (!layer) {
        for (auto* l : { m_pings_val, m_modality_val, m_freq_val, m_duration_val,
                         m_sonar_val, m_date_val,
                         m_survey_val, m_vessel_val, m_crs_val })
            if (l) l->setText("—");
        if (m_pings_key) m_pings_key->setText(tr("Pings"));
        if (m_display_section) m_display_section->setVisible(false);
        return;
    }

    // -- Record count — label and value both depend on modality ------------
    {
        using Mod = app::Modality;
        int count = 0;
        QString key;
        switch (layer->modality) {
        case Mod::Sidescan:
            key   = tr("Pings");
            count = layer->bandSidescanCount();
            break;
        case Mod::SubBottom:
            key   = tr("Traces");
            count = layer->subBottomCount();
            break;
        case Mod::Magnetometer:
            key   = tr("Samples");
            count = layer->artifactCount();
            break;
        default:
            key   = tr("Records");
            count = layer->artifactCount();
            break;
        }
        if (m_pings_key) m_pings_key->setText(key);
        m_pings_val->setText(count > 0 ? QLocale().toString(count) : "—");
    }

    // -- Display section — only meaningful for sidescan amplitude display -----
    if (m_display_section)
        m_display_section->setVisible(layer->modality == app::Modality::Sidescan);

    // -- Palette — sync combo to per-layer override (or app default) -----------
    if (m_palette && layer->modality == app::Modality::Sidescan) {
        int pal = layer->sss_palette;
        if (pal < 0) {
            QSettings qs;
            const QVariant v = qs.value(QStringLiteral("sss/paletteIdx"));
            pal = v.isValid() ? v.toInt()
                              : SSSPalette::indexFromName(
                                    qs.value(SettingsDialog::kKeyDefaultPalette,
                                             QStringLiteral("Gray")).toString());
        }
        QSignalBlocker sb(m_palette);
        m_palette->setCurrentIndex(pal);
    }

    // -- Modality -----------------------------------------------------------
    m_modality_val->setText(
        QString::fromStdString(app::modalityLabel(layer->modality)));

    // -- Frequency ---------------------------------------------------------
    if (layer->frequency_hz > 0.f) {
        QString freq = fmtKhz(layer->frequency_hz);
        if (layer->low_frequency_hz > 0.f
                && std::fabs(layer->low_frequency_hz - layer->frequency_hz) > 1.f)
            freq = fmtKhz(layer->low_frequency_hz) + " / " + freq;
        m_freq_val->setText(freq);
    } else {
        m_freq_val->setText("—");
    }

    // -- Duration ----------------------------------------------------------
    m_duration_val->setText(
        fmtDuration(layer->end_time_utc - layer->start_time_utc));

    // -- Sonar --------------------------------------------------------------
    m_sonar_val->setText(layer->sonar_name.empty()
        ? "—" : QString::fromStdString(layer->sonar_name));

    // -- Date --------------------------------------------------------------
    if (layer->start_time_utc > 0.0) {
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(
            static_cast<qint64>(layer->start_time_utc), Qt::UTC);
        m_date_val->setText(dt.toString("d MMM yyyy"));
    } else {
        m_date_val->setText("—");
    }

    // -- Survey / Vessel ----------------------------------------------------
    m_survey_val->setText(layer->survey_name.empty()
        ? "—" : QString::fromStdString(layer->survey_name));
    m_vessel_val->setText(layer->vessel_name.empty()
        ? "—" : QString::fromStdString(layer->vessel_name));

    // -- CRS ---------------------------------------------------------------
    const QString crs = spatialRefDisplayName(layer->source_spatial_ref);
    m_crs_val->setText(crs.isEmpty() ? "—" : crs);
}

int LayerInspectorPage::currentPaletteIndex() const
{
    return m_palette ? m_palette->currentIndex() : 0;
}

void LayerInspectorPage::setPalette(int idx)
{
    if (!m_palette) return;
    QSignalBlocker sb(m_palette);
    m_palette->setCurrentIndex(idx);
}

} // namespace dolphin::ui
