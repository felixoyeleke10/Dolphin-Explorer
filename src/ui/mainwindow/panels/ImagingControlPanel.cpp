#include "ui/mainwindow/panels/ImagingControlPanel.h"
#include "ui/features/waterfall/components/WfValueRow.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace dolphin::ui {

ImagingControlPanel::ImagingControlPanel(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    setMinimumHeight(0);

    auto* fl = new QVBoxLayout(this);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(0);

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

    // -- ARN -------------------------------------------------------------------
    m_arn_en = new QCheckBox(tr("ARN"), container);
    m_arn_en->setObjectName("ctrlToggle");
    m_arn_en->setToolTip(
        tr("Adaptive Range Normalisation — removes across-track brightness gradients\n"
           "that remain after TVG and ARC. Works without geometry correction,\n"
           "but is most accurate after Slant Range Correction.\n"
           "Applied to assembled uint16 rows after TVG/ARC and before destripe.\n"
           "Requires Apply."));
    vl->addWidget(m_arn_en);

    auto* arn_rows = new QWidget(container);
    auto* arn_l = new QVBoxLayout(arn_rows);
    arn_l->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    arn_l->setSpacing(3);
    m_arn_strength = new WfValueRow(tr("Strength"), 0.0, 1.0, 0.80, 0.05, 2, "", arn_rows);
    m_arn_strength->setToolTip(
        tr("ARN correction strength (0 = off, 1 = full).\n"
           "Start at 0.80. Increase if residual along-range gradients remain;\n"
           "reduce if the image looks over-flattened or textureless."));
    m_arn_gain_cap = new WfValueRow(tr("Gain Cap"), 0.0, 24.0, 12.0, 1.0, 0, " dB", arn_rows);
    m_arn_gain_cap->setToolTip(
        tr("Maximum gain ARN may apply to any range column.\n"
           "Start at 12 dB. Lower values are safer; higher values can lift noise and water-column clutter."));
    arn_l->addWidget(m_arn_strength);
    arn_l->addWidget(m_arn_gain_cap);
    vl->addWidget(arn_rows);

    // -- Destripe --------------------------------------------------------------
    auto* div1 = new QFrame(container); div1->setObjectName("ctrlDivider");
    div1->setFixedHeight(Theme::kSepSz); vl->addWidget(div1);

    m_destripe_en = new QCheckBox(tr("Destripe"), container);
    m_destripe_en->setObjectName("ctrlToggle");
    m_destripe_en->setToolTip(
        tr("Removes horizontal stripe artefacts caused by ping-to-ping level variation.\n"
           "Uses a running median across pings to detect and subtract periodic banding.\n"
           "Effective against gain instability, surface returns, and hardware-level stripes.\n"
           "Requires Apply."));
    vl->addWidget(m_destripe_en);

    auto* ds_rows = new QWidget(container);
    auto* ds_l = new QVBoxLayout(ds_rows);
    ds_l->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    ds_l->setSpacing(3);
    m_destripe_capping = new WfValueRow(tr("Capping"), 1.0, 5.0, 2.0, 0.1, 1, "×", ds_rows);
    m_destripe_capping->setToolTip(
        tr("Maximum de-stripe correction factor.\n"
           "Start at 2.0×. Increase if heavy banding persists;\n"
           "reduce if fine along-track detail is being suppressed."));
    ds_l->addWidget(m_destripe_capping);
    vl->addWidget(ds_rows);

    // -- Beam Pattern ----------------------------------------------------------
    auto* div2 = new QFrame(container); div2->setObjectName("ctrlDivider");
    div2->setFixedHeight(Theme::kSepSz); vl->addWidget(div2);

    m_bpn_en = new QCheckBox(tr("Beam Pattern"), container);
    m_bpn_en->setObjectName("ctrlToggle");
    m_bpn_en->setToolTip(
        tr("Beam Pattern Normalisation — corrects transducer array sensitivity\n"
           "variation across the swath.\n"
           "Slant Range Correction is forced on while BPN is enabled so column\n"
           "statistics are computed in ground-range geometry. Requires Apply."));
    vl->addWidget(m_bpn_en);

    auto* bpn_rows = new QWidget(container);
    auto* bpn_l = new QVBoxLayout(bpn_rows);
    bpn_l->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    bpn_l->setSpacing(3);
    m_bpn_strength = new WfValueRow(tr("Strength"), 0.0, 1.0, 1.0, 0.05, 2, "", bpn_rows);
    m_bpn_strength->setToolTip(
        tr("Amount of beam-pattern correction (0 = off, 1 = full).\n"
           "Start at 1.00. Reduce toward 0.50 if the correction overshoots\n"
           "in near-nadir or far-range zones."));
    bpn_l->addWidget(m_bpn_strength);
    vl->addWidget(bpn_rows);

    // -- ML Enhance ------------------------------------------------------------
    auto* div3 = new QFrame(container); div3->setObjectName("ctrlDivider");
    div3->setFixedHeight(Theme::kSepSz); vl->addWidget(div3);

    m_ml_en = new QCheckBox(tr("ML Enhance"), container);
    m_ml_en->setObjectName("ctrlToggle");
    m_ml_en->setToolTip(
        tr("Adaptive local contrast enhancement (CLAHE-like).\n"
           "Divides the image into tiles and independently equalises each tile's\n"
           "histogram — revealing both shadow and highlight detail simultaneously.\n"
           "Useful for high dynamic-range bottom types. Requires Apply."));
    vl->addWidget(m_ml_en);

    auto* ml_rows = new QWidget(container);
    auto* ml_l = new QVBoxLayout(ml_rows);
    ml_l->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    ml_l->setSpacing(3);
    m_ml_clip_limit = new WfValueRow(tr("Clip Limit"), 1.0, 8.0, 2.0, 0.1, 1, "", ml_rows);
    m_ml_clip_limit->setToolTip(
        tr("Limits local contrast amplification within each tile.\n"
           "Start at 2.0. Increase to reveal more detail;\n"
           "decrease if noise or speckle becomes too prominent."));
    ml_l->addWidget(m_ml_clip_limit);
    vl->addWidget(ml_rows);

    // -- Slant Range Correction ------------------------------------------------
    auto* div4 = new QFrame(container); div4->setObjectName("ctrlDivider");
    div4->setFixedHeight(Theme::kSepSz); vl->addWidget(div4);

    m_src_en = new QCheckBox(tr("Slant Range Corr."), container);
    m_src_en->setObjectName("ctrlToggle");
    m_src_en->setToolTip(
        tr("Slant Range Correction — remaps samples from slant range (the diagonal\n"
           "acoustic path) to ground range (true horizontal seabed distance).\n"
           "Corrects the near-nadir compression and hyperbolic warp of the raw image.\n"
           "Recommended before enabling Beam Pattern Normalisation.\n"
           "Requires Apply."));
    vl->addWidget(m_src_en);

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
        tr("Apply the current imaging settings to this waterfall line only.\n"
           "All enabled processing steps run in pipeline order on the loaded data."));

    m_apply_all_btn = new QPushButton(tr("Apply to All"), btn_row);
    m_apply_all_btn->setObjectName("ctrlApplyBtnSecondary");
    m_apply_all_btn->setToolTip(
        tr("Apply the current imaging settings to every line in the project.\n"
           "Use only after confirming the result looks correct on this line."));

    bl->addWidget(m_apply_line_btn);
    bl->addWidget(m_apply_all_btn);
    fl->addWidget(btn_row);

    connect(m_arn_en,         &QCheckBox::toggled, this, &ImagingControlPanel::updateControlStates);
    connect(m_destripe_en,    &QCheckBox::toggled, this, &ImagingControlPanel::updateControlStates);
    connect(m_bpn_en, &QCheckBox::toggled, this, [this](bool on) {
        if (on) m_src_en->setChecked(true);
        updateControlStates();
    });
    connect(m_ml_en,          &QCheckBox::toggled, this, &ImagingControlPanel::updateControlStates);
    connect(m_src_en, &QCheckBox::toggled, this, [this](bool on) {
        if (!on && m_bpn_en->isChecked())
            m_src_en->setChecked(true);
    });
    connect(m_apply_line_btn, &QPushButton::clicked, this, &ImagingControlPanel::onApplyLine);
    connect(m_apply_all_btn,  &QPushButton::clicked, this, &ImagingControlPanel::onApplyAll);

    setParams(m_params);
}

