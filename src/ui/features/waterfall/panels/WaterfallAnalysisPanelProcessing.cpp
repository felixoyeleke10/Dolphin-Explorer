// WaterfallAnalysisPanelProcessing.cpp — PROCESSING TOOLS + NAVIGATION PROCESSING sections

#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"
#include "ui/features/waterfall/components/WfToggleRow.h"
#include "ui/features/waterfall/components/WfValueRow.h"

#include <QFrame>
#include <QLabel>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace dolphin::ui {

// Shared geometry — must match WfValueRow and WaterfallAnalysisPanelImage.cpp.
static constexpr int kLabelL = 14;
static constexpr int kRowR   = 10;

static QFrame* makeHRule(QWidget* parent)
{
    auto* s = new QFrame(parent);
    s->setFrameShape(QFrame::HLine);
    s->setObjectName("avHRule");
    return s;
}

static QLabel* makeSubLabel(const QString& text, QWidget* parent, bool first = false)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setObjectName("wfSubSectionLabel");
    lbl->setContentsMargins(kLabelL, first ? 8 : 6, kRowR, 2);
    return lbl;
}

// -----------------------------------------------------------------------------
//  PROCESSING TOOLS visibility + dirty
// -----------------------------------------------------------------------------

void WaterfallAnalysisPanel::updateProcessingVisibility()
{
    if (m_bpn_body) m_bpn_body->setVisible(m_bpn_toggle && m_bpn_toggle->isChecked());
    if (m_arc_body) m_arc_body->setVisible(m_arc_toggle && m_arc_toggle->isChecked());
    if (m_mle_body) m_mle_body->setVisible(m_mle_toggle && m_mle_toggle->isChecked());
}

void WaterfallAnalysisPanel::refreshProcessingDirty()
{
    const bool dirty =
        (m_bpn_toggle     && !m_bpn_toggle->isAtDefault())     ||
        (m_bpn_strength   && !m_bpn_strength->isAtDefault())   ||
        (m_bpn_smooth     && !m_bpn_smooth->isAtDefault())     ||
        (m_arc_toggle     && !m_arc_toggle->isAtDefault())     ||
        (m_arc_exponent   && !m_arc_exponent->isAtDefault())   ||
        (m_arc_gain_cap   && !m_arc_gain_cap->isAtDefault())   ||
        (m_mle_toggle     && !m_mle_toggle->isAtDefault())     ||
        (m_mle_tile_pings && !m_mle_tile_pings->isAtDefault()) ||
        (m_mle_tile_samps && !m_mle_tile_samps->isAtDefault()) ||
        (m_mle_clip_limit && !m_mle_clip_limit->isAtDefault());

    // Re-use the same markSectionDirty static defined in the main .cpp.
    // Access it via QPushButton property directly to avoid a forward declaration.
    if (m_processing_hdr) {
        m_processing_hdr->setProperty("wfDirty", dirty);
        m_processing_hdr->style()->unpolish(m_processing_hdr);
        m_processing_hdr->style()->polish(m_processing_hdr);
    }
}

// -----------------------------------------------------------------------------
//  PROCESSING TOOLS section builder
//
//  Applied via the main "Apply to This Line / All Lines" buttons (same path
//  as Image Processing — these are display-time corrections on assembled rows).
// -----------------------------------------------------------------------------

