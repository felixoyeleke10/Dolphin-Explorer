#include "ui/mainwindow/panels/GainControlPanel.h"
#include "ui/features/waterfall/components/WfValueRow.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
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

    // Mode (Global / Variable)
    {
        auto* row = new QWidget(agc_rows);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);
        auto* lbl = new QLabel(tr("Mode"), row);
        lbl->setObjectName("wfSliderLabel");
        m_agc_mode = new QComboBox(row);
        m_agc_mode->addItems({ tr("Global"), tr("Variable") });
        m_agc_mode->setObjectName("wfCombo");
        m_agc_mode->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_agc_mode->setToolTip(
            tr("Global: one gain model for the loaded line; best first choice.\n"
               "Variable: adapts along-track using a moving window."));
        rl->addWidget(lbl); rl->addWidget(m_agc_mode);
        agc_l->addWidget(row);
    }

    m_agc_strength = new WfValueRow(tr("Strength"), 0.0, 100.0, 50.0, 1.0, 0, "%", agc_rows);
    m_agc_strength->setToolTip(
        tr("How strongly AGC normalises brightness.\n"
           "Start at 50 %. Reduce if shadows/contrast look flattened; increase if the line is still uneven."));
    agc_l->addWidget(m_agc_strength);

    // Along-Track Window — Variable mode only
    m_agc_along_track_row = new QWidget(agc_rows);
    m_agc_along_track_row->setVisible(false);
    {
        auto* atl = new QVBoxLayout(m_agc_along_track_row);
        atl->setContentsMargins(0, 0, 0, 0);
        atl->setSpacing(3);
        m_agc_along_track = new WfValueRow(tr("Along-Track Window"), 10, 500, 50, 5, 0,
                                           " pings", m_agc_along_track_row);
        m_agc_along_track->setToolTip(
            tr("Pings used by Variable AGC to estimate local gain.\n"
               "Start at 50. Smaller follows rapid changes; larger is smoother."));
        atl->addWidget(m_agc_along_track);
    }
    agc_l->addWidget(m_agc_along_track_row);

    // Smoothing type (Mean / Median)
    {
        auto* row = new QWidget(agc_rows);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);
        auto* lbl = new QLabel(tr("Smoothing Type"), row);
        lbl->setObjectName("wfSliderLabel");
        m_agc_smooth_type = new QComboBox(row);
        m_agc_smooth_type->addItems({ tr("Mean"), tr("Median") });
        m_agc_smooth_type->setObjectName("wfCombo");
        m_agc_smooth_type->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_agc_smooth_type->setToolTip(
            tr("Mean: smoother estimate for clean data.\n"
               "Median: more robust with bright targets, spikes, or dropouts."));
        rl->addWidget(lbl); rl->addWidget(m_agc_smooth_type);
        agc_l->addWidget(row);
    }

    m_agc_smooth_win = new WfValueRow(tr("Smoothing Window"), 1, 50, 5, 1, 0, "", agc_rows);
    m_agc_smooth_win->setToolTip(
        tr("Smooths the AGC gain curve across range samples.\n"
           "Start at 5. Increase if patchy; decrease if detail smears."));
    agc_l->addWidget(m_agc_smooth_win);

    m_agc_edge_skip = new WfValueRow(tr("Edge Skip"), 0, 500, 50, 5, 0, " smpl", agc_rows);
    m_agc_edge_skip->setToolTip(
        tr("Samples near nadir ignored while estimating AGC.\n"
           "Start at 50 to avoid the water column/nadir dominating the gain estimate."));
    agc_l->addWidget(m_agc_edge_skip);

    m_agc_noise_floor = new WfValueRow(tr("Noise Floor"), 0, 20, 2, 1, 0, "%", agc_rows);
    m_agc_noise_floor->setToolTip(
        tr("Lowest percentage treated as noise during AGC.\n"
           "Start at 2 %. Increase if low-level noise is amplified."));
    agc_l->addWidget(m_agc_noise_floor);

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

    // Apply is a single shared bar at the bottom of the right-panel (see
    // MainWindow): this section only edits values and contributes them via
    // writeInto(). No per-section Apply buttons.
    connect(m_tvg_en, &QCheckBox::toggled, this, &GainControlPanel::updateControlStates);
    connect(m_agc_en, &QCheckBox::toggled, this, &GainControlPanel::updateControlStates);
    connect(m_agc_mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateControlStates(); });
    connect(m_arc_en, &QCheckBox::toggled, this, &GainControlPanel::updateControlStates);

    setParams(m_params);
}

