#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "ui/features/waterfall/components/WfValueRow.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace dolphin::ui {

NavInfoPanel::NavInfoPanel(QWidget* parent) : QWidget(parent)
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

    // ── GPS Smoothing ─────────────────────────────────────────────────────────
    m_smooth_en = new QCheckBox(tr("GPS Smooth"), container);
    m_smooth_en->setObjectName("ctrlToggle");
    m_smooth_en->setToolTip(
        tr("Applies a running-average window to the GPS navigation track.\n"
           "Reduces ping-to-ping position noise and GPS jitter.\n"
           "Best used when the raw track shows visible oscillation.\n\n"
           "Execution order: Layback is applied before smoothing so the\n"
           "smoother works on geometrically correct positions."));
    vl->addWidget(m_smooth_en);

    auto* smooth_rows = new QWidget(container);
    auto* smooth_l = new QVBoxLayout(smooth_rows);
    smooth_l->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    smooth_l->setSpacing(3);
    m_smooth_win = new WfValueRow(tr("Window"), 2.0, 100.0, 5.0, 1.0, 0, " pings", smooth_rows);
    m_smooth_win->setToolTip(
        tr("Number of pings used in the smoothing window.\n"
           "Start at 5 pings. Increase to 20–50 for very noisy GPS.\n"
           "Keep low around sharp turns to preserve real heading changes."));
    smooth_l->addWidget(m_smooth_win);
    vl->addWidget(smooth_rows);

    // ── Layback ───────────────────────────────────────────────────────────────
    auto* div1 = new QFrame(container);
    div1->setObjectName("ctrlDivider");
    div1->setFixedHeight(Theme::kSepSz);
    vl->addWidget(div1);

    m_layback_en = new QCheckBox(tr("Layback"), container);
    m_layback_en->setObjectName("ctrlToggle");
    m_layback_en->setToolTip(
        tr("Corrects towfish position for cable layback.\n"
           "Offsets the recorded GPS position backwards along the vessel heading\n"
           "by the cable distance to place the towfish at the correct ground position.\n\n"
           "Applied before Nav Smoothing in the processing chain."));
    vl->addWidget(m_layback_en);

    auto* lb_rows = new QWidget(container);
    auto* lb_l = new QVBoxLayout(lb_rows);
    lb_l->setContentsMargins(Theme::kSpacing4, 0, 0, 0);
    lb_l->setSpacing(3);
    m_layback_m = new WfValueRow(tr("Distance"), 0.0, 500.0, 0.0, 1.0, 1, " m", lb_rows);
    m_layback_m->setToolTip(
        tr("Horizontal cable distance from the vessel GPS antenna to the towfish.\n"
           "Use measured tow cable geometry when available.\n"
           "Start at 0 m if the fish position is already stored correctly in the data."));
    lb_l->addWidget(m_layback_m);
    vl->addWidget(lb_rows);

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
        tr("Run the selected navigation corrections on the current waterfall line only.\n"
           "Check the result here before applying to all lines."));

    auto* apply_all = new QPushButton(tr("Apply to All"), btn_row);
    apply_all->setObjectName("ctrlApplyBtnSecondary");
    apply_all->setToolTip(
        tr("Run the selected navigation corrections on every line in the project.\n"
           "Use only after confirming the result looks correct on this line."));

    bl->addWidget(apply_line);
    bl->addWidget(apply_all);
    fl->addWidget(btn_row);

    connect(m_smooth_en,  &QCheckBox::toggled, this, &NavInfoPanel::updateControlStates);
    connect(m_layback_en, &QCheckBox::toggled, this, &NavInfoPanel::updateControlStates);
    connect(apply_line, &QPushButton::clicked, this, &NavInfoPanel::onApplyLine);
    connect(apply_all,  &QPushButton::clicked, this, &NavInfoPanel::onApplyAll);

    updateControlStates();
}

NavProcessingParams NavInfoPanel::currentParams() const
{
    NavProcessingParams p;
    p.smooth_enabled  = m_smooth_en->isChecked();
    p.smooth_window   = m_smooth_win->intValue();
    p.layback_enabled = m_layback_en->isChecked();
    p.layback_m       = static_cast<float>(m_layback_m->value());
    return p;
}

void NavInfoPanel::onApplyLine() { emit applyToLineRequested(currentParams()); }
void NavInfoPanel::onApplyAll()  { emit applyToAllRequested(currentParams()); }

void NavInfoPanel::updateControlStates()
{
    m_smooth_win->setEnabled(m_smooth_en->isChecked());
    m_layback_m->setEnabled(m_layback_en->isChecked());
}

} // namespace dolphin::ui
