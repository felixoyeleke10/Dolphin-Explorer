#include "ui/mainwindow/rightpanel/RightPanel.SbpGain.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace dolphin::ui {

SbpGainModule::SbpGainModule(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    setMinimumHeight(0);

    auto* fl = makeCompactLayout<QVBoxLayout>(this);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    fl->addWidget(scroll, 1);

    auto* container = new QWidget;
    auto* vl = new QVBoxLayout(container);
    vl->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    vl->setSpacing(Theme::kSpacing1);
    scroll->setWidget(container);

    // -- Static Gain ------------------------------------------------------------
    m_static_en = new QCheckBox(tr("Static Gain"), container);
    m_static_en->setObjectName("ctrlToggle");
    m_static_en->setToolTip(
        tr("Apply a fixed gain offset to all samples in the profile.\n"
           "Useful for normalising the overall brightness of a specific file.\n"
           "Requires Apply."));
    vl->addWidget(m_static_en);

    auto* sg_grid = new QWidget(container);
    auto* sg = new QGridLayout(sg_grid);
    sg->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    sg->setSpacing(3);
    sg->setColumnStretch(1, 1);

    auto* sg_lbl = new QLabel(tr("Level"), sg_grid);
    sg_lbl->setObjectName("ctrlParamLabel");
    m_static_db = new QDoubleSpinBox(sg_grid);
    m_static_db->setObjectName("ctrlSpinBox");
    m_static_db->setRange(-20.0, 20.0);
    m_static_db->setSingleStep(1.0);
    m_static_db->setDecimals(1);
    m_static_db->setSuffix(" dB");
    m_static_db->setFixedHeight(Theme::kInputH);
    m_static_db->setToolTip(
        tr("Gain level in dB. Positive values brighten; negative values darken.\n"
           "+6 dB ≈ ×2.0,  −6 dB ≈ ×0.5.\n"
           "Stacks with the display gain in the Display section."));
    sg->addWidget(sg_lbl,      0, 0);
    sg->addWidget(m_static_db, 0, 1);
    vl->addWidget(sg_grid);

    // -- AGC --------------------------------------------------------------------
    auto* div1 = new QFrame(container); div1->setObjectName("ctrlDivider");
    div1->setFixedHeight(Theme::kSepSz); vl->addWidget(div1);

    m_agc_en = new QCheckBox(tr("AGC"), container);
    m_agc_en->setObjectName("ctrlToggle");
    m_agc_en->setToolTip(
        tr("Automatic Gain Control — running-RMS normalisation along the profile.\n"
           "Each trace is scaled by the inverse of the local RMS amplitude,\n"
           "keeping brightness roughly constant as sub-bottom reflectors\n"
           "weaken with depth. Requires Apply."));
    vl->addWidget(m_agc_en);

    auto* agc_grid = new QWidget(container);
    auto* ag = new QGridLayout(agc_grid);
    ag->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    ag->setSpacing(3);
    ag->setColumnStretch(1, 1);

    auto* agc_lbl = new QLabel(tr("Half-window"), agc_grid);
    agc_lbl->setObjectName("ctrlParamLabel");
    m_agc_window = new QSpinBox(agc_grid);
    m_agc_window->setObjectName("ctrlSpinBox");
    m_agc_window->setRange(5, 200);
    m_agc_window->setSingleStep(5);
    m_agc_window->setSuffix(tr(" traces"));
    m_agc_window->setFixedHeight(Theme::kInputH);
    m_agc_window->setValue(20);
    m_agc_window->setToolTip(
        tr("Half-window size in traces for the running RMS calculation.\n"
           "Smaller: fast response — may over-brighten isolated noise bursts.\n"
           "Larger: stable — reacts slowly to gradual amplitude changes."));
    ag->addWidget(agc_lbl,      0, 0);
    ag->addWidget(m_agc_window, 0, 1);
    auto* cap_lbl = new QLabel(tr("Gain cap"), agc_grid);
    cap_lbl->setObjectName("ctrlParamLabel");
    m_agc_gain_cap = new QDoubleSpinBox(agc_grid);
    m_agc_gain_cap->setObjectName("ctrlSpinBox");
    m_agc_gain_cap->setRange(0.0, 80.0);
    m_agc_gain_cap->setDecimals(0);
    m_agc_gain_cap->setSuffix(tr(" dB"));
    m_agc_gain_cap->setValue(40.0);
    m_agc_gain_cap->setFixedHeight(Theme::kInputH);
    m_agc_gain_cap->setToolTip(
        tr("Maximum positive AGC amplification.\n"
           "Lower this when weak traces contain substantial noise."));
    ag->addWidget(cap_lbl, 1, 0);
    ag->addWidget(m_agc_gain_cap, 1, 1);
    vl->addWidget(agc_grid);

    // -- Normalize --------------------------------------------------------------
    auto* div2 = new QFrame(container); div2->setObjectName("ctrlDivider");
    div2->setFixedHeight(Theme::kSepSz); vl->addWidget(div2);

    m_normalize_en = new QCheckBox(tr("Normalize"), container);
    m_normalize_en->setObjectName("ctrlToggle");
    m_normalize_en->setToolTip(
        tr("Per-trace peak normalisation — divides each trace by its maximum\n"
           "absolute amplitude, scaling every trace to full range.\n"
           "Removes all along-profile amplitude variation.\n"
           "Useful for comparing reflector geometry across variable-amplitude data.\n"
           "Requires Apply."));
    vl->addWidget(m_normalize_en);

    vl->addStretch(1);

    // Apply is the single shared bar at the bottom of the right-panel (see
    // MainWindow); this section only edits values and exposes them via currentParams().
    connect(m_static_en, &QCheckBox::toggled, this, &SbpGainModule::updateControlStates);
    connect(m_agc_en,    &QCheckBox::toggled, this, &SbpGainModule::updateControlStates);

    updateControlStates();
}

SbpGainParams SbpGainModule::currentParams() const
{
    SbpGainParams p;
    p.static_gain_en = m_static_en->isChecked();
    p.static_gain_db = static_cast<float>(m_static_db->value());
    p.agc_en         = m_agc_en->isChecked();
    p.agc_window     = m_agc_window->value();
    p.agc_gain_cap_db = static_cast<float>(m_agc_gain_cap->value());
    p.normalize_en   = m_normalize_en->isChecked();
    return p;
}

void SbpGainModule::setParams(const SbpGainParams& p)
{
    QSignalBlocker b1(m_static_en),    b2(m_static_db);
    QSignalBlocker b3(m_agc_en),       b4(m_agc_window), b4b(m_agc_gain_cap);
    QSignalBlocker b5(m_normalize_en);

    m_static_en->setChecked(p.static_gain_en);
    m_static_db->setValue(p.static_gain_db);
    m_agc_en->setChecked(p.agc_en);
    m_agc_window->setValue(p.agc_window);
    m_agc_gain_cap->setValue(p.agc_gain_cap_db);
    m_normalize_en->setChecked(p.normalize_en);

    updateControlStates();
}

void SbpGainModule::updateControlStates()
{
    m_static_db->setEnabled(m_static_en->isChecked());
    m_agc_window->setEnabled(m_agc_en->isChecked());
    m_agc_gain_cap->setEnabled(m_agc_en->isChecked());
}

} // namespace dolphin::ui
