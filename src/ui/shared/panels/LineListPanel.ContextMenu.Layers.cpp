// Layer and layer-group context menus for LineListPanel.
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/panels/LineListPanel.ContextMenu_p.h"
#include "ui/shared/panels/LineListPanel_p.h"

#include "app/layers/LayerUtils.h"
#include "app/project/Project.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTreeWidget>

#include <algorithm>
#include <utility>

using namespace dolphin::ui::detail;

namespace dolphin::ui {

void LineListPanel::showLayerContextMenu(QTreeWidgetItem* item,
                                         const QPoint& global_pos)
{
    const std::string clicked_id = item->data(0, kRoleId).toString().toStdString();

    std::vector<std::string> ids;
    for (QTreeWidgetItem* selected : m_tree->selectedItems()) {
        if (itemTypeOf(selected) == ItemType::Layer)
            ids.push_back(selected->data(0, kRoleId).toString().toStdString());
    }
    if (std::find(ids.begin(), ids.end(), clicked_id) == ids.end())
        ids = {clicked_id};

    const bool multi = ids.size() > 1;
    const int count = static_cast<int>(ids.size());
    auto* group_item = item->parent();
    const int current_position = group_item ? group_item->indexOfChild(item) : -1;
    const int group_size = group_item ? group_item->childCount() : 0;
    const bool can_move_up = current_position > 0;
    const bool can_move_down = current_position >= 0
                               && current_position < group_size - 1;

    QMenu menu(this);

    if (!multi) {
        using Modality = app::Modality;
        const auto modality = static_cast<Modality>(item->data(0, kRoleModality).toInt());
        if (modality == Modality::SubBottom) {
            menu.addAction(tr("Open in Sub-bottom Viewer"), this,
                           [this, clicked_id] {
                emit openInSubBottomRequested(clicked_id);
            });
        } else if (modality == Modality::Sidescan) {
            menu.addAction(tr("Open in Waterfall"), this,
                           [this, clicked_id] {
                emit openInWaterfallRequested(clicked_id);
            });
        }
    }

    menu.addAction(
        multi ? tr("Run %1 Lines").arg(count) : tr("Run Processing"),
        this, [this, ids, multi] {
            if (multi) emit runLayersRequested(ids);
            else       emit runLayerRequested(ids.front());
        });

    menu.addSeparator();

    // Raster layers can export to GeoTIFF (GDAL); other modalities' export is
    // not implemented yet, so it stays disabled per D-05.
    if (static_cast<app::Modality>(item->data(0, kRoleModality).toInt())
            == app::Modality::Raster) {
        menu.addAction(tr("Export GeoTIFF…"), this, [this, ids] {
            emit exportLayersRequested(ids, QStringLiteral("geotiff"));
        });
    } else {
        menu.addMenu(tr("Export"))->setEnabled(false);
    }

    menu.addSeparator();

    if (!multi) {
        const auto* layer = m_project->findLayer(clicked_id);
        if (layer) {
            menu.addMenu(buildTagMenu(
                this, layer->tags,
                [this, clicked_id](std::vector<std::string> tags) {
                    m_project->setLayerTags(clicked_id, std::move(tags));
                    refreshLayer(clicked_id);
                    const auto* updated_layer = m_project->findLayer(clicked_id);
                    const QString label = updated_layer
                        ? QString::fromStdString(updated_layer->label)
                        : tr("layer");
                    const QString description = updated_layer && !updated_layer->tags.empty()
                        ? tr("Tag applied to %1").arg(label)
                        : tr("Tags cleared from %1").arg(label);
                    emit activityLogged(description, 7);
                }));

            menu.addMenu(buildGroupMenu(
                this, m_project->layerGroups(), layer->group_id,
                [this, clicked_id](std::string group_id) {
                    m_project->setLayerGroup(clicked_id, group_id);
                    refresh();
                    const auto* updated_layer = m_project->findLayer(clicked_id);
                    const QString label = updated_layer
                        ? QString::fromStdString(updated_layer->label)
                        : tr("layer");
                    if (group_id.empty()) {
                        emit activityLogged(
                            tr("%1 removed from group").arg(label), 8);
                    } else {
                        const auto* group = m_project->findLayerGroup(group_id);
                        const QString group_name = group
                            ? QString::fromStdString(group->name)
                            : tr("group");
                        emit activityLogged(
                            tr("%1 added to \"%2\"").arg(label, group_name), 8);
                    }
                },
                [this](const std::string& name) -> app::ItemGroup* {
                    auto* group = m_project->addLayerGroup(name);
                    if (group) {
                        emit activityLogged(
                            tr("Group \"%1\" created").arg(QString::fromStdString(name)),
                            8);
                    }
                    return group;
                }));
        }

        menu.addSeparator();

        menu.addAction(tr("Move to Top"), this,
                       [this, item, group_size] {
            moveLayerInTree(item, -group_size);
        })->setEnabled(can_move_up);
        menu.addAction(tr("Move Up"), this,
                       [this, item] { moveLayerInTree(item, -1); })
            ->setEnabled(can_move_up);
        menu.addAction(tr("Move Down"), this,
                       [this, item] { moveLayerInTree(item, +1); })
            ->setEnabled(can_move_down);
        menu.addAction(tr("Move to Bottom"), this,
                       [this, item, group_size] {
            moveLayerInTree(item, +group_size);
        })->setEnabled(can_move_down);

        menu.addSeparator();

        const bool nav_shown = m_visible_nav_tracks.count(clicked_id) > 0;
        menu.addAction(
            nav_shown ? tr("Hide Navigation Track") : tr("Show Navigation Track"),
            this, [this, clicked_id, nav_shown] {
                const bool new_visible = !nav_shown;
                if (nav_shown) m_visible_nav_tracks.erase(clicked_id);
                else           m_visible_nav_tracks.insert(clicked_id);
                emit navTrackVisibilityChanged(clicked_id, new_visible);
            });

        menu.addSeparator();
        menu.addAction(tr("Rename…"), this, [this, clicked_id] {
            emit renameLayerRequested(clicked_id);
        });
        menu.addAction(tr("Remove Layer…"), this, [this, clicked_id] {
            emit removeLayerRequested(clicked_id);
        });
    } else {
        menu.addSeparator();
        menu.addMenu(buildBulkLayerGroupMenu(
            this, m_project->layerGroups(), count,
            [this, ids, count](std::string group_id) {
                m_project->setLayerGroups(ids, group_id);
                refresh();
                if (group_id.empty()) {
                    emit activityLogged(
                        tr("%n layers removed from group", nullptr, count), 8);
                } else {
                    const auto* group = m_project->findLayerGroup(group_id);
                    const QString group_name = group
                        ? QString::fromStdString(group->name)
                        : tr("group");
                    emit activityLogged(
                        tr("%n layers moved to \"%1\"", nullptr, count).arg(group_name),
                        8);
                }
            },
            [this](const std::string& name) -> app::ItemGroup* {
                auto* group = m_project->addLayerGroup(name);
                if (group) {
                    emit activityLogged(
                        tr("Group \"%1\" created").arg(QString::fromStdString(name)),
                        8);
                }
                return group;
            }));

        menu.addSeparator();
        menu.addAction(tr("Remove %1 Layers…").arg(count), this, [this, ids] {
            emit removeLayersRequested(ids);
        });
    }

    menu.exec(global_pos);
}

void LineListPanel::showLayerGroupContextMenu(QTreeWidgetItem* item,
                                              const QPoint& global_pos)
{
    const std::string group_id = item->data(0, kRoleGroupId).toString().toStdString();
    QMenu menu(this);

    menu.addAction(tr("Rename Group…"), this, [this, group_id] {
        const auto* group = m_project->findLayerGroup(group_id);
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Rename Group"), tr("New name:"), QLineEdit::Normal,
            group ? QString::fromStdString(group->name) : QString(), &ok).trimmed();
        if (ok && !name.isEmpty()) {
            m_project->renameLayerGroup(group_id, name.toStdString());
            refresh();
            emit activityLogged(tr("Group renamed to \"%1\"").arg(name), 8);
        }
    });

