// ContactPickingPanel.cpp — reusable "Contact Picking" tool-section content.
#include "ui/shared/panels/ContactPickingPanel.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

ContactPickingPanel::ContactPickingPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2,
                           Theme::kSpacing3, Theme::kSpacing2);
    vl->setSpacing(Theme::kSpacing2);

    // -- Pick toggle -------------------------------------------------------
    m_pick_btn = new QToolButton(this);
    m_pick_btn->setText(tr("Pick Contact"));
    m_pick_btn->setCheckable(true);
    m_pick_btn->setObjectName("wfToolBtn");
    m_pick_btn->setToolTip(
        tr("Toggle contact picking.\n"
           "When active, click the view to place a point contact with the selected "
           "classification.\nStays active until you switch tools."));
    m_pick_btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pick_btn->setFixedHeight(Theme::kMediumBtnSz);
    vl->addWidget(m_pick_btn);
    connect(m_pick_btn, &QToolButton::toggled, this, &ContactPickingPanel::pickToggled);

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
            tr("Boulder"), tr("Debris"), tr("Cable"),
            tr("Pipeline"), tr("Anomaly"), tr("Unknown"),
        });
        m_class_combo->setCurrentText(tr("Unknown"));
        m_class_combo->setObjectName("wfCombo");
        m_class_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_class_combo->setToolTip(tr("Classification assigned to the next contact pick."));

        rl->addWidget(lbl);
        rl->addWidget(m_class_combo);
        vl->addWidget(row);

        connect(m_class_combo, &QComboBox::currentTextChanged,
                this, &ContactPickingPanel::classificationChanged);
    }

    // -- Clear -------------------------------------------------------------
    {
        auto* clear_btn = new QToolButton(this);
        clear_btn->setText(tr("Clear All Contacts"));
        clear_btn->setObjectName("wfToolBtn");
        clear_btn->setToolTip(tr("Remove all contact picks from the current project."));
        clear_btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        clear_btn->setFixedHeight(Theme::kFormBtnH);
        vl->addWidget(clear_btn);
        connect(clear_btn, &QToolButton::clicked, this, &ContactPickingPanel::clearRequested);
    }
}

void ContactPickingPanel::setPickActive(bool active)
{
    if (!m_pick_btn) return;
    QSignalBlocker sb(m_pick_btn);
    m_pick_btn->setChecked(active);
}

QString ContactPickingPanel::classification() const
{
    return m_class_combo ? m_class_combo->currentText() : QString{};
}

} // namespace dolphin::ui
