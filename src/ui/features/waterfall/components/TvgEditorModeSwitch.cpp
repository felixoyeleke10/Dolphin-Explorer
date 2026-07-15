#include "ui/features/waterfall/components/TvgEditorModeSwitch.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QSettings>
#include <QToolButton>

namespace dolphin::ui {

TvgEditorModeSwitch::TvgEditorModeSwitch(QWidget* parent) : QWidget(parent)
{
    setFixedSize(58, 22);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);

    m_graph = new QToolButton(this);
    m_graph->setText(QStringLiteral("⌁"));
    m_graph->setToolTip(tr("Graph TVG editor"));
    m_numeric = new QToolButton(this);
    m_numeric->setText(QStringLiteral("123"));
    m_numeric->setToolTip(tr("Numeric TVG editor"));
    for (auto* button : {m_graph, m_numeric}) {
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setFixedSize(28, 22);
        button->setObjectName("tvgModeButton");
        layout->addWidget(button);
    }

    setStyleSheet(QStringLiteral(
        "QToolButton#tvgModeButton { border: 1px solid rgba(255,255,255,0.12);"
        " background: rgba(255,255,255,0.035); border-radius: 3px;"
        " color: #8e8e93; font-size: 9px; padding: 0; }"
        "QToolButton#tvgModeButton:hover { background: rgba(255,255,255,0.08); color: #e5e5ea; }"
        "QToolButton#tvgModeButton:checked { background: rgba(10,132,255,0.24);"
        " border-color: rgba(10,132,255,0.65); color: #5abaff; }"));

    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    group->addButton(m_graph, Graph);
    group->addButton(m_numeric, Numeric);
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        setMode(static_cast<Mode>(id));
        QSettings().setValue(QStringLiteral("waterfall/tvgEditorMode"), id);
        emit modeChanged(m_mode);
    });

    setMode(static_cast<Mode>(qBound(0,
        QSettings().value(QStringLiteral("waterfall/tvgEditorMode"), 0).toInt(), 1)));
}

void TvgEditorModeSwitch::setMode(Mode mode)
{
    m_mode = mode;
    m_graph->setChecked(mode == Graph);
    m_numeric->setChecked(mode == Numeric);
}

} // namespace dolphin::ui
