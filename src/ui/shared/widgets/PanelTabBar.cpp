#include "ui/shared/widgets/PanelTabBar.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QToolButton>

namespace dolphin::ui {

PanelTabBar::PanelTabBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("panelTabBar"));
    setFixedHeight(Theme::kPanelHdrH);

    m_layout = makeCompactLayout<QHBoxLayout>(this);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idClicked, this, &PanelTabBar::tabChanged);
}

QToolButton* PanelTabBar::addTab(const QString& label, int id)
{
    if (m_has_tabs) {
        auto* sep = new QFrame(this);
        sep->setObjectName(QStringLiteral("panelTabSep"));
        m_layout->addWidget(sep);
    }

    auto* btn = new QToolButton(this);
    btn->setText(label);
    btn->setCheckable(true);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    btn->setObjectName(QStringLiteral("panelTab"));
    m_layout->addWidget(btn);
    m_group->addButton(btn, id);
    m_has_tabs = true;
    return btn;
}

QToolButton* PanelTabBar::button(int id) const
{
    return qobject_cast<QToolButton*>(m_group->button(id));
}

void PanelTabBar::setCurrentId(int id)
{
    if (auto* btn = m_group->button(id))
        btn->setChecked(true);
}

int PanelTabBar::currentId() const
{
    return m_group->checkedId();
}

} // namespace dolphin::ui
