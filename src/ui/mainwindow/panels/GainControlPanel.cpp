#include "ui/mainwindow/panels/GainControlPanel.h"
#include "ui/features/waterfall/components/WfValueRow.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace dolphin::ui {

GainControlPanel::GainControlPanel(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    setMinimumHeight(0);

    auto* fl = makeCompactLayout<QVBoxLayout>(this);

    // -- Scrollable content ----------------------------------------------------
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

    // -- TVG -------------------------------------------------------------------
    m_tvg_en = new QCheckBox(tr("TVG"), container);
    m_tvg_en->setObjectName("ctrlToggle");
    m_tvg_en->setToolTip(
        tr("Time-Varying Gain — compensates signal decay with range.\n"
           "The acoustic signal weakens with distance; TVG applies an increasing\n"
           "gain curve so near-range and far-range returns appear equally bright.\n"
           "Requires Apply."));
    vl->addWidget(m_tvg_en);

    auto* tvg_rows = new QWidget(container);
    auto* tvg_l = new QVBoxLayout(tvg_rows);
    tvg_l->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    tvg_l->setSpacing(3);
    m_tvg_spread = new WfValueRow(tr("Spreading"), 0.0, 40.0, 20.0, 1.0, 0, " dB/dec", tvg_rows);
    m_tvg_spread->setToolTip(
        tr("Geometric spreading loss compensation in dB per decade of range.\n"
           "TVG is 0 dB at the ping blanking distance, or 1 m when blanking is unavailable.\n"
           "Start at 20 dB/dec.\n"
           "Increase if far range remains too dark after correction."));
    m_tvg_absorb = new WfValueRow(tr("Absorption"), 0.0, 2.0, 0.0, 0.01, 2, " dB/m", tvg_rows);
    m_tvg_absorb->setToolTip(
        tr("Absorption loss compensation in dB per metre of water path.\n"
           "Start at 0.00 dB/m. Increase gently only for high-frequency data or very lossy water.\n"
           "Too much will over-brighten the outer swath."));
    tvg_l->addWidget(m_tvg_spread);
    tvg_l->addWidget(m_tvg_absorb);
    vl->addWidget(tvg_rows);

    // -- AGC -------------------------------------------------------------------
    auto* div1 = new QFrame(container); div1->setObjectName("ctrlDivider");
    div1->setFixedHeight(Theme::kSepSz); vl->addWidget(div1);

    m_agc_en = new QCheckBox(tr("AGC"), container);
    m_agc_en->setObjectName("ctrlToggle");
    m_agc_en->setToolTip(
        tr("Automatic Gain Control — dynamically adjusts gain along the track\n"
           "to equalise overall brightness across pings.\n"
           "Useful when survey conditions change along a line.\n"
           "Requires Apply."));
    vl->addWidget(m_agc_en);

    auto* agc_rows = new QWidget(container);
    auto* agc_l = new QVBoxLayout(agc_rows);
    agc_l->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    agc_l->setSpacing(3);
    m_agc_strength = new WfValueRow(tr("Strength"), 0.0, 100.0, 50.0, 5.0, 0, "%", agc_rows);
    m_agc_strength->setToolTip(
        tr("AGC correction strength (0 % = off, 100 % = full normalisation).\n"
           "Start at 50 %. High values aggressively flatten brightness differences;\n"
           "low values apply subtle along-track levelling."));
    agc_l->addWidget(m_agc_strength);
    vl->addWidget(agc_rows);

    // -- ARC -------------------------------------------------------------------
    auto* div2 = new QFrame(container); div2->setObjectName("ctrlDivider");
    div2->setFixedHeight(Theme::kSepSz); vl->addWidget(div2);

    m_arc_en = new QCheckBox(tr("ARC"), container);
    m_arc_en->setObjectName("ctrlToggle");
    m_arc_en->setToolTip(
        tr("Angle Range Correction — compensates grazing-angle-dependent backscatter.\n"
           "Strong near-nadir returns and weak far-range returns are normalised\n"
           "based on the acoustic incidence angle.\n"
           "Requires a valid seabed pick or non-zero tow depth. Requires Apply."));
    vl->addWidget(m_arc_en);

    auto* arc_rows = new QWidget(container);
    auto* arc_l = new QVBoxLayout(arc_rows);
    arc_l->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    arc_l->setSpacing(3);
    m_arc_exp = new WfValueRow(tr("Exponent"), 0.5, 4.0, 1.5, 0.1, 1, "", arc_rows);
    m_arc_exp->setToolTip(
        tr("Controls how aggressively ARC boosts far-range low-grazing-angle returns.\n"
           "Start at 1.5. Lower is subtle; higher is aggressive and can over-brighten far range."));
    m_arc_gain_cap = new WfValueRow(tr("Gain Cap"), 0.0, 40.0, 12.0, 1.0, 0, " dB", arc_rows);
    m_arc_gain_cap->setToolTip(
        tr("Maximum ARC gain applied at far range.\n"
           "Start at 12 dB. Lower prevents noise lift; higher can recover weak far-range returns."));
    arc_l->addWidget(m_arc_exp);
    arc_l->addWidget(m_arc_gain_cap);
    vl->addWidget(arc_rows);

    vl->addStretch(1);

    // -- Apply buttons (pinned below scroll) -----------------------------------
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("ctrlDivider");
    fl->addWidget(sep);

    auto* btn_row = new QWidget(this);
    auto* bl = new QHBoxLayout(btn_row);
    bl->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2, Theme::kSpacing3, Theme::kSpacing3);
    bl->setSpacing(Theme::kSpacing1);

    m_apply_line_btn = new QPushButton(tr("Apply to Line"), btn_row);
    m_apply_line_btn->setObjectName("ctrlApplyBtn");
    m_apply_line_btn->setToolTip(
        tr("Apply the current gain settings to the waterfall for this line only.\n"
           "Check the result before applying to all lines."));

    m_apply_all_btn = new QPushButton(tr("Apply to All"), btn_row);
    m_apply_all_btn->setObjectName("ctrlApplyBtnSecondary");
    m_apply_all_btn->setToolTip(
        tr("Apply the current gain settings to every line in the project.\n"
           "Use only after confirming the result looks correct on this line."));

    bl->addWidget(m_apply_line_btn);
    bl->addWidget(m_apply_all_btn);
    fl->addWidget(btn_row);

    connect(m_tvg_en,         &QCheckBox::toggled, this, &GainControlPanel::updateControlStates);
    connect(m_agc_en,         &QCheckBox::toggled, this, &GainControlPanel::updateControlStates);
    connect(m_arc_en,         &QCheckBox::toggled, this, &GainControlPanel::updateControlStates);
    connect(m_apply_line_btn, &QPushButton::clicked, this, &GainControlPanel::onApplyLine);
    connect(m_apply_all_btn,  &QPushButton::clicked, this, &GainControlPanel::onApplyAll);

    setParams(m_params);
}

