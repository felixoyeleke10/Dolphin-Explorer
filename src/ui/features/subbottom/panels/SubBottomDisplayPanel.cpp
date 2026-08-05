// SubBottomDisplayPanel.cpp — right display/processing panel for SubBottomWindow.

#include "ui/features/subbottom/panels/SubBottomDisplayPanel.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kLabelW    = 60;  // key label column width in display rows
static constexpr int kToggleBtnW = 40;  // On/Off toggle button fixed width


// static
QVBoxLayout* SubBottomDisplayPanel::makeSection(const QString& title,
                                                 bool           expanded,
                                                 QWidget*       parent,
                                                 QVBoxLayout*   parent_layout)
{
    auto* hdr = new QPushButton(parent);
    hdr->setCheckable(true);
    hdr->setChecked(expanded);
    hdr->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    hdr->setObjectName("avCollapseHdr");
    hdr->setFixedHeight(Theme::kColorBtnH);
    hdr->setFlat(true);

    auto* body = new QWidget(parent);
    body->setObjectName("avCollapseBody");
    auto* bl = makeCompactLayout<QVBoxLayout>(body);
    body->setVisible(expanded);

    auto syncLabel = [hdr, title](bool checked) {
        hdr->setText(QString(checked ? "▼  " : "▶  ") + title);
    };
    syncLabel(expanded);

    QObject::connect(hdr, &QPushButton::toggled, body, &QWidget::setVisible);
    QObject::connect(hdr, &QPushButton::toggled, [syncLabel](bool c) { syncLabel(c); });

    parent_layout->addWidget(hdr);
    parent_layout->addWidget(body);
    return bl;
}

