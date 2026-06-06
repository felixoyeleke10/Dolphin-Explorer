#include "ui/mainwindow/rightpanel/RightPanel.SubBottomDisplay.h"
#include "ui/shell/Theme.h"
#include "ui/features/subbottom/SubBottomPalette.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kKeyLabelW = 84;  // key column width in sub-bottom display rows

SubBottomDisplayModule::SubBottomDisplayModule(QWidget* parent)
    : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 2, 0, Theme::kSpacing2);
    vl->setSpacing(0);

    // Helper: create a key-value row and return its inner layout.
    auto makeRow = [&](const QString& key_text) -> QHBoxLayout* {
        auto* row = new QWidget(this);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(Theme::kSpacing4, 3, Theme::kSpacing4, 5);
        rl->setSpacing(Theme::kSpacing3);
        auto* lbl = new QLabel(key_text, row);
        lbl->setObjectName("inspMetaKey");
        lbl->setFixedWidth(kKeyLabelW);
        lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        rl->addWidget(lbl);
        vl->addWidget(row);
        return rl;
    };

    // -- Palette ---------------------------------------------------------------
    {
        auto* rl = makeRow(tr("Palette"));
        m_palette = new QComboBox(this);
        m_palette->setObjectName("avPaletteCombo");
        m_palette->setToolTip(
            tr("Colour palette used to display SBP trace amplitudes.\n"
               "Greyscale: absolute value → luminance.\n"
               "Inverted Grey: reversed — black = peak amplitude.\n"
               "Seismic: positive → red, negative → blue (shows polarity).\n"
               "Thermal: black → purple → orange → yellow for amplitude intensity."));
        for (int i = 0; i < SbpPalette::Count; ++i)
            m_palette->addItem(SbpPalette::name(i));
        connect(m_palette, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { emitAndSave(); });
        rl->addWidget(m_palette, 1);
    }

    // -- Gain ------------------------------------------------------------------
    {
        auto* rl = makeRow(tr("Gain"));
        m_gain = new QDoubleSpinBox(this);
        m_gain->setObjectName("inspSpin");
        m_gain->setRange(0.1, 20.0);
        m_gain->setSingleStep(0.1);
        m_gain->setDecimals(1);
        m_gain->setSuffix(" ×");
        m_gain->setToolTip(
            tr("Amplitude multiplier applied before palette mapping.\n"
               "1.0 = as-recorded. Increase to brighten weak sub-bottom reflectors;\n"
               "strong reflectors will saturate (clip) at high values."));
        connect(m_gain, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) { emitAndSave(); });
        rl->addWidget(m_gain, 1);
    }

    // -- Contrast -----------------------------------------------------------------
    {
        auto* rl = makeRow(tr("Contrast"));
        m_contrast = new QDoubleSpinBox(this);
        m_contrast->setObjectName("inspSpin");
        m_contrast->setRange(0.5, 3.0);
        m_contrast->setSingleStep(0.1);
        m_contrast->setDecimals(1);
        m_contrast->setToolTip(
            tr("Power-curve exponent applied after gain.\n"
               "1.0 = linear (unchanged).\n"
               ">1 stretches mid-range reflectors, adding contrast in weak layers.\n"
               "<1 compresses dynamic range, useful for saturated data."));
        connect(m_contrast, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) { emitAndSave(); });
        rl->addWidget(m_contrast, 1);
    }

    // -- Polarity --------------------------------------------------------------
    {
        auto* rl = makeRow(tr("Polarity"));
        m_polarity = new QCheckBox(tr("Invert"), this);
        m_polarity->setObjectName("inspCheck");
        m_polarity->setToolTip(
            tr("Flip the polarity of all trace amplitudes before palette mapping.\n"
               "Useful when recorded data polarity is reversed from convention.\n"
               "With the Seismic palette: swaps red and blue lobes."));
        connect(m_polarity, &QCheckBox::toggled,
                this, [this](bool) { emitAndSave(); });
        rl->addWidget(m_polarity);
        rl->addStretch(1);
    }

    // -- Bottom track ----------------------------------------------------------
    {
        auto* rl = makeRow(tr("Bottom track"));
        m_bt_check = new QCheckBox(tr("Show"), this);
        m_bt_check->setObjectName("inspCheck");
        m_bt_check->setToolTip(
            tr("Show or hide the seabed pick overlay on the SBP section.\n"
               "The red line marks the first-return seabed sample detected at import.\n"
               "Depth is computed using the sound speed set in the viewer."));
        connect(m_bt_check, &QCheckBox::toggled,
                this, [this](bool) { emitAndSave(); });
        rl->addWidget(m_bt_check);
        rl->addStretch(1);
    }

    vl->addStretch(1);

    // Restore persisted state — same keys as SubBottomDisplayPanel so both panels
    // start in sync with the most recently saved settings.
    QSettings s;
    SubBottomDisplayParams p;
    p.palette_index     = s.value("sbpDisplay/palette",        SbpPalette::Greyscale).toInt();
    p.gain              = static_cast<float>(s.value("sbpDisplay/gain",           1.0).toDouble());
    p.contrast          = static_cast<float>(s.value("sbpDisplay/contrast",       1.0).toDouble());
    p.polarity_invert   = s.value("sbpDisplay/polarityInvert", false).toBool();
    p.show_bottom_track = s.value("sbpDisplay/showBt",         true).toBool();
    setParams(p);
}

SubBottomDisplayParams SubBottomDisplayModule::currentParams() const
{
    SubBottomDisplayParams p;
    if (m_palette)   p.palette_index     = m_palette->currentIndex();
    if (m_gain)      p.gain              = static_cast<float>(m_gain->value());
    if (m_contrast)  p.contrast          = static_cast<float>(m_contrast->value());
    if (m_polarity)  p.polarity_invert   = m_polarity->isChecked();
    if (m_bt_check)  p.show_bottom_track = m_bt_check->isChecked();
    return p;
}

void SubBottomDisplayModule::setParams(const SubBottomDisplayParams& p)
{
    QSignalBlocker b1(m_palette),  b2(m_gain), b3(m_bt_check),
                   b4(m_contrast), b5(m_polarity);
    if (m_palette)   m_palette->setCurrentIndex(p.palette_index);
    if (m_gain)      m_gain->setValue(p.gain);
    if (m_contrast)  m_contrast->setValue(p.contrast);
    if (m_polarity)  m_polarity->setChecked(p.polarity_invert);
    if (m_bt_check)  m_bt_check->setChecked(p.show_bottom_track);
}

void SubBottomDisplayModule::emitAndSave()
{
    const SubBottomDisplayParams p = currentParams();
    QSettings s;
    s.setValue("sbpDisplay/palette",        p.palette_index);
    s.setValue("sbpDisplay/gain",           static_cast<double>(p.gain));
    s.setValue("sbpDisplay/contrast",       static_cast<double>(p.contrast));
    s.setValue("sbpDisplay/polarityInvert", p.polarity_invert);
    s.setValue("sbpDisplay/showBt",         p.show_bottom_track);
    emit paramsChanged(p);
}

} // namespace dolphin::ui