WaterfallParams ImagingControlPanel::buildParams() const
{
    WaterfallParams p = m_params;
    p.arn.enabled          = m_arn_en->isChecked();
    p.arn.strength         = static_cast<float>(m_arn_strength->value());
    p.arn.gain_cap_db      = static_cast<float>(m_arn_gain_cap->value());

    p.destripe.enabled     = m_destripe_en->isChecked();
    p.destripe.capping     = static_cast<float>(m_destripe_capping->value());

    p.beam_pattern.enabled  = m_bpn_en->isChecked();
    p.beam_pattern.strength = static_cast<float>(m_bpn_strength->value());

    p.ml_enhance.enabled    = m_ml_en->isChecked();
    p.ml_enhance.clip_limit = static_cast<float>(m_ml_clip_limit->value());

    p.slant_range_correction = m_src_en->isChecked() || m_bpn_en->isChecked();
    return p;
}

void ImagingControlPanel::setParams(const WaterfallParams& p)
{
    m_params = p;

    const QSignalBlocker b1(m_arn_en),      b2(m_arn_strength), b3(m_arn_gain_cap);
    const QSignalBlocker b4(m_destripe_en), b5(m_destripe_capping);
    const QSignalBlocker b6(m_bpn_en),      b7(m_bpn_strength);
    const QSignalBlocker b8(m_ml_en),       b9(m_ml_clip_limit);
    const QSignalBlocker b10(m_src_en);

    m_arn_en->setChecked(p.arn.enabled);
    m_arn_strength->setValue(p.arn.strength);
    m_arn_gain_cap->setValue(p.arn.gain_cap_db);

    m_destripe_en->setChecked(p.destripe.enabled);
    m_destripe_capping->setValue(p.destripe.capping);

    m_bpn_en->setChecked(p.beam_pattern.enabled);
    m_bpn_strength->setValue(p.beam_pattern.strength);

    m_ml_en->setChecked(p.ml_enhance.enabled);
    m_ml_clip_limit->setValue(p.ml_enhance.clip_limit);

    m_src_en->setChecked(p.slant_range_correction || p.beam_pattern.enabled);

    updateControlStates();
}

void ImagingControlPanel::onApplyLine()
{
    m_params = buildParams();
    emit applyToLineRequested(m_params);
}

void ImagingControlPanel::onApplyAll()
{
    m_params = buildParams();
    emit applyToAllRequested(m_params);
}

void ImagingControlPanel::updateControlStates()
{
    m_arn_strength->setEnabled(m_arn_en->isChecked());
    m_arn_gain_cap->setEnabled(m_arn_en->isChecked());
    m_destripe_capping->setEnabled(m_destripe_en->isChecked());
    m_bpn_strength->setEnabled(m_bpn_en->isChecked());
    m_ml_clip_limit->setEnabled(m_ml_en->isChecked());
    m_src_en->setEnabled(!m_bpn_en->isChecked());
}

} // namespace dolphin::ui
