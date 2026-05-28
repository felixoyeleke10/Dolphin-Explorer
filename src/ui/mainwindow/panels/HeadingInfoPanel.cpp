#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "ui/features/waterfall/components/WfValueRow.h"
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

    auto* fl = new QVBoxLayout(this);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(0);

    // ── Scrollable content ────────────────────────────────────────────────────
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

    // ── Attitude offset rows ──────────────────────────────────────────────────
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

    // ── Apply buttons (pinned below scroll) ───────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("ctrlDivider");
    fl->addWidget(sep);

    auto* btn_row = new QWidget(this);
    auto* bl = new QHBoxLayout(btn_row);
    bl->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2, Theme::kSpacing3, Theme::kSpacing3);
    bl->setSpacing(Theme::kSpacing1);

    auto* apply_line = new QPushButton(tr("Apply to Line"), btn_row);
    apply_line->setObjectName("ctrlApplyBtn");
    apply_line->setToolTip(
        tr("Apply the attitude offsets to the current waterfall line only.\n"
           "Check the swath geometry result before applying to all lines."));

    auto* apply_all = new QPushButton(tr("Apply to All"), btn_row);
    apply_all->setObjectName("ctrlApplyBtnSecondary");
    apply_all->setToolTip(
        tr("Apply the attitude offsets to every line in the project.\n"
           "Use only after confirming the geometry looks correct on this line."));

    bl->addWidget(apply_line);
    bl->addWidget(apply_all);
    fl->addWidget(btn_row);

    connect(apply_line, &QPushButton::clicked, this, &HeadingInfoPanel::onApplyLine);
    connect(apply_all,  &QPushButton::clicked, this, &HeadingInfoPanel::onApplyAll);
}

NavProcessingParams HeadingInfoPanel::currentParams() const
{
    NavProcessingParams p;
    p.heading_offset_deg = static_cast<float>(m_hdg_offset->value());
    p.pitch_offset_deg   = static_cast<float>(m_pitch_offset->value());
    p.roll_offset_deg    = static_cast<float>(m_roll_offset->value());
    return p;
}

void HeadingInfoPanel::onApplyLine() { emit applyToLineRequested(currentParams()); }
void HeadingInfoPanel::onApplyAll()  { emit applyToAllRequested(currentParams()); }

} // namespace dolphin::ui