    menu.addAction(tr("Delete Group"), this, [this, group_id] {
        const auto* group = m_project->findLayerGroup(group_id);
        const QString name = group
            ? QString::fromStdString(group->name)
            : tr("this group");
        if (QMessageBox::question(
                this, tr("Delete Group"),
                tr("Delete \"%1\"? Layers in the group will become ungrouped.")
                    .arg(name)) == QMessageBox::Yes) {
            m_project->removeLayerGroup(group_id);
            refresh();
            emit activityLogged(tr("Group \"%1\" deleted").arg(name), 8);
        }
    });

    menu.addSeparator();
    menu.addAction(tr("New Group…"), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("New Group"), tr("Group name:"), QLineEdit::Normal, {}, &ok)
                                 .trimmed();
        if (ok && !name.isEmpty()) {
            m_project->addLayerGroup(name.toStdString());
            refresh();
            emit activityLogged(tr("Group \"%1\" created").arg(name), 8);
        }
    });

    menu.exec(global_pos);
}

void LineListPanel::moveLayerInTree(QTreeWidgetItem* layer_item, int delta)
{
    if (!layer_item) return;
    auto* parent = layer_item->parent();
    if (!parent) return;

    const int current = parent->indexOfChild(layer_item);
    const int count = parent->childCount();
    if (current < 0) return;

    const int next = std::clamp(current + delta, 0, count - 1);
    if (next == current) return;

    parent->takeChild(current);
    parent->insertChild(next, layer_item);
    m_tree->setCurrentItem(layer_item);
    syncLayerOrderFromTree();
}

} // namespace dolphin::ui
