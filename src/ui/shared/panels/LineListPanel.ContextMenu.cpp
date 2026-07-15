// LineListPanel.ContextMenu.cpp - context-menu dispatch by tree item type.
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/panels/LineListPanel_p.h"

#include <QTreeWidget>

using namespace dolphin::ui::detail;

namespace dolphin::ui {

void LineListPanel::onContextMenuRequested(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item) return;

    const QPoint global_pos = m_tree->viewport()->mapToGlobal(pos);
    switch (itemTypeOf(item)) {
    case ItemType::Project:
        showProjectContextMenu(global_pos);
        break;
    case ItemType::Layer:
        showLayerContextMenu(item, global_pos);
        break;
    case ItemType::LayerGroup:
        showLayerGroupContextMenu(item, global_pos);
        break;
    case ItemType::Source:
        showSourceContextMenu(item, global_pos);
        break;
    case ItemType::Contact:
        showContactContextMenu(item, global_pos);
        break;
    case ItemType::Feature:
        showFeatureContextMenu(item, global_pos);
        break;
    case ItemType::ContactGroup:
        showContactGroupContextMenu(item, global_pos);
        break;
    default:
        break;
    }
}

} // namespace dolphin::ui
