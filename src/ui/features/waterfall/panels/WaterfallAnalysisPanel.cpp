// WaterfallAnalysisPanel.cpp — constructor, public API, shared helpers

#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/features/waterfall/components/TvgCurveEditor.h"
#include "ui/shared/UiUtils.h"
#include "ui/features/waterfall/components/WfToggleRow.h"
#include "ui/features/waterfall/components/WfValueRow.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Construction
// -----------------------------------------------------------------------------

WaterfallAnalysisPanel::WaterfallAnalysisPanel(QWidget* parent)
    : QFrame(parent)
{
    auto* fl = makeCompactLayout<QVBoxLayout>(this);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("av_panel_scroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    fl->addWidget(scroll, 1);

    auto* container = new QWidget;
    auto* vl        = makeCompactLayout<QVBoxLayout>(container);
    scroll->setWidget(container);

    buildImageSection      (vl, container);
    buildSeabedSection     (vl, container);
    buildContactSection    (vl, container);
    buildProcessingSection (vl, container);
    vl->addStretch();

    // -- APPLY buttons (pinned below scroll) -------------------------------
    {
        auto* sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setObjectName("avHRule");
        fl->addWidget(sep);

        auto* hdr = new QFrame(this);
        hdr->setObjectName("wfSectionHdr");
        auto* hl  = new QHBoxLayout(hdr);
        hl->setContentsMargins(Theme::kSpacing4, 0, Theme::kSpacing4, 0);
        auto* t = new QLabel(tr("Apply"), hdr);
        t->setObjectName("wfSectionTitle");
        hl->addWidget(t);
        fl->addWidget(hdr);

        auto* col = new QWidget(this);
        auto* cl  = new QVBoxLayout(col);
        cl->setContentsMargins(10, Theme::kSpacing3, 10, 10);
        cl->setSpacing(Theme::kSpacing2);

        auto makeBtn = [&](const QString& text) {
            auto* b = new QToolButton(this);
            b->setText(text);
            b->setObjectName("wfApplyBtn");
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setFixedHeight(Theme::kDialogBtnH);
            return b;
        };

        auto* btn_line = makeBtn(tr("Apply to This Line"));
        btn_line->setToolTip(
            tr("Apply the current image-processing and processing-tool settings to the loaded waterfall line only.\n"
               "Use this for testing values before applying them across the survey."));
        auto* btn_all  = makeBtn(tr("Apply to All Lines"));
        btn_all->setToolTip(
            tr("Apply the current image-processing and processing-tool settings to every line/layer that supports them.\n"
               "Use after checking the result on this line first."));

        connect(btn_line, &QToolButton::clicked,
                this, &WaterfallAnalysisPanel::applyToLineRequested);
        connect(btn_all,  &QToolButton::clicked,
                this, &WaterfallAnalysisPanel::applyToAllLinesRequested);

        cl->addWidget(btn_line);
        cl->addWidget(btn_all);
        fl->addWidget(col);
    }
}

// -----------------------------------------------------------------------------
//  Public API
// -----------------------------------------------------------------------------

WaterfallParams WaterfallAnalysisPanel::currentParams(int palette_index) const
{
    WaterfallParams p;
    p.palette = palette_index;

    // TVG
    p.tvg.enabled    = m_tvg_toggle->isChecked();
    p.tvg.spreading  = static_cast<float>(m_tvg_spreading->value());
    p.tvg.absorption = static_cast<float>(m_tvg_absorption->value());

    // ARN
    p.arn.enabled     = m_arn_toggle->isChecked();
    p.arn.strength    = static_cast<float>(m_arn_strength->value()) / 100.f;
    p.arn.gain_cap_db = static_cast<float>(m_arn_gain_cap->value());
    p.arn.column_smooth = m_arn_smooth->intValue();

    // AGC
    p.agc.enabled           = m_agc_enable_toggle->isChecked();
    p.agc.mode              = (m_agc_mode_combo->currentIndex() == 1)
                            ? AgcMode::Variable : AgcMode::Global;
    p.agc.strength          = static_cast<float>(m_agc_strength->value()) / 100.f;
    p.agc.along_track_win   = m_agc_along_track->intValue();
    p.agc.smoothing_type    = (m_agc_smooth_type_combo->currentIndex() == 1)
                            ? AgcSmoothingType::Median : AgcSmoothingType::Mean;
    p.agc.smoothing_win     = m_agc_smooth_win->intValue();
    p.agc.edge_skip_samples = m_agc_edge_skip->intValue();
    p.agc.noise_floor_pct   = static_cast<float>(m_agc_noise_floor->value());
    p.agc.gain_cap_db       = static_cast<float>(m_agc_gain_cap->value());

    // Destripe
    p.destripe.enabled     = m_destripe_toggle->isChecked();
    p.destripe.window      = m_destripe_window->intValue();
    p.destripe.subdivision = m_destripe_subdiv->intValue();
    p.destripe.capping     = static_cast<float>(m_destripe_cap->value());
    p.destripe.threshold_db = static_cast<float>(m_destripe_threshold->value());

    p.slant_range_correction = m_src_toggle->isChecked();

    // Beam Pattern Normalisation
    p.beam_pattern.enabled       = m_bpn_toggle && m_bpn_toggle->isChecked();
    p.beam_pattern.strength      = m_bpn_strength ? static_cast<float>(m_bpn_strength->value()) : 1.f;
    p.beam_pattern.smooth_radius = m_bpn_smooth   ? m_bpn_smooth->intValue() : 10;
    p.beam_pattern.gain_cap_db   = m_bpn_gain_cap ? static_cast<float>(m_bpn_gain_cap->value()) : 12.f;

    // Angle Range Correction
    p.arc.enabled     = m_arc_toggle && m_arc_toggle->isChecked();
    p.arc.exponent    = m_arc_exponent  ? static_cast<float>(m_arc_exponent->value())  : 1.5f;
    p.arc.gain_cap_db = m_arc_gain_cap  ? static_cast<float>(m_arc_gain_cap->value())  : 12.f;

    // Adaptive Contrast Enhancement
    p.ml_enhance.enabled    = m_mle_toggle && m_mle_toggle->isChecked();
    p.ml_enhance.tile_pings = m_mle_tile_pings  ? m_mle_tile_pings->intValue()  : 64;
    p.ml_enhance.tile_samps = m_mle_tile_samps  ? m_mle_tile_samps->intValue()  : 128;
    p.ml_enhance.clip_limit = m_mle_clip_limit  ? static_cast<float>(m_mle_clip_limit->value()) : 2.f;

    return p;
}

void WaterfallAnalysisPanel::setParams(const WaterfallParams& p)
{
    // TVG
    { QSignalBlocker blocker(m_tvg_toggle);
      m_tvg_toggle->setChecked(p.tvg.enabled); }
    m_tvg_spreading ->setValue(p.tvg.spreading);
    m_tvg_absorption->setValue(p.tvg.absorption);
    if (m_tvg_curve)
        m_tvg_curve->setCoefficients(p.tvg.spreading, p.tvg.absorption);

    // ARN — block signal to prevent the SRC advisory QMessageBox firing during
    // programmatic restore (setLayer); the user already chose these params.
    { QSignalBlocker sb(m_arn_toggle);
      m_arn_toggle->setChecked(p.arn.enabled); }
    m_arn_strength ->setValue(p.arn.strength    * 100.0);
    m_arn_gain_cap ->setValue(p.arn.gain_cap_db);
    m_arn_smooth   ->setValue(p.arn.column_smooth);

    // AGC
    { QSignalBlocker bm(m_agc_mode_combo), bst(m_agc_smooth_type_combo);
      QSignalBlocker enable_blocker(m_agc_enable_toggle);
      m_agc_enable_toggle->setChecked(p.agc.enabled);
      m_agc_mode_combo->setCurrentIndex(p.agc.mode == AgcMode::Variable ? 1 : 0);
      m_agc_smooth_type_combo->setCurrentIndex(
          p.agc.smoothing_type == AgcSmoothingType::Median ? 1 : 0); }
    m_agc_strength   ->setValue(p.agc.strength * 100.0);
    m_agc_along_track->setValue(p.agc.along_track_win);
    m_agc_smooth_win ->setValue(p.agc.smoothing_win);
    m_agc_edge_skip  ->setValue(p.agc.edge_skip_samples);
    m_agc_noise_floor->setValue(p.agc.noise_floor_pct);
    m_agc_gain_cap   ->setValue(p.agc.gain_cap_db);

    // Destripe
    { QSignalBlocker blocker(m_destripe_toggle);
      m_destripe_toggle->setChecked(p.destripe.enabled); }
    m_destripe_window->setValue(p.destripe.window);
    m_destripe_subdiv->setValue(p.destripe.subdivision);
    m_destripe_cap   ->setValue(p.destripe.capping);
    m_destripe_threshold->setValue(p.destripe.threshold_db);

    // SRC — block signal to prevent the BPN guard dialog firing during
    // programmatic restore; the user already set these params deliberately.
    { QSignalBlocker sb(m_src_toggle);
      m_src_toggle->setChecked(p.slant_range_correction); }

    // Beam Pattern Normalisation
    if (m_bpn_toggle) {
        QSignalBlocker blocker(m_bpn_toggle);
        m_bpn_toggle->setChecked(p.beam_pattern.enabled);
    }
    if (m_bpn_strength)  m_bpn_strength->setValue(p.beam_pattern.strength);
    if (m_bpn_smooth)    m_bpn_smooth->setValue(p.beam_pattern.smooth_radius);
    if (m_bpn_gain_cap)  m_bpn_gain_cap->setValue(p.beam_pattern.gain_cap_db);

    // Angle Range Correction
    if (m_arc_toggle) {
        QSignalBlocker blocker(m_arc_toggle);
        m_arc_toggle->setChecked(p.arc.enabled);
    }
    if (m_arc_exponent)  m_arc_exponent->setValue(p.arc.exponent);
    if (m_arc_gain_cap)  m_arc_gain_cap->setValue(p.arc.gain_cap_db);

    // Adaptive Contrast Enhancement
    if (m_mle_toggle) {
        QSignalBlocker blocker(m_mle_toggle);
        m_mle_toggle->setChecked(p.ml_enhance.enabled);
    }
    if (m_mle_tile_pings)  m_mle_tile_pings->setValue(p.ml_enhance.tile_pings);
    if (m_mle_tile_samps)  m_mle_tile_samps->setValue(p.ml_enhance.tile_samps);
    if (m_mle_clip_limit)  m_mle_clip_limit->setValue(p.ml_enhance.clip_limit);

    updateRangeCompVisibility();
    updateAgcVisibility();
    updateDestripeVisibility();
    updateProcessingVisibility();
    refreshImageDirty();
    refreshSeabedDirty();
    refreshProcessingDirty();
}

void WaterfallAnalysisPanel::setContactPickActive(bool active)
{
    if (!m_contact_pick_btn) return;
    QSignalBlocker sb(m_contact_pick_btn);
    m_contact_pick_btn->setChecked(active);
}

int WaterfallAnalysisPanel::currentSeabedChannel() const
{
    return m_seabed_channel_combo ? m_seabed_channel_combo->currentIndex() : 0;
}

QString WaterfallAnalysisPanel::currentContactClassText() const
{
    return m_contact_class_combo ? m_contact_class_combo->currentText() : tr("Unknown");
}

SeabedAutoParams WaterfallAnalysisPanel::currentSeabedAutoParams() const
{
    SeabedAutoParams p;
    switch (m_seabed_method_combo->currentIndex()) {
    case 1:  p.method = SeabedMethod::FirstReturn;      break;
    default: p.method = SeabedMethod::Threshold;         break;
    }
    p.blanking_pct    = static_cast<float>(m_seabed_blank->value());
    p.threshold_pct   = static_cast<float>(m_seabed_thresh->value());
    p.min_snr         = static_cast<float>(m_seabed_snr->value());
    p.max_delta_m     = static_cast<float>(m_seabed_outlier->value());
    p.channel_agree_m = static_cast<float>(m_seabed_ch_agree->value());
    p.smoothing       = static_cast<float>(m_seabed_smooth->value());
    return p;
}

// -----------------------------------------------------------------------------
//  Private helpers
// -----------------------------------------------------------------------------

void WaterfallAnalysisPanel::updateRangeCompVisibility()
{
    if (m_tvg_body) m_tvg_body->setVisible(m_tvg_toggle->isChecked());
    if (m_arn_body) m_arn_body->setVisible(m_arn_toggle->isChecked());
}

void WaterfallAnalysisPanel::updateAgcVisibility()
{
    const bool on  = m_agc_enable_toggle && m_agc_enable_toggle->isChecked();
    const bool var = m_agc_mode_combo    && m_agc_mode_combo->currentIndex() == 1;
    if (m_agc_body)            m_agc_body->setVisible(on);
    if (m_agc_along_track_row) m_agc_along_track_row->setVisible(var);
}

void WaterfallAnalysisPanel::updateDestripeVisibility()
{
    if (m_destripe_body) m_destripe_body->setVisible(m_destripe_toggle->isChecked());
}

// static
QVBoxLayout* WaterfallAnalysisPanel::makeSection(const QString& title,
                                                  bool expanded,
                                                  QWidget* parent,
                                                  QVBoxLayout* parent_layout,
                                                  QPushButton** hdr_out)
{
    auto* hdr = new QPushButton(parent);
    hdr->setCheckable(true);
    hdr->setChecked(expanded);
    hdr->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    hdr->setObjectName("avCollapseHdr");
    hdr->setFixedHeight(Theme::kColorBtnH);
    hdr->setFlat(true);
    hdr->setToolTip(tr("Show or hide the %1 controls.").arg(title));

    if (hdr_out) *hdr_out = hdr;

    auto* body = new QWidget(parent);
    body->setObjectName("avCollapseBody");
    auto* bl = makeCompactLayout<QVBoxLayout>(body);
    body->setVisible(expanded);

    auto syncLabel = [hdr, title](bool checked) {
        hdr->setText(QString(checked ? "\u25bc  " : "\u25b6  ") + title);
    };
    syncLabel(expanded);

    QObject::connect(hdr, &QPushButton::toggled, body, &QWidget::setVisible);
    QObject::connect(hdr, &QPushButton::toggled, [syncLabel](bool c) { syncLabel(c); });

    parent_layout->addWidget(hdr);
    parent_layout->addWidget(body);
    return bl;
}

static void markSectionDirty(QPushButton* hdr, bool dirty)
{
    if (!hdr) return;
    hdr->setProperty("wfDirty", dirty);
    hdr->style()->unpolish(hdr);
    hdr->style()->polish(hdr);
}

void WaterfallAnalysisPanel::refreshImageDirty()
{
    const bool dirty =
        (m_tvg_toggle        && !m_tvg_toggle->isAtDefault())        ||
        (m_arn_toggle        && !m_arn_toggle->isAtDefault())        ||
        (m_agc_enable_toggle && !m_agc_enable_toggle->isAtDefault()) ||
        (m_destripe_toggle   && !m_destripe_toggle->isAtDefault())   ||
        (m_src_toggle        && !m_src_toggle->isAtDefault())        ||
        (m_tvg_spreading     && !m_tvg_spreading->isAtDefault())     ||
        (m_tvg_absorption    && !m_tvg_absorption->isAtDefault())    ||
        (m_arn_strength      && !m_arn_strength->isAtDefault())      ||
        (m_arn_gain_cap      && !m_arn_gain_cap->isAtDefault())      ||
        (m_arn_smooth        && !m_arn_smooth->isAtDefault())        ||
        (m_agc_strength      && !m_agc_strength->isAtDefault())      ||
        (m_agc_along_track   && !m_agc_along_track->isAtDefault())   ||
        (m_agc_smooth_win    && !m_agc_smooth_win->isAtDefault())    ||
        (m_agc_edge_skip     && !m_agc_edge_skip->isAtDefault())     ||
        (m_agc_noise_floor   && !m_agc_noise_floor->isAtDefault())   ||
        (m_destripe_window   && !m_destripe_window->isAtDefault())   ||
        (m_destripe_subdiv   && !m_destripe_subdiv->isAtDefault())   ||
        (m_destripe_cap      && !m_destripe_cap->isAtDefault())      ||
        (m_destripe_threshold && !m_destripe_threshold->isAtDefault());
    markSectionDirty(m_image_hdr, dirty);
}

void WaterfallAnalysisPanel::refreshSeabedDirty()
{
    const bool dirty =
        (m_seabed_blank   && !m_seabed_blank->isAtDefault())   ||
        (m_seabed_thresh  && !m_seabed_thresh->isAtDefault())  ||
        (m_seabed_snr     && !m_seabed_snr->isAtDefault())     ||
        (m_seabed_outlier && !m_seabed_outlier->isAtDefault()) ||
        (m_seabed_smooth  && !m_seabed_smooth->isAtDefault())  ||
        (m_seabed_method_combo  && m_seabed_method_combo->currentIndex()  != 0);
    markSectionDirty(m_seabed_hdr, dirty);
}

void WaterfallAnalysisPanel::addValueRow(QVBoxLayout*   bl,
                                          const QString& lbl,
                                          WfValueRow*&   row_out,
                                          double lo, double hi, double def,
                                          double step, int decimals,
                                          const QString& suffix)
{
    row_out = new WfValueRow(lbl, lo, hi, def, step, decimals, suffix, this);
    bl->addWidget(row_out);
}

} // namespace dolphin::ui
