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
        auto refresh  = [this](auto) { refreshProcessingDirty(); };
        auto refreshB = [this](bool) { updateProcessingVisibility(); refreshProcessingDirty(); };

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


} // namespace dolphin::ui