void GainControlPanel::writeInto(WaterfallParams& p) const
{
    p.tvg.enabled    = m_tvg_en->isChecked();
    p.tvg.spreading  = static_cast<float>(m_tvg_spread->value());
    p.tvg.absorption = static_cast<float>(m_tvg_absorb->value());

    p.agc.enabled           = m_agc_en->isChecked();
    p.agc.mode              = (m_agc_mode->currentIndex() == 1)
                            ? AgcMode::Variable : AgcMode::Global;
    p.agc.strength          = static_cast<float>(m_agc_strength->value() / 100.0);
    p.agc.along_track_win   = m_agc_along_track->intValue();
    p.agc.smoothing_type    = (m_agc_smooth_type->currentIndex() == 1)
                            ? AgcSmoothingType::Median : AgcSmoothingType::Mean;
    p.agc.smoothing_win     = m_agc_smooth_win->intValue();
    p.agc.edge_skip_samples = m_agc_edge_skip->intValue();
    p.agc.noise_floor_pct   = static_cast<float>(m_agc_noise_floor->value());

    p.arc.enabled     = m_arc_en->isChecked();
    p.arc.exponent    = static_cast<float>(m_arc_exp->value());
    p.arc.gain_cap_db = static_cast<float>(m_arc_gain_cap->value());
}

void GainControlPanel::setParams(const WaterfallParams& p)
{
    m_params = p;

    const QSignalBlocker b1(m_tvg_en),    b2(m_tvg_spread), b3(m_tvg_absorb);
    const QSignalBlocker b4(m_agc_en),    b5(m_agc_strength), b5b(m_agc_mode);
    const QSignalBlocker b5c(m_agc_along_track), b5d(m_agc_smooth_type);
    const QSignalBlocker b5e(m_agc_smooth_win),  b5f(m_agc_edge_skip), b5g(m_agc_noise_floor);
    const QSignalBlocker b6(m_arc_en),    b7(m_arc_exp), b8(m_arc_gain_cap);

    m_tvg_en->setChecked(p.tvg.enabled);
    m_tvg_spread->setValue(p.tvg.spreading);
    m_tvg_absorb->setValue(p.tvg.absorption);

    m_agc_en->setChecked(p.agc.enabled);
    m_agc_mode->setCurrentIndex(p.agc.mode == AgcMode::Variable ? 1 : 0);
    m_agc_strength->setValue(static_cast<double>(p.agc.strength) * 100.0);
    m_agc_along_track->setValue(p.agc.along_track_win);
    m_agc_smooth_type->setCurrentIndex(p.agc.smoothing_type == AgcSmoothingType::Median ? 1 : 0);
    m_agc_smooth_win->setValue(p.agc.smoothing_win);
    m_agc_edge_skip->setValue(p.agc.edge_skip_samples);
    m_agc_noise_floor->setValue(p.agc.noise_floor_pct);

    m_arc_en->setChecked(p.arc.enabled);
    m_arc_exp->setValue(p.arc.exponent);
    m_arc_gain_cap->setValue(p.arc.gain_cap_db);

    updateControlStates();
}

void GainControlPanel::updateControlStates()
{
    m_tvg_spread->setEnabled(m_tvg_en->isChecked());
    m_tvg_absorb->setEnabled(m_tvg_en->isChecked());

    const bool agc = m_agc_en->isChecked();
    m_agc_mode->setEnabled(agc);
    m_agc_strength->setEnabled(agc);
    m_agc_smooth_type->setEnabled(agc);
    m_agc_smooth_win->setEnabled(agc);
    m_agc_edge_skip->setEnabled(agc);
    m_agc_noise_floor->setEnabled(agc);
    m_agc_along_track->setEnabled(agc);
    m_agc_along_track_row->setVisible(agc && m_agc_mode->currentIndex() == 1);

    m_arc_exp->setEnabled(m_arc_en->isChecked());
    m_arc_gain_cap->setEnabled(m_arc_en->isChecked());
}

} // namespace dolphin::ui