SubBottomDisplayPanel::SubBottomDisplayPanel(QWidget* parent)
    : QFrame(parent)
{
    auto* fl = makeCompactLayout<QVBoxLayout>(this);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("av_panel_scroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    fl->addWidget(scroll, 1);

    auto* container = new QWidget;
    auto* vl        = makeCompactLayout<QVBoxLayout>(container);
    scroll->setWidget(container);

    // -- DISPLAY -------------------------------------------------------------
    {
        auto* bl = makeSection("Display", true, container, vl);

        // Palette
        auto* pal_row = new QWidget(container);
        auto* prl     = new QHBoxLayout(pal_row);
        prl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1, Theme::kSpacing4, Theme::kSpacing1);
        prl->setSpacing(Theme::kSpacing2);

        auto* pal_lbl = new QLabel(tr("Palette"), container);
        pal_lbl->setObjectName("avMetaKey");
        pal_lbl->setFixedWidth(kLabelW);

        m_palette_combo = new QComboBox(container);
        m_palette_combo->setObjectName("avPaletteCombo");
        m_palette_combo->setToolTip(
            tr("Colour palette used to display trace amplitudes.\n"
               "Greyscale: absolute value → luminance.\n"
               "Inverted Grey: reversed — black = peak.\n"
               "Seismic: positive → red, negative → blue.\n"
               "Thermal: amplitude → black→purple→orange→yellow."));
        for (int i = 0; i < SbpPalette::Count; ++i)
            m_palette_combo->addItem(SbpPalette::name(i));
        m_palette_combo->setCurrentIndex(SbpPalette::Greyscale);
        prl->addWidget(pal_lbl);
        prl->addWidget(m_palette_combo, 1);
        bl->addWidget(pal_row);

        // Gain
        auto* gain_row = new QWidget(container);
        gain_row->setFixedHeight(Theme::kPanelRowH);
        auto* grl      = new QHBoxLayout(gain_row);
        grl->setContentsMargins(Theme::kSpacing4, 2, Theme::kSpacing4, 2);
        grl->setSpacing(Theme::kSpacing2);

        auto* gain_lbl = new QLabel(tr("Gain"), container);
        gain_lbl->setObjectName("avMetaKey");
        gain_lbl->setFixedWidth(kLabelW);

        m_gain_spin = new QDoubleSpinBox(container);
        m_gain_spin->setObjectName("sbpSpin");
        m_gain_spin->setRange(0.1, 20.0);
        m_gain_spin->setSingleStep(0.1);
        m_gain_spin->setDecimals(1);
        m_gain_spin->setValue(1.0);
        m_gain_spin->setToolTip(
            tr("Amplitude multiplier applied before palette mapping.\n"
               "1.0 = as-recorded. Increase to brighten weak reflectors;\n"
               "strong reflectors will saturate (clip) at high values."));
        grl->addWidget(gain_lbl);
        grl->addWidget(m_gain_spin, 1);
        bl->addWidget(gain_row);

        // Contrast
        auto* con_row = new QWidget(container);
        con_row->setFixedHeight(Theme::kPanelRowH);
        auto* crl = new QHBoxLayout(con_row);
        crl->setContentsMargins(Theme::kSpacing4, 2, Theme::kSpacing4, 2);
        crl->setSpacing(Theme::kSpacing2);

        auto* con_lbl = new QLabel(tr("Contrast"), container);
        con_lbl->setObjectName("avMetaKey");
        con_lbl->setFixedWidth(kLabelW);

        m_contrast_spin = new QDoubleSpinBox(container);
        m_contrast_spin->setObjectName("sbpSpin");
        m_contrast_spin->setRange(0.5, 3.0);
        m_contrast_spin->setSingleStep(0.1);
        m_contrast_spin->setDecimals(1);
        m_contrast_spin->setValue(1.0);
        m_contrast_spin->setToolTip(
            tr("Power-curve exponent applied after gain.\n"
               "1.0 = linear (unchanged).\n"
               ">1 stretches mid-range reflectors (more contrast in weak layers).\n"
               "<1 compresses the dynamic range (useful for saturated data)."));
        crl->addWidget(con_lbl);
        crl->addWidget(m_contrast_spin, 1);
        bl->addWidget(con_row);

        // Polarity
        auto* pol_row = new QWidget(container);
        pol_row->setFixedHeight(Theme::kPanelRowH);
        auto* prl2 = new QHBoxLayout(pol_row);
        prl2->setContentsMargins(Theme::kSpacing4, 2, Theme::kSpacing4, 2);
        prl2->setSpacing(Theme::kSpacing2);

        auto* pol_lbl = new QLabel(tr("Polarity"), container);
        pol_lbl->setObjectName("avMetaKey");
        pol_lbl->setFixedWidth(kLabelW);

        m_polarity_check = new QCheckBox(tr("Invert"), container);
        m_polarity_check->setObjectName("sbpCheck");
        m_polarity_check->setToolTip(
            tr("Flip the polarity of all trace amplitudes before palette mapping.\n"
               "Useful when the recorded data polarity is reversed from convention.\n"
               "With the Seismic palette: swaps red and blue lobes."));
        prl2->addWidget(pol_lbl);
        prl2->addWidget(m_polarity_check);
        prl2->addStretch();
        bl->addWidget(pol_row);

        connect(m_palette_combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { emitParams(true); });
        connect(m_gain_spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) { emitParams(true); });
        connect(m_contrast_spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) { emitParams(true); });
        connect(m_polarity_check, &QCheckBox::toggled,
                this, [this](bool) { emitParams(true); });
    }

    // -- BOTTOM TRACK ---------------------------------------------------------
    {
        auto* bl = makeSection("Bottom Track", true, container, vl);

        auto* bt_row = new QWidget(container);
        bt_row->setFixedHeight(Theme::kDialogBtnH);
        auto* brl    = new QHBoxLayout(bt_row);
        brl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1, Theme::kSpacing4, Theme::kSpacing1);
        brl->setSpacing(Theme::kSpacing2);

        auto* bt_lbl = new QLabel(tr("Overlay"), container);
        bt_lbl->setObjectName("avMetaKey");
        bt_lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        m_bt_toggle = new QToolButton(container);
        m_bt_toggle->setObjectName("avToggleBtn");
        m_bt_toggle->setCheckable(true);
        m_bt_toggle->setChecked(true);
        m_bt_toggle->setText(tr("On"));
        m_bt_toggle->setFixedWidth(kToggleBtnW);
        m_bt_toggle->setToolTip(
            tr("Show or hide the pre-computed seabed pick line.\n"
               "The red overlay marks the first-return seabed sample\n"
               "as detected by SegyReader at import time."));
        connect(m_bt_toggle, &QToolButton::toggled, this, [this](bool on) {
            m_bt_toggle->setText(on ? tr("On") : tr("Off"));
            emitParams(true);
        });

        brl->addWidget(bt_lbl);
        brl->addWidget(m_bt_toggle);
        bl->addWidget(bt_row);
    }

    // -- ACQUISITION ----------------------------------------------------------
    {
        auto* bl = makeSection("Acquisition", false, container, vl);

        auto* spd_row = new QWidget(container);
        spd_row->setFixedHeight(Theme::kPanelRowH);
        auto* srl     = new QHBoxLayout(spd_row);
        srl->setContentsMargins(Theme::kSpacing4, 2, Theme::kSpacing4, 2);
        srl->setSpacing(Theme::kSpacing2);

        auto* spd_lbl = new QLabel(tr("Sound spd"), container);
        spd_lbl->setObjectName("avMetaKey");
        spd_lbl->setFixedWidth(kLabelW);

        m_speed_spin = new QDoubleSpinBox(container);
        m_speed_spin->setObjectName("sbpSpin");
        m_speed_spin->setRange(1400.0, 1700.0);
        m_speed_spin->setSingleStep(10.0);
        m_speed_spin->setDecimals(0);
        m_speed_spin->setSuffix(tr(" m/s"));
        m_speed_spin->setValue(1500.0);
        m_speed_spin->setToolTip(
            tr("Acoustic propagation speed in water (default 1500 m/s).\n"
               "Used to convert two-way travel time to estimated depth:\n"
               "  depth = travel_time × speed ÷ 2\n"
               "Typical values: 1480–1530 m/s for seawater."));
        srl->addWidget(spd_lbl);
        srl->addWidget(m_speed_spin, 1);
        bl->addWidget(spd_row);

        connect(m_speed_spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) { emitParams(true); });
    }

    vl->addStretch();

    // Restore saved preferences
    {
        QSettings s;
        SubBottomDisplayParams p;
        p.palette_index     = s.value("sbpDisplay/palette",        SbpPalette::Greyscale).toInt();
        p.gain              = static_cast<float>(s.value("sbpDisplay/gain",           1.0).toDouble());
        p.contrast          = static_cast<float>(s.value("sbpDisplay/contrast",       1.0).toDouble());
        p.polarity_invert   = s.value("sbpDisplay/polarityInvert", false).toBool();
        p.show_bottom_track = s.value("sbpDisplay/showBt",         true).toBool();
        p.sound_speed_ms    = static_cast<float>(s.value("sbpDisplay/speed",          1500.0).toDouble());
        setParams(p);
    }
}