void WaterfallAnalysisPanel::buildProcessingSection(QVBoxLayout* vl, QWidget* container)
{
    auto* bl = makeSection(tr("Processing Tools"), false, container, vl, &m_processing_hdr);

    // -- ACOUSTIC ENHANCEMENT -------------------------------------------------
    bl->addWidget(makeSubLabel(tr("Acoustic Enhancement"), container, true));

    // Beam Pattern Normalisation
    m_bpn_toggle = new WfToggleRow(tr("Beam Pattern Norm."), false, container);
    m_bpn_toggle->setToolTip(
        tr("Corrects transducer array sensitivity variation across the swath.\n"
           "Applied after SRC (slant range correction) on assembled uint16 rows.\n"
           "Enable Slant Range Correction first for geometrically correct results.\n"
           "Runs before ARN and destripe in the processing chain. Requires Apply."));
    bl->addWidget(m_bpn_toggle);

    m_bpn_body = new QWidget(container);
    m_bpn_body->setVisible(false);
    {
        auto* bpn_bl = makeCompactLayout<QVBoxLayout>(m_bpn_body);
        addValueRow(bpn_bl, tr("Strength"),      m_bpn_strength, 0.0, 1.0, 1.0,  0.05, 2);
        m_bpn_strength->setToolTip(
            tr("Amount of beam-pattern correction applied.\n"
               "Start at 1.00 for full correction. Reduce toward 0.50 if the correction looks too strong."));
        addValueRow(bpn_bl, tr("Smooth Radius"),  m_bpn_smooth,   0,   50,  10,    1,   0, tr(" samp"));
        m_bpn_smooth->setToolTip(
            tr("Smooths the estimated beam pattern across range samples.\n"
               "Start at 10 samples. Increase for cleaner correction; decrease to preserve narrow changes."));
    }
    bl->addWidget(m_bpn_body);

    // Angle Range Correction
    m_arc_toggle = new WfToggleRow(tr("Angle Range Correction"), false, container);
    m_arc_toggle->setToolTip(
        tr("Compensates grazing-angle-dependent backscatter variation.\n"
           "Uses slant range geometry (altitude / slant range = sin θ).\n"
           "Applied to raw 16-bit pings BEFORE assembly and SRC — always\n"
           "operates in slant range regardless of SlantRangeNode pipeline state.\n"
           "Requires a valid seabed bottom pick or non-zero tow depth. Requires Apply."));
    bl->addWidget(m_arc_toggle);

    m_arc_body = new QWidget(container);
    m_arc_body->setVisible(false);
    {
        auto* arc_bl = makeCompactLayout<QVBoxLayout>(m_arc_body);
        addValueRow(arc_bl, tr("Exponent"), m_arc_exponent, 0.5, 4.0, 1.5, 0.1, 1);
        m_arc_exponent->setToolTip(
            tr("Controls how strongly grazing-angle compensation increases at low angles.\n"
               "Start at 1.5. Lower is subtle; higher is aggressive and can over-brighten far range."));
        addValueRow(arc_bl, tr("Gain Cap"), m_arc_gain_cap, 0.0, 40.0, 12.0, 1.0, 0, tr(" dB"));
        m_arc_gain_cap->setToolTip(
            tr("Maximum gain allowed during angle-range correction.\n"
               "Start at 12 dB. Lower prevents noise lift; higher can recover weak far-range returns."));
    }
    bl->addWidget(m_arc_body);

    // -- ADAPTIVE CONTRAST -----------------------------------------------------
    bl->addWidget(makeHRule(container));
    bl->addWidget(makeSubLabel(tr("Adaptive Contrast"), container));

    m_mle_toggle = new WfToggleRow(tr("Adaptive Contrast (CLAHE)"), false, container);
    m_mle_toggle->setToolTip(
        tr("Adaptive local contrast enhancement (CLAHE-like).\n"
           "Divides the image into tiles and independently equalises\n"
           "each tile's histogram, improving both shadow and highlight\n"
           "visibility simultaneously. Requires Apply."));
    bl->addWidget(m_mle_toggle);

    m_mle_body = new QWidget(container);
    m_mle_body->setVisible(false);
    {
        auto* mle_bl = makeCompactLayout<QVBoxLayout>(m_mle_body);
        addValueRow(mle_bl, tr("Tile Pings"),  m_mle_tile_pings,  16, 256, 64,  8,  0, tr(" pings"));
        m_mle_tile_pings->setToolTip(
            tr("Along-track tile height for adaptive contrast.\n"
               "Start at 64 pings. Smaller tiles reveal local detail but can look patchy."));
        addValueRow(mle_bl, tr("Tile Samps"),  m_mle_tile_samps,  16, 512, 128, 8,  0, tr(" samp"));
        m_mle_tile_samps->setToolTip(
            tr("Across-track tile width for adaptive contrast.\n"
               "Start at 128 samples. Smaller tiles enhance fine detail; larger tiles look smoother."));
        addValueRow(mle_bl, tr("Clip Limit"),  m_mle_clip_limit,  1.0, 8.0, 2.0, 0.1, 1);
        m_mle_clip_limit->setToolTip(
            tr("Limits local contrast amplification.\n"
               "Start at 2.0. Increase to reveal more detail; decrease if noise or speckle becomes too strong."));
    }
    bl->addWidget(m_mle_body);

    // -- Connections -----------------------------------------------------------
    {
        auto refresh  = [this](auto)   { refreshProcessingDirty(); };
        auto refreshB = [this](bool)   { updateProcessingVisibility(); refreshProcessingDirty(); };

        // BPN requires SRC — enforce on toggle-on.
        connect(m_bpn_toggle, &WfToggleRow::toggled, this, [this, refreshB](bool on) {
            if (on && m_src_toggle && !m_src_toggle->isChecked()) {
                auto* msg = new QMessageBox(this);
                msg->setWindowTitle(tr("Slant Range Correction Required"));
                msg->setIcon(QMessageBox::Warning);
                msg->setText(tr("<b>Beam Pattern Normalisation requires Slant Range "
                                "Correction to be enabled first.</b>"));
                msg->setInformativeText(
                    tr("Beam Pattern Normalisation corrects the transducer's "
                       "across-track sensitivity variation column by column. For "
                       "this to produce a geometrically correct result, samples must "
                       "first be remapped from <i>slant range</i> (the diagonal path "
                       "the sonar pulse travels through water) to <i>ground range</i> "
                       "(the true horizontal distance on the seabed)."
                       "<br><br>"
                       "Without Slant Range Correction, near-nadir columns span a "
                       "different angular extent than far-range columns. The "
                       "normalisation will overcorrect near-nadir and undercorrect "
                       "far-range, making the image geometrically worse than leaving "
                       "it uncorrected."
                       "<br><br>"
                       "Click <b>Enable SRC</b> to turn on Slant Range Correction "
                       "and continue, or <b>Cancel</b> to leave both off."));
                msg->setTextFormat(Qt::RichText);
                auto* btn_enable = msg->addButton(tr("Enable SRC && Continue"),
                                                  QMessageBox::AcceptRole);
                msg->addButton(QMessageBox::Cancel);
                msg->exec();

                if (msg->clickedButton() == static_cast<QAbstractButton*>(btn_enable)) {
                    // Auto-enable SRC and let the image section know immediately.
                    m_src_toggle->setChecked(true);
                    emit slantRangeCorrectionChanged(true);
                } else {
                    // Revert the BPN toggle silently.
                    QSignalBlocker sb(m_bpn_toggle);
                    m_bpn_toggle->setChecked(false);
                    return;
                }
            }
            refreshB(on);
        });

        connect(m_arc_toggle,     &WfToggleRow::toggled,     this, refreshB);
        connect(m_mle_toggle,     &WfToggleRow::toggled,     this, refreshB);
        connect(m_bpn_strength,   &WfValueRow::valueChanged, this, refresh);
        connect(m_bpn_smooth,     &WfValueRow::valueChanged, this, refresh);
        connect(m_arc_exponent,   &WfValueRow::valueChanged, this, refresh);
        connect(m_arc_gain_cap,   &WfValueRow::valueChanged, this, refresh);
        connect(m_mle_tile_pings, &WfValueRow::valueChanged, this, refresh);
        connect(m_mle_tile_samps, &WfValueRow::valueChanged, this, refresh);
        connect(m_mle_clip_limit, &WfValueRow::valueChanged, this, refresh);
    }
}

