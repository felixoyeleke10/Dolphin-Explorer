#include "ui/mainwindow/rightpanel/RightPanel.SbpGain.h"
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

    auto* fl = new QVBoxLayout(this);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(0);

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

    // ── Static Gain ────────────────────────────────────────────────────────────
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

    // ── AGC ────────────────────────────────────────────────────────────────────
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

    auto* agc_lbl = new QLabel(tr("Window"), agc_grid);
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
    vl->addWidget(agc_grid);

    // ── Normalize ──────────────────────────────────────────────────────────────
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

    // ── Apply buttons (pinned below scroll) ───────────────────────────────────
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
        tr("Apply the current gain settings to this SBP line only.\n"
           "Check the result before applying to all lines."));

    m_apply_all_btn = new QPushButton(tr("Apply to All"), btn_row);
    m_apply_all_btn->setObjectName("ctrlApplyBtnSecondary");
    m_apply_all_btn->setToolTip(
        tr("Apply the current gain settings to every SBP line in the project.\n"
           "Use only after confirming the result looks correct on this line."));

    bl->addWidget(m_apply_line_btn);
    bl->addWidget(m_apply_all_btn);
    fl->addWidget(btn_row);

    connect(m_static_en,      &QCheckBox::toggled, this, &SbpGainModule::updateControlStates);
    connect(m_agc_en,         &QCheckBox::toggled, this, &SbpGainModule::updateControlStates);
    connect(m_apply_line_btn, &QPushButton::clicked, this, &SbpGainModule::onApplyLine);
    connect(m_apply_all_btn,  &QPushButton::clicked, this, &SbpGainModule::onApplyAll);

    updateControlStates();
}

SbpGainParams SbpGainModule::currentParams() const
{
    SbpGainParams p;
    p.static_gain_en = m_static_en->isChecked();
    p.static_gain_db = static_cast<float>(m_static_db->value());
    p.agc_en         = m_agc_en->isChecked();
    p.agc_window     = m_agc_window->value();
    p.normalize_en   = m_normalize_en->isChecked();
    return p;
}

void SbpGainModule::setParams(const SbpGainParams& p)
{
    QSignalBlocker b1(m_static_en),    b2(m_static_db);
    QSignalBlocker b3(m_agc_en),       b4(m_agc_window);
    QSignalBlocker b5(m_normalize_en);

    m_static_en->setChecked(p.static_gain_en);
    m_static_db->setValue(p.static_gain_db);
    m_agc_en->setChecked(p.agc_en);
    m_agc_window->setValue(p.agc_window);
    m_normalize_en->setChecked(p.normalize_en);

    updateControlStates();
}

void SbpGainModule::onApplyLine()
{
    emit applyToLineRequested(currentParams());
}

void SbpGainModule::onApplyAll()
{
    emit applyToAllRequested(currentParams());
}

void SbpGainModule::updateControlStates()
{
    m_static_db->setEnabled(m_static_en->isChecked());
    m_agc_window->setEnabled(m_agc_en->isChecked());
}

} // namespace dolphin::ui
