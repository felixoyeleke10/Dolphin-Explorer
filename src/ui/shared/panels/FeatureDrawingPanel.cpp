// FeatureDrawingPanel.cpp — reusable "Feature Drawing" tool-section content.
#include "ui/shared/panels/FeatureDrawingPanel.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

FeatureDrawingPanel::FeatureDrawingPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2,
                           Theme::kSpacing3, Theme::kSpacing2);
    vl->setSpacing(Theme::kSpacing2);

    // -- Draw toggle -------------------------------------------------------
    m_draw_btn = new QToolButton(this);
    m_draw_btn->setText(tr("Draw Feature"));
    m_draw_btn->setCheckable(true);
    m_draw_btn->setObjectName("wfToolBtn");
    m_draw_btn->setToolTip(
        tr("Toggle feature drawing.\n"
           "Click the view to add vertices; double-click or Enter to finish, "
           "Esc or right-click to cancel.\nPolygon encloses an area; Line traces a path."));
    m_draw_btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_draw_btn->setFixedHeight(Theme::kMediumBtnSz);
    vl->addWidget(m_draw_btn);
    connect(m_draw_btn, &QToolButton::toggled, this, [this](bool) { emitTool(); });

    // -- Type --------------------------------------------------------------
    {
        auto* row = new QWidget(this);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);

        auto* lbl = new QLabel(tr("Type"), row);
        lbl->setObjectName("wfSliderLabel");

        m_type_combo = new QComboBox(row);
        m_type_combo->addItems({ tr("Polygon"), tr("Line") });
        m_type_combo->setObjectName("wfCombo");
        m_type_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_type_combo->setToolTip(tr("Polygon (closed area) or Line (open path)."));

        rl->addWidget(lbl);
        rl->addWidget(m_type_combo);
        vl->addWidget(row);

        connect(m_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { emitTool(); });
    }

    // -- Classification ----------------------------------------------------
    {
        auto* row = new QWidget(this);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);

        auto* lbl = new QLabel(tr("Classification"), row);
        lbl->setObjectName("wfSliderLabel");

        m_class_combo = new QComboBox(row);
        m_class_combo->addItems({
            tr("Unclassified"), tr("Debris Field"), tr("Survey Zone"),
            tr("Exclusion Zone"), tr("Cable Corridor"), tr("Pipeline"),
            tr("Boundary"), tr("Sand Waves"),
        });
        m_class_combo->setObjectName("wfCombo");
        m_class_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_class_combo->setToolTip(tr("Classification assigned to the next drawn feature."));

        rl->addWidget(lbl);
        rl->addWidget(m_class_combo);
        vl->addWidget(row);

        connect(m_class_combo, &QComboBox::currentTextChanged,
                this, &FeatureDrawingPanel::classificationChanged);
    }
}

void FeatureDrawingPanel::emitTool()
{
    const bool on   = m_draw_btn && m_draw_btn->isChecked();
    const int  type = (m_type_combo && m_type_combo->currentIndex() == 1) ? 2 : 1;
    emit toolChanged(on ? type : 0);
}

void FeatureDrawingPanel::setDrawActive(bool active)
{
    if (!m_draw_btn) return;
    QSignalBlocker sb(m_draw_btn);
    m_draw_btn->setChecked(active);
}

QString FeatureDrawingPanel::classification() const
{
    if (!m_class_combo) return {};
    const QString t = m_class_combo->currentText();
    return (t == tr("Unclassified")) ? QString{} : t;
}

} // namespace dolphin::ui
