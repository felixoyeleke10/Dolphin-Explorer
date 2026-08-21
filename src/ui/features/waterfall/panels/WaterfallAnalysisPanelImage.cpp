// WaterfallAnalysisPanelImage.cpp — IMAGE PROCESSING section

#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"
#include "ui/features/waterfall/components/WfToggleRow.h"
#include "ui/features/waterfall/components/WfValueRow.h"
#include "ui/features/waterfall/components/TvgCurveEditor.h"
#include "ui/features/waterfall/components/TvgEditorModeSwitch.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace dolphin::ui {

// Shared geometry — must match WfValueRow's internal kPillW / kPillR / kRowH.
static constexpr int kComboW  = 90;   // matches WfValueRow pill width
static constexpr int kLabelL  = 14;   // matches WfValueRow label left margin
static constexpr int kRowR    = 10;   // matches WfValueRow pill right margin
static constexpr int kRowH    = 28;   // matches WfValueRow fixed height

// Adds a label + QComboBox row with the same geometry as WfValueRow.
static QComboBox* addComboRow(QVBoxLayout* bl, const QString& lbl, QWidget* parent)
{
    auto* row = new QWidget(parent);
    row->setFixedHeight(kRowH);
    auto* rl  = new QHBoxLayout(row);
    rl->setContentsMargins(kLabelL, 3, kRowR, 3);
    rl->setSpacing(Theme::kSpacing2);

    auto* k = new QLabel(lbl, row);
    k->setObjectName("wfParamLabel");
    k->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* combo = new QComboBox(row);
    combo->setObjectName("wfCombo");
    combo->setFixedWidth(kComboW);

    rl->addWidget(k);
    rl->addWidget(combo);
    bl->addWidget(row);
    return combo;
}