// -----------------------------------------------------------------------------
//  NAVIGATION PROCESSING visibility + dirty
// -----------------------------------------------------------------------------

void WaterfallAnalysisPanel::updateNavVisibility()
{
    if (m_nav_smooth_body)  m_nav_smooth_body->setVisible(m_nav_smooth_toggle  && m_nav_smooth_toggle->isChecked());
    if (m_nav_layback_body) m_nav_layback_body->setVisible(m_nav_layback_toggle && m_nav_layback_toggle->isChecked());
}

void WaterfallAnalysisPanel::refreshNavDirty()
{
    const bool dirty =
        (m_nav_smooth_toggle  && !m_nav_smooth_toggle->isAtDefault())  ||
        (m_nav_smooth_window  && !m_nav_smooth_window->isAtDefault())  ||
        (m_nav_layback_toggle && !m_nav_layback_toggle->isAtDefault()) ||
        (m_nav_layback_m      && !m_nav_layback_m->isAtDefault());

    if (m_nav_hdr) {
        m_nav_hdr->setProperty("wfDirty", dirty);
        m_nav_hdr->style()->unpolish(m_nav_hdr);
        m_nav_hdr->style()->polish(m_nav_hdr);
    }
}

// -----------------------------------------------------------------------------
//  NAVIGATION PROCESSING section builder
//
//  These corrections modify the stored raw ping navigation data permanently
//  within the loaded window.  They are NEVER auto-applied — the user must
//  click "Run on This Line" or "Run on All Lines" explicitly.
//
//  NavSmoothNode: running-average smoothing of the GPS track.
//  GeoCorrectNode: layback offset from ship GPS to towfish position.
// -----------------------------------------------------------------------------

