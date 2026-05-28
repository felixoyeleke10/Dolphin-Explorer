#include "ui/mainwindow/rightpanel/RightPanel.SbpSignal.h"
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
#include <QVBoxLayout>

namespace dolphin::ui {

SbpSignalModule::SbpSignalModule(QWidget* parent) : QWidget(parent)
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

    // ── Envelope ───────────────────────────────────────────────────────────────
    m_envelope_en = new QCheckBox(tr("Envelope"), container);
    m_envelope_en->setObjectName("ctrlToggle");
    m_envelope_en->setToolTip(
        tr("Compute instantaneous amplitude (Hilbert envelope) of each trace.\n"
           "Rectifies the signal so both positive and negative half-cycles\n"
           "contribute equally to brightness.\n"
           "Useful for analysing reflection strength without polarity ambiguity.\n"
           "Requires Apply."));
    vl->addWidget(m_envelope_en);

    // ── DC Removal ─────────────────────────────────────────────────────────────
    auto* div1 = new QFrame(container); div1->setObjectName("ctrlDivider");
    div1->setFixedHeight(Theme::kSepSz); vl->addWidget(div1);

    m_dc_removal_en = new QCheckBox(tr("DC Removal"), container);
    m_dc_removal_en->setObjectName("ctrlToggle");
    m_dc_removal_en->setToolTip(
        tr("Subtract the per-trace mean before rendering.\n"
           "Removes any DC offset introduced by the acquisition hardware.\n"
           "A non-zero mean shifts the entire trace and distorts palette mapping.\n"
           "Requires Apply."));
    vl->addWidget(m_dc_removal_en);

    // ── Bandpass ───────────────────────────────────────────────────────────────
    auto* div2 = new QFrame(container); div2->setObjectName("ctrlDivider");
    div2->setFixedHeight(Theme::kSepSz); vl->addWidget(div2);

    m_bandpass_en = new QCheckBox(tr("Bandpass"), container);
    m_bandpass_en->setObjectName("ctrlToggle");
    m_bandpass_en->setToolTip(
        tr("Apply a bandpass filter to each trace before rendering.\n"
           "Attenuates frequencies below Low cut and above High cut.\n"
           "Reduces low-frequency swell noise and high-frequency transducer ringing.\n"
           "Requires Apply."));
    vl->addWidget(m_bandpass_en);

    auto* bp_grid = new QWidget(container);
    auto* bg = new QGridLayout(bp_grid);
    bg->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    bg->setSpacing(3);
    bg->setColumnStretch(1, 1);

    auto* lo_lbl = new QLabel(tr("Low cut"), bp_grid);
    lo_lbl->setObjectName("ctrlParamLabel");
    m_bp_lo_hz = new QDoubleSpinBox(bp_grid);
    m_bp_lo_hz->setObjectName("ctrlSpinBox");
    m_bp_lo_hz->setRange(0.0, 10000.0);
    m_bp_lo_hz->setSingleStep(50.0);
    m_bp_lo_hz->setDecimals(0);
    m_bp_lo_hz->setSuffix(" Hz");
    m_bp_lo_hz->setFixedHeight(Theme::kInputH);
    m_bp_lo_hz->setValue(100.0);
    m_bp_lo_hz->setToolTip(
        tr("Lower frequency cut-off of the bandpass filter.\n"
           "Signals below this frequency are attenuated.\n"
           "Typical SBP: 100–500 Hz to suppress low-frequency swell noise."));
    bg->addWidget(lo_lbl,     0, 0);
    bg->addWidget(m_bp_lo_hz, 0, 1);

    auto* hi_lbl = new QLabel(tr("High cut"), bp_grid);
    hi_lbl->setObjectName("ctrlParamLabel");
    m_bp_hi_hz = new QDoubleSpinBox(bp_grid);
    m_bp_hi_hz->setObjectName("ctrlSpinBox");
    m_bp_hi_hz->setRange(0.0, 20000.0);
    m_bp_hi_hz->setSingleStep(100.0);
    m_bp_hi_hz->setDecimals(0);
    m_bp_hi_hz->setSuffix(" Hz");
    m_bp_hi_hz->setFixedHeight(Theme::kInputH);
    m_bp_hi_hz->setValue(3000.0);
    m_bp_hi_hz->setToolTip(
        tr("Upper frequency cut-off of the bandpass filter.\n"
           "Signals above this frequency are attenuated.\n"
           "Set below the Nyquist frequency for your data\n"
           "(Nyquist = sample_rate / 2)."));
    bg->addWidget(hi_lbl,     1, 0);
    bg->addWidget(m_bp_hi_hz, 1, 1);
    vl->addWidget(bp_grid);

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
        tr("Apply the current signal processing to this SBP line only.\n"
           "Check the result before applying to all lines."));

    m_apply_all_btn = new QPushButton(tr("Apply to All"), btn_row);
    m_apply_all_btn->setObjectName("ctrlApplyBtnSecondary");
    m_apply_all_btn->setToolTip(
        tr("Apply the current signal processing to every SBP line in the project.\n"
           "Use only after confirming the result looks correct on this line."));

    bl->addWidget(m_apply_line_btn);
    bl->addWidget(m_apply_all_btn);
    fl->addWidget(btn_row);

    connect(m_bandpass_en,    &QCheckBox::toggled, this, &SbpSignalModule::updateControlStates);
    connect(m_apply_line_btn, &QPushButton::clicked, this, &SbpSignalModule::onApplyLine);
    connect(m_apply_all_btn,  &QPushButton::clicked, this, &SbpSignalModule::onApplyAll);

    updateControlStates();
}

SbpSignalParams SbpSignalModule::currentParams() const
{
    SbpSignalParams p;
    p.envelope_en   = m_envelope_en->isChecked();
    p.dc_removal_en = m_dc_removal_en->isChecked();
    p.bandpass_en   = m_bandpass_en->isChecked();
    p.bp_lo_hz      = static_cast<float>(m_bp_lo_hz->value());
    p.bp_hi_hz      = static_cast<float>(m_bp_hi_hz->value());
    return p;
}

void SbpSignalModule::setParams(const SbpSignalParams& p)
{
    QSignalBlocker b1(m_envelope_en),  b2(m_dc_removal_en);
    QSignalBlocker b3(m_bandpass_en),  b4(m_bp_lo_hz), b5(m_bp_hi_hz);

    m_envelope_en->setChecked(p.envelope_en);
    m_dc_removal_en->setChecked(p.dc_removal_en);
    m_bandpass_en->setChecked(p.bandpass_en);
    m_bp_lo_hz->setValue(p.bp_lo_hz);
    m_bp_hi_hz->setValue(p.bp_hi_hz);

    updateControlStates();
}

void SbpSignalModule::onApplyLine()
{
    emit applyToLineRequested(currentParams());
}

void SbpSignalModule::onApplyAll()
{
    emit applyToAllRequested(currentParams());
}

void SbpSignalModule::updateControlStates()
{
    m_bp_lo_hz->setEnabled(m_bandpass_en->isChecked());
    m_bp_hi_hz->setEnabled(m_bandpass_en->isChecked());
}

} // namespace dolphin::ui