void WaterfallAnalysisPanel::buildImageSection(QVBoxLayout* vl, QWidget* container)
{
    auto* bl = makeSection(tr("Image Processing"), true, container, vl, &m_image_hdr);

    // -- RANGE COMPENSATION ------------------------------------------------
    {
        auto* sub = new QLabel(tr("Range Compensation"), container);
        sub->setObjectName("wfSubSectionLabel");
        sub->setContentsMargins(kLabelL, 8, kRowR, 2);
        bl->addWidget(sub);
    }

    // TVG
    m_tvg_toggle = new WfToggleRow(tr("TVG (Time Varying Gain)"), false, container);
    m_tvg_toggle->setToolTip(
        tr("Compensates range-dependent acoustic spreading loss.\n"
           "Applied to raw 16-bit amplitudes before display processing.\n"
           "Gain = 0 dB at the blanking distance, rising with range.\n"
           "Suggested start: enable only when far range looks too dark.\n"
           "Spreading 20 dB/dec is a normal first value for open-water sidescan."));
    auto* tvg_header = new QWidget(container);
    auto* tvg_header_layout = makeCompactLayout<QHBoxLayout>(tvg_header);
    tvg_header_layout->addWidget(m_tvg_toggle, 1);
    m_tvg_editor_mode = new TvgEditorModeSwitch(tvg_header);
    tvg_header_layout->addWidget(m_tvg_editor_mode, 0, Qt::AlignVCenter);
    bl->addWidget(tvg_header);
    m_tvg_body = new QWidget(container);
    m_tvg_body->setVisible(false);
    {
        auto* tvg_bl = makeCompactLayout<QVBoxLayout>(m_tvg_body);
        m_tvg_numeric_body = new QWidget(m_tvg_body);
        auto* numeric_bl = makeCompactLayout<QVBoxLayout>(m_tvg_numeric_body);
        addValueRow(numeric_bl, tr("Spreading"),   m_tvg_spreading,
                    0, 40, 20, 1, 0, tr(" dB/dec"));
        m_tvg_spreading->setToolTip(
            tr("Controls how strongly TVG brightens far-range samples.\n"
               "TVG is 0 dB at the ping blanking distance, or 1 m when blanking is unavailable.\n"
               "Start with 20 dB/dec. Lower if far range becomes washed out; higher if it remains too dark.\n"
               "Typical range: 15-25 dB/dec."));
        addValueRow(numeric_bl, tr("Absorption"),  m_tvg_absorption,
                    0.0, 2.0, 0.0, 0.01, 2, tr(" dB/m"));
        m_tvg_absorption->setToolTip(
            tr("Adds extra far-range gain for water absorption loss.\n"
               "Start at 0.0 dB/m. Increase gently only for high-frequency data or very lossy water.\n"
               "Too much will over-brighten the outer swath."));
        tvg_bl->addWidget(m_tvg_numeric_body);

        m_tvg_curve = new TvgCurveEditor(m_tvg_body);
        tvg_bl->addWidget(m_tvg_curve);
        const auto editor_mode = m_tvg_editor_mode->mode();
        m_tvg_curve->setVisible(editor_mode == TvgEditorModeSwitch::Graph);
        m_tvg_numeric_body->setVisible(editor_mode == TvgEditorModeSwitch::Numeric);
        connect(m_tvg_editor_mode, &TvgEditorModeSwitch::modeChanged,
                this, [this](TvgEditorModeSwitch::Mode mode) {
                    m_tvg_curve->setVisible(mode == TvgEditorModeSwitch::Graph);
                    m_tvg_numeric_body->setVisible(mode == TvgEditorModeSwitch::Numeric);
                });
        connect(m_tvg_curve, &TvgCurveEditor::coefficientsChanged,
                this, [this](float spreading, float absorption) {
                    m_tvg_spreading->setValue(spreading);
                    m_tvg_absorption->setValue(absorption);
                    refreshImageDirty();
                });
        auto syncCurve = [this](double) {
            m_tvg_curve->setCoefficients(
                static_cast<float>(m_tvg_spreading->value()),
                static_cast<float>(m_tvg_absorption->value()));
        };
        connect(m_tvg_spreading, &WfValueRow::valueChanged, this, syncCurve);
        connect(m_tvg_absorption, &WfValueRow::valueChanged, this, syncCurve);
        m_tvg_curve->setCoefficients(20.f, 0.f);
    }
    bl->addWidget(m_tvg_body);

    // ARN
    m_arn_toggle = new WfToggleRow(tr("Adaptive Range Normalisation"), false, container);
    m_arn_toggle->setToolTip(
        tr("Per-column histogram normalisation across all loaded pings.\n"
           "Removes range-dependent shading without assuming a spreading model.\n"
           "Good for making the waterfall visually even from nadir to far range.\n"
           "Suggested start: Strength 60-80%, Gain Cap 8-12 dB."));
    bl->addWidget(m_arn_toggle);
    m_arn_body = new QWidget(container);
    m_arn_body->setVisible(false);
    {
        auto* arn_bl = makeCompactLayout<QVBoxLayout>(m_arn_body);
        addValueRow(arn_bl, tr("Strength"),    m_arn_strength,
                    0, 100, 80, 1, 0, tr("%"));
        m_arn_strength->setToolTip(
            tr("How much adaptive range normalisation is blended into the image.\n"
               "Start around 60-80%. Use lower values if natural bottom shading disappears."));
        addValueRow(arn_bl, tr("Gain Cap"),    m_arn_gain_cap,
                    0, 24, 12, 1, 0, tr(" dB"));
        m_arn_gain_cap->setToolTip(
            tr("Maximum gain ARN may apply to any range column.\n"
               "Start at 12 dB. Lower values are safer; higher values can lift noise and water-column clutter."));
        addValueRow(arn_bl, tr("Smooth Radius"), m_arn_smooth,
                    0, 100, 5, 1, 0, tr(" samp"));
        m_arn_smooth->setToolTip(
            tr("Half-window used to smooth the learned range-response profile."));
    }
    bl->addWidget(m_arn_body);

    connect(m_tvg_toggle, &WfToggleRow::toggled,
            this, &WaterfallAnalysisPanel::updateRangeCompVisibility);
    connect(m_arn_toggle, &WfToggleRow::toggled, this, [this](bool on) {
        updateRangeCompVisibility();
        if (on && m_src_toggle && !m_src_toggle->isChecked()) {
            QMessageBox* msg = new QMessageBox(this);
            msg->setWindowTitle(tr("Slant Range Correction Recommended"));
            msg->setIcon(QMessageBox::Information);
            msg->setText(tr("<b>Adaptive Range Normalisation works best after "
                            "Slant Range Correction is enabled.</b>"));
            msg->setInformativeText(
                tr("ARN normalises each column of the waterfall image to a common "
                   "amplitude level. Without Slant Range Correction, columns are in "
                   "<i>slant range</i> coordinates — each column mixes samples from "
                   "different grazing angles, so the column statistics ARN computes "
                   "reflect both the true range shading <i>and</i> angle-dependent "
                   "backscatter mixed together."
                   "<br><br>"
                   "Enabling Slant Range Correction first remaps samples to "
                   "<i>ground range</i>, so each column represents a consistent "
                   "horizontal distance from the towfish. ARN can then isolate and "
                   "correct true range shading cleanly."
                   "<br><br>"
                   "You can continue without SRC — ARN will still reduce visible "
                   "shading — but results will be more accurate with it enabled."));
            msg->setTextFormat(Qt::RichText);
            auto* btn_enable = msg->addButton(tr("Enable SRC && Continue"),
                                              QMessageBox::AcceptRole);
            msg->addButton(tr("Continue Without SRC"), QMessageBox::RejectRole);
            msg->exec();
            if (msg->clickedButton() == static_cast<QAbstractButton*>(btn_enable)) {
                m_src_toggle->setChecked(true);
            }
        }
    });

    // -- AGC ---------------------------------------------------------------
    bl->addWidget([] {
        auto* s = new QFrame; s->setFrameShape(QFrame::HLine);
        s->setObjectName("avHRule"); return s; }());

    {
        auto* sub = new QLabel(tr("Automatic Gain Control"), container);
        sub->setObjectName("wfSubSectionLabel");
        sub->setContentsMargins(kLabelL, 6, kRowR, 2);
        bl->addWidget(sub);
    }

    // AGC enable
    m_agc_enable_toggle = new WfToggleRow(tr("Enable AGC"), false, container);
    m_agc_enable_toggle->setToolTip(
        tr("Automatic Gain Control evens out brightness changes along the waterfall.\n"
           "Use when some sections are too bright or too dark compared with nearby pings.\n"
           "Suggested start: Global mode, Strength 40-60%."));
    bl->addWidget(m_agc_enable_toggle);

    m_agc_body = new QWidget(container);
    m_agc_body->setVisible(false);
    {
        auto* agc_bl = makeCompactLayout<QVBoxLayout>(m_agc_body);

        m_agc_mode_combo = addComboRow(agc_bl, tr("Mode"), m_agc_body);
        m_agc_mode_combo->addItem(tr("Global"));
        m_agc_mode_combo->addItem(tr("Variable"));
        m_agc_mode_combo->setToolTip(
            tr("Global: one gain model for the loaded line; best first choice.\n"
               "Variable: adapts along-track using a moving window; useful when brightness changes during the line."));

        addValueRow(agc_bl, tr("Strength"),
                    m_agc_strength, 0, 100, 50, 1, 0, tr("%"));
        m_agc_strength->setToolTip(
            tr("How strongly AGC normalises brightness.\n"
               "Start at 50%. Reduce if shadows and object contrast look flattened; increase if the line is still uneven."));

        // Along-Track Window — visible only in Variable mode
        m_agc_along_track_row = new QWidget(m_agc_body);
        m_agc_along_track_row->setVisible(false);
        {
            auto* atl = makeCompactLayout<QVBoxLayout>(m_agc_along_track_row);
            addValueRow(atl, tr("Along-Track Window"),
                        m_agc_along_track, 10, 500, 50, 5, 0, tr(" pings"));
            m_agc_along_track->setToolTip(
                tr("Number of pings used by Variable AGC to estimate local gain.\n"
                   "Start at 50 pings. Smaller follows rapid changes; larger is smoother and safer."));
        }
        agc_bl->addWidget(m_agc_along_track_row);

        m_agc_smooth_type_combo = addComboRow(agc_bl, tr("Smoothing Type"), m_agc_body);
        m_agc_smooth_type_combo->addItem(tr("Mean"));
        m_agc_smooth_type_combo->addItem(tr("Median"));
        m_agc_smooth_type_combo->setToolTip(
            tr("Mean: smoother gain estimate for clean data.\n"
               "Median: more robust when there are bright targets, spikes, or dropouts."));

        addValueRow(agc_bl, tr("Smoothing Window"),
                    m_agc_smooth_win, 1, 50, 5, 1, 0);
        m_agc_smooth_win->setToolTip(
            tr("Smooths the AGC gain curve along-track across neighbouring pings.\n"
               "Start at 5. Increase if gain changes look patchy."));
        addValueRow(agc_bl, tr("Edge Skip"),
                    m_agc_edge_skip, 0, 500, 50, 5, 0, tr(" smpl"));
        m_agc_edge_skip->setToolTip(
            tr("Samples near nadir ignored while estimating AGC.\n"
               "Start at 50 samples to avoid the water column/nadir dominating the gain estimate."));
        addValueRow(agc_bl, tr("Noise Floor"),
                    m_agc_noise_floor, 0, 20, 2, 1, 0, tr("%"));
        m_agc_noise_floor->setToolTip(
            tr("Lowest percentage treated as noise during AGC.\n"
               "Start at 2%. Increase if low-level noise is being amplified."));
        addValueRow(agc_bl, tr("Gain Cap"),
                    m_agc_gain_cap, 0, 60, 24, 1, 0, tr(" dB"));
        m_agc_gain_cap->setToolTip(
            tr("Maximum positive gain AGC may apply.\n"
               "Start at 24 dB. Lower values are safer for noisy or weak records."));
    }
    bl->addWidget(m_agc_body);

    connect(m_agc_enable_toggle, &WfToggleRow::toggled,
            this, &WaterfallAnalysisPanel::updateAgcVisibility);
    connect(m_agc_mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WaterfallAnalysisPanel::updateAgcVisibility);

    // -- DESTRIPPING -------------------------------------------------------
    bl->addWidget([] {
        auto* s = new QFrame; s->setFrameShape(QFrame::HLine);
        s->setObjectName("avHRule"); return s; }());

    {
        auto* sub = new QLabel(tr("Destriping"), container);
        sub->setObjectName("wfSubSectionLabel");
        sub->setContentsMargins(kLabelL, 6, kRowR, 2);
        bl->addWidget(sub);
    }

    m_destripe_toggle = new WfToggleRow(tr("Enable Destriping"), false, container);
    m_destripe_toggle->setToolTip(
        tr("Removes horizontal banding caused by ping-to-ping gain variations.\n"
           "Uses robust local medians in independent range zones and only\n"
           "corrects deviations above the selected stripe threshold.\n"
           "Suggested start: Window 50 pings, 4 zones, 1 dB threshold."));
    bl->addWidget(m_destripe_toggle);
    m_destripe_body = new QWidget(container);
    m_destripe_body->setVisible(false);
    {
        auto* ds_bl = makeCompactLayout<QVBoxLayout>(m_destripe_body);
        addValueRow(ds_bl, tr("Window"),
                    m_destripe_window, 10, 500, 50, 5, 0, tr(" pings"));
        m_destripe_window->setToolTip(
            tr("Along-track window used to estimate stripe correction.\n"
               "Start at 50 pings. Larger values are smoother; smaller values react faster but may affect real features."));
        addValueRow(ds_bl, tr("Subdivision"),
                    m_destripe_subdiv, 1, 16, 4, 1, 0);
        m_destripe_subdiv->setToolTip(
            tr("Splits each channel into range zones before correcting stripes.\n"
               "Start at 4. Increase when banding changes strongly from near range to far range."));
        addValueRow(ds_bl, tr("Capping"),
                    m_destripe_cap, 1.0, 5.0, 2.0, 0.1, 1);
        m_destripe_cap->setToolTip(
            tr("Limits how strong the destripe correction can become.\n"
               "Start at 2.0. Lower is safer; higher removes stronger stripes but can alter true seabed texture."));
        addValueRow(ds_bl, tr("Stripe Threshold"), m_destripe_threshold,
                    0.0, 12.0, 1.0, 0.25, 2, tr(" dB"));
        m_destripe_threshold->setToolTip(
            tr("Minimum robust local-level deviation treated as a stripe."));
    }
    bl->addWidget(m_destripe_body);

    connect(m_destripe_toggle, &WfToggleRow::toggled,
            this, &WaterfallAnalysisPanel::updateDestripeVisibility);

    // -- Slant Range Correction --------------------------------------------
    bl->addWidget([] {
        auto* s = new QFrame; s->setFrameShape(QFrame::HLine);
        s->setObjectName("avHRule"); return s; }());

    m_src_toggle = new WfToggleRow(tr("Slant Range Correction"), false, container);
    m_src_toggle->setToolTip(
        tr("Remap slant range to ground range using seabed altitude.\n"
           "Requires seabed tracking data.\n"
           "Required with beam-pattern correction so displayed across-track distance represents seabed ground range."));
    bl->addWidget(m_src_toggle);

    // -- Dirty indicator connections --------------------------------------
    {
        auto refresh = [this](auto) { refreshImageDirty(); };
        auto refreshB = [this](bool) { refreshImageDirty(); };
        connect(m_tvg_toggle,        &WfToggleRow::toggled,      this, refreshB);
        connect(m_arn_toggle,        &WfToggleRow::toggled,      this, refreshB);
        connect(m_agc_enable_toggle, &WfToggleRow::toggled,      this, refreshB);
        connect(m_destripe_toggle,   &WfToggleRow::toggled,      this, refreshB);
        connect(m_src_toggle, &WfToggleRow::toggled, this, [this](bool on) {
            if (!on && m_bpn_toggle && m_bpn_toggle->isChecked()) {
                // SRC is being disabled while BPN is active — block and warn.
                QSignalBlocker sb(m_src_toggle);
                m_src_toggle->setChecked(true);
                QMessageBox::warning(this,
                    tr("Slant Range Correction Required"),
                    tr("<b>Slant Range Correction cannot be disabled while "
                       "Beam Pattern Normalisation is active.</b>"
                       "<p>Beam Pattern Normalisation works column-by-column across "
                       "the swath. For the correction to represent the transducer's "
                       "true across-track sensitivity pattern, its corrected amplitudes "
                       "must be displayed using ground-range geometry.</p>"
                       "<p>Without Slant Range Correction, each column contains "
                       "samples at different grazing angles, causing the normalisation "
                       "to overcorrect near-nadir and undercorrect far-range — "
                       "producing an image that looks worse than no correction at all.</p>"
                       "<p>To disable Slant Range Correction, first turn off "
                       "Beam Pattern Normalisation in the Processing Tools section.</p>"));
                return;
            }
            // SRC has its own immediate repipe path; the remaining processing
            // controls stay as a draft until the operator presses Apply.
            refreshImageDirty();
        });
        connect(m_tvg_spreading,     &WfValueRow::valueChanged,  this, refresh);
        connect(m_tvg_absorption,    &WfValueRow::valueChanged,  this, refresh);
        connect(m_arn_strength,      &WfValueRow::valueChanged,  this, refresh);
        connect(m_arn_gain_cap,      &WfValueRow::valueChanged,  this, refresh);
        connect(m_arn_smooth,        &WfValueRow::valueChanged,  this, refresh);
        connect(m_agc_strength,      &WfValueRow::valueChanged,  this, refresh);
        connect(m_agc_along_track,   &WfValueRow::valueChanged,  this, refresh);
        connect(m_agc_smooth_win,    &WfValueRow::valueChanged,  this, refresh);
        connect(m_agc_edge_skip,     &WfValueRow::valueChanged,  this, refresh);
        connect(m_agc_noise_floor,   &WfValueRow::valueChanged,  this, refresh);
        connect(m_agc_gain_cap,      &WfValueRow::valueChanged,  this, refresh);
        connect(m_destripe_window,   &WfValueRow::valueChanged,  this, refresh);
        connect(m_destripe_subdiv,   &WfValueRow::valueChanged,  this, refresh);
        connect(m_destripe_cap,      &WfValueRow::valueChanged,  this, refresh);
        connect(m_destripe_threshold,&WfValueRow::valueChanged,  this, refresh);
    }

}

} // namespace dolphin::ui