void WaterfallAnalysisPanel::buildNavSection(QVBoxLayout* vl, QWidget* container)
{
    auto* bl = makeSection(tr("Navigation Processing"), false, container, vl, &m_nav_hdr);

    // -- NAV SMOOTHING ---------------------------------------------------------
    bl->addWidget(makeSubLabel(tr("Nav Smoothing"), container, true));

    m_nav_smooth_toggle = new WfToggleRow(tr("Enable Nav Smoothing"), false, container);
    m_nav_smooth_toggle->setToolTip(
        tr("Applies a running-average window to the navigation track.\n"
           "Reduces GPS jitter and smooths ping-to-ping position noise.\n\n"
           "Execution order: If Layback Correction is also enabled, layback\n"
           "is applied first so the smoother works on correct positions.\n\n"
           "Uses the NavSmooth pipeline node.\n"
           "Click \"Run\" — never auto-applied on data load."));
    bl->addWidget(m_nav_smooth_toggle);

    m_nav_smooth_body = new QWidget(container);
    m_nav_smooth_body->setVisible(false);
    {
        auto* sb = makeCompactLayout<QVBoxLayout>(m_nav_smooth_body);
        addValueRow(sb, tr("Window"), m_nav_smooth_window, 1, 200, 5, 1, 0, tr(" pings"));
        m_nav_smooth_window->setToolTip(
            tr("Number of pings used in the navigation smoothing window.\n"
               "Start at 5. Increase for noisy GPS; keep low around sharp turns or short manoeuvres."));
    }
    bl->addWidget(m_nav_smooth_body);

    // -- LAYBACK CORRECTION ----------------------------------------------------
    bl->addWidget(makeHRule(container));
    bl->addWidget(makeSubLabel(tr("Layback Correction"), container));

    m_nav_layback_toggle = new WfToggleRow(tr("Apply Layback"), false, container);
    m_nav_layback_toggle->setToolTip(
        tr("Corrects the towfish position for cable layback.\n"
           "Offsets the GPS position backwards along the vessel heading\n"
           "by the specified horizontal distance.\n\n"
           "Execution order: Layback is always applied BEFORE Nav Smoothing.\n"
           "This ensures the smoother works on geometrically correct positions\n"
           "rather than raw GPS offsets that still carry the cable-layback error.\n\n"
           "Uses the GeoCorrect pipeline node.\n"
           "Click \"Run\" — never auto-applied on data load."));
    bl->addWidget(m_nav_layback_toggle);

    m_nav_layback_body = new QWidget(container);
    m_nav_layback_body->setVisible(false);
    {
        auto* lb = makeCompactLayout<QVBoxLayout>(m_nav_layback_body);
        addValueRow(lb, tr("Layback"), m_nav_layback_m, 0.0, 500.0, 0.0, 1.0, 1, tr(" m"));
        m_nav_layback_m->setToolTip(
            tr("Horizontal distance from vessel GPS antenna back to the towfish.\n"
               "Use measured cable/tow geometry when available. Start at 0 m if fish position is already stored."));
    }
    bl->addWidget(m_nav_layback_body);

    // -- Run buttons -----------------------------------------------------------
    bl->addWidget(makeHRule(container));

    {
        auto* col = new QWidget(container);
        auto* cl  = new QVBoxLayout(col);
        cl->setContentsMargins(10, Theme::kSpacing2, 10, Theme::kSpacing3);
        cl->setSpacing(Theme::kSpacing2);

        auto makeBtn = [&](const QString& text) -> QToolButton* {
            auto* b = new QToolButton(container);
            b->setText(text);
            b->setObjectName("wfApplyBtn");
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setFixedHeight(Theme::kDialogBtnH);
            return b;
        };

        auto* btn_line = makeBtn(tr("Run on This Line"));
        btn_line->setToolTip(
            tr("Run selected navigation processing on the currently loaded waterfall line only.\n"
               "Use this to check smoothing or layback values before applying broadly."));
        auto* btn_all  = makeBtn(tr("Run on All Lines"));
        btn_all->setToolTip(
            tr("Run selected navigation processing on all applicable lines.\n"
               "Use only after confirming the values on this line."));

        connect(btn_line, &QToolButton::clicked, this, [this] {
            emit navProcessThisLineRequested(currentNavParams());
        });
        connect(btn_all, &QToolButton::clicked, this, [this] {
            emit navProcessAllLinesRequested(currentNavParams());
        });

        cl->addWidget(btn_line);
        cl->addWidget(btn_all);
        bl->addWidget(col);
    }

    // -- Connections -----------------------------------------------------------
    {
        auto refresh  = [this](auto)   { refreshNavDirty(); };
        auto refreshB = [this](bool)   { updateNavVisibility(); refreshNavDirty(); };
        connect(m_nav_smooth_toggle,  &WfToggleRow::toggled,     this, refreshB);
        connect(m_nav_layback_toggle, &WfToggleRow::toggled,     this, refreshB);
        connect(m_nav_smooth_window,  &WfValueRow::valueChanged, this, refresh);
        connect(m_nav_layback_m,      &WfValueRow::valueChanged, this, refresh);
    }
}

} // namespace dolphin::ui