WaterfallParams GainControlPanel::buildParams() const
{
    WaterfallParams p = m_params;
    p.tvg.enabled    = m_tvg_en->isChecked();
    p.tvg.spreading  = static_cast<float>(m_tvg_spread->value());
    p.tvg.absorption = static_cast<float>(m_tvg_absorb->value());

    p.agc.enabled  = m_agc_en->isChecked();
    p.agc.strength = static_cast<float>(m_agc_strength->value() / 100.0);

    p.arc.enabled     = m_arc_en->isChecked();
    p.arc.exponent    = static_cast<float>(m_arc_exp->value());
    p.arc.gain_cap_db = static_cast<float>(m_arc_gain_cap->value());
    return p;
}

void GainControlPanel::setParams(const WaterfallParams& p)
{
    m_params = p;

    const QSignalBlocker b1(m_tvg_en),    b2(m_tvg_spread), b3(m_tvg_absorb);
    const QSignalBlocker b4(m_agc_en),    b5(m_agc_strength);
    const QSignalBlocker b6(m_arc_en),    b7(m_arc_exp), b8(m_arc_gain_cap);

    m_tvg_en->setChecked(p.tvg.enabled);
    m_tvg_spread->setValue(p.tvg.spreading);
    m_tvg_absorb->setValue(p.tvg.absorption);

    m_agc_en->setChecked(p.agc.enabled);
    m_agc_strength->setValue(static_cast<double>(p.agc.strength) * 100.0);

    m_arc_en->setChecked(p.arc.enabled);
    m_arc_exp->setValue(p.arc.exponent);
    m_arc_gain_cap->setValue(p.arc.gain_cap_db);

    updateControlStates();
}

void GainControlPanel::onApplyLine()
{
    m_params = buildParams();
    emit applyToLineRequested(m_params);
}

void GainControlPanel::onApplyAll()
{
    m_params = buildParams();
    emit applyToAllRequested(m_params);
}

void GainControlPanel::updateControlStates()
{
    m_tvg_spread->setEnabled(m_tvg_en->isChecked());
    m_tvg_absorb->setEnabled(m_tvg_en->isChecked());
    m_agc_strength->setEnabled(m_agc_en->isChecked());
    m_arc_exp->setEnabled(m_arc_en->isChecked());
    m_arc_gain_cap->setEnabled(m_arc_en->isChecked());
}

} // namespace dolphin::ui
