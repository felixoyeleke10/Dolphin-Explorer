#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "ui/features/waterfall/components/WfValueRow.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace dolphin::ui {

HeadingInfoPanel::HeadingInfoPanel(QWidget* parent) : QWidget(parent)
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

    // -- Attitude offset rows --------------------------------------------------
    m_hdg_offset = new WfValueRow(tr("Heading offset"), -360.0, 360.0, 0.0, 0.5, 1, " °", container);
    m_hdg_offset->setToolTip(
        tr("Adds a constant offset to every ping's heading value.\n"
           "Use to correct systematic compass bias or mounting misalignment.\n"
           "Positive values rotate clockwise; negative counter-clockwise.\n"
           "Range: ±360 °, step 0.5 °."));

    m_pitch_offset = new WfValueRow(tr("Pitch offset"), -360.0, 360.0, 0.0, 0.5, 1, " °", container);
    m_pitch_offset->setToolTip(
        tr("Adds a constant offset to every ping's pitch angle.\n"
           "Corrects systematic bow-up or bow-down bias from sensor mounting.\n"
           "Positive = bow-up (nose high); negative = bow-down.\n"
           "Range: ±360 °, step 0.5 °."));

    m_roll_offset = new WfValueRow(tr("Roll offset"), -360.0, 360.0, 0.0, 0.5, 1, " °", container);
    m_roll_offset->setToolTip(
        tr("Adds a constant offset to every ping's roll angle.\n"
           "Corrects systematic port or starboard lean from sensor mounting.\n"
           "Positive = starboard-down; negative = port-down.\n"
           "Range: ±360 °, step 0.5 °."));

    vl->addWidget(m_hdg_offset);
    vl->addWidget(m_pitch_offset);
    vl->addWidget(m_roll_offset);
    vl->addStretch(1);
    // Apply is the single shared bar at the bottom of the right-panel (see
    // MainWindow); this section only edits values and contributes via writeInto().
}

void HeadingInfoPanel::writeInto(NavProcessingParams& p) const
{
    p.heading_offset_deg = static_cast<float>(m_hdg_offset->value());
    p.pitch_offset_deg   = static_cast<float>(m_pitch_offset->value());
    p.roll_offset_deg    = static_cast<float>(m_roll_offset->value());
}

NavProcessingParams HeadingInfoPanel::currentParams() const
{
    NavProcessingParams p;
    writeInto(p);
    return p;
}

} // namespace dolphin::ui