SubBottomDisplayParams SubBottomDisplayPanel::currentParams() const
{
    SubBottomDisplayParams p;
    if (m_palette_combo)  p.palette_index     = m_palette_combo->currentIndex();
    if (m_gain_spin)      p.gain              = static_cast<float>(m_gain_spin->value());
    if (m_contrast_spin)  p.contrast          = static_cast<float>(m_contrast_spin->value());
    if (m_polarity_check) p.polarity_invert   = m_polarity_check->isChecked();
    if (m_bt_toggle)      p.show_bottom_track = m_bt_toggle->isChecked();
    if (m_speed_spin)     p.sound_speed_ms    = static_cast<float>(m_speed_spin->value());
    return p;
}

void SubBottomDisplayPanel::setParams(const SubBottomDisplayParams& p)
{
    QSignalBlocker b1(m_palette_combo), b2(m_gain_spin),
                   b3(m_contrast_spin), b4(m_polarity_check),
                   b5(m_bt_toggle),     b6(m_speed_spin);
    if (m_palette_combo)  m_palette_combo->setCurrentIndex(p.palette_index);
    if (m_gain_spin)      m_gain_spin->setValue(p.gain);
    if (m_contrast_spin)  m_contrast_spin->setValue(p.contrast);
    if (m_polarity_check) m_polarity_check->setChecked(p.polarity_invert);
    if (m_bt_toggle) {
        m_bt_toggle->setChecked(p.show_bottom_track);
        m_bt_toggle->setText(p.show_bottom_track ? tr("On") : tr("Off"));
    }
    if (m_speed_spin) m_speed_spin->setValue(p.sound_speed_ms);
}

void SubBottomDisplayPanel::notifyParamsChanged()
{
    emitParams(true);
}

void SubBottomDisplayPanel::refreshParams()
{
    emitParams(false);
}

void SubBottomDisplayPanel::emitParams(bool persist)
{
    const SubBottomDisplayParams p = currentParams();
    if (persist) {
        QSettings s;
        s.setValue("sbpDisplay/palette",       p.palette_index);
        s.setValue("sbpDisplay/gain",          static_cast<double>(p.gain));
        s.setValue("sbpDisplay/contrast",      static_cast<double>(p.contrast));
        s.setValue("sbpDisplay/polarityInvert", p.polarity_invert);
        s.setValue("sbpDisplay/showBt",        p.show_bottom_track);
        s.setValue("sbpDisplay/speed",         static_cast<double>(p.sound_speed_ms));
    }
    emit paramsChanged(p);
    if (persist)
        emit userParamsEdited(p);   // user action → display-state authority
}

} // namespace dolphin::ui
