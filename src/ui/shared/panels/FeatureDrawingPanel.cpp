// FeatureDrawingPanel.cpp — reusable "Feature Drawing" tool row (Polygon/Line/Pen).
#include "ui/shared/panels/FeatureDrawingPanel.h"
#include "ui/shell/Theme.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QSignalBlocker>
#include <QToolButton>

namespace dolphin::ui {

FeatureDrawingPanel::FeatureDrawingPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2,
                            Theme::kSpacing3, Theme::kSpacing2);
    row->setSpacing(Theme::kSpacing2);

    auto makeTool = [this](const char* icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(Theme::icon(icon));
        b->setIconSize(QSize(Theme::kIconToolBar, Theme::kIconToolBar));
        b->setCheckable(true);
        b->setObjectName("wfToolBtn");
        b->setToolTip(tip);
        b->setFixedHeight(Theme::kMediumBtnSz);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return b;
    };

    m_poly = makeTool(":/icons/draw_polygon.svg",
        tr("Polygon — click to add vertices, double-click or Enter to close the area."));
    m_line = makeTool(":/icons/draw_line.svg",
        tr("Line — click to add vertices, double-click or Enter to finish."));
    m_pen  = makeTool(":/icons/draw_pen.svg",
        tr("Pen — press and drag to draw a freehand line; release to finish."));

    row->addWidget(m_poly);
    row->addWidget(m_line);
    row->addWidget(m_pen);

    // Manual exclusivity: activating one clears the others; clicking the active one
    // again turns drawing off. (QButtonGroup exclusive can't un-check by click.)
    auto wire = [this](QToolButton* btn, int kind) {
        connect(btn, &QToolButton::toggled, this, [this, btn, kind](bool on) {
            if (on) {
                for (QToolButton* b : { m_poly, m_line, m_pen }) {
                    if (b == btn) continue;
                    QSignalBlocker sb(b);
                    b->setChecked(false);
                }
                emit toolChanged(kind);
            } else if (!m_poly->isChecked() && !m_line->isChecked() && !m_pen->isChecked()) {
                emit toolChanged(0);
            }
        });
    };
    wire(m_poly, 1);
    wire(m_line, 2);
    wire(m_pen,  3);
}

void FeatureDrawingPanel::setActiveTool(int tool)
{
    QToolButton* btns[] = { m_poly, m_line, m_pen };
    for (int i = 0; i < 3; ++i) {
        if (!btns[i]) continue;
        QSignalBlocker sb(btns[i]);
        btns[i]->setChecked(tool == i + 1);
    }
}

} // namespace dolphin::ui
