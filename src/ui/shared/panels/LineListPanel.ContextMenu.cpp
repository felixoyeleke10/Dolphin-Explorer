// LineListPanel.ContextMenu.cpp — right-click menu and in-tree reorder logic.
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/panels/LineListPanel_p.h"
#include "app/layers/LayerUtils.h"
#include "app/project/Project.h"
#include "core/Contact.h"
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QTreeWidget>
#include <functional>

#include <algorithm>

using namespace dolphin::ui::detail;

namespace dolphin::ui {

// -- Tag menu helper -----------------------------------------------------------

// Builds a 16×16 filled-circle icon for a palette entry.
static QIcon makePaletteIcon(const TagPaletteEntry& e)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    {
        QPainter pa(&pm);
        pa.setRenderHint(QPainter::Antialiasing);
        pa.setPen(Qt::NoPen);
        pa.setBrush(QColor(e.r, e.g, e.b));
        pa.drawEllipse(1, 1, 12, 12);
    }
    return QIcon(pm);
}

static QMenu* buildTagMenu(QWidget* parent,
                            const std::vector<std::string>& current_tags,
                            std::function<void(std::vector<std::string>)> apply)
{
    auto* menu = new QMenu(QObject::tr("Tags"), parent);

    for (const auto& e : kTagPalette) {
        const std::string tid(e.id);
        const bool active = std::find(current_tags.begin(), current_tags.end(), tid)
                            != current_tags.end();

        auto* act = menu->addAction(makePaletteIcon(e), QObject::tr(e.label),
                                    parent, [current_tags, tid, apply]() {
            auto tags = current_tags;
            auto it   = std::find(tags.begin(), tags.end(), tid);
            if (it != tags.end()) tags.erase(it);
            else                  tags.push_back(tid);
            apply(std::move(tags));
        });
        act->setCheckable(true);
        act->setChecked(active);
    }

    if (!current_tags.empty()) {
        menu->addSeparator();
        menu->addAction(QObject::tr("Clear All Tags"), parent, [apply]() { apply({}); });
    }

    return menu;
}

// -- Group assignment submenu helper ------------------------------------------

static QMenu* buildGroupMenu(QWidget* parent,
                              const std::vector<app::ItemGroup>& groups,
                              const std::string& current_group_id,
                              std::function<void(std::string)> apply_group,
                              std::function<app::ItemGroup*(const std::string&)> create_group)
{
    auto* menu = new QMenu(QObject::tr("Group"), parent);

    if (!current_group_id.empty()) {
        menu->addAction(QObject::tr("Remove from Group"), parent,
                        [apply_group]() { apply_group({}); });
        menu->addSeparator();
    }

    for (const auto& g : groups) {
        auto* act = menu->addAction(QString::fromStdString(g.name),
                                    parent, [gid = g.id, apply_group]() {
            apply_group(gid);
        });
        if (g.id == current_group_id) {
            act->setCheckable(true);
            act->setChecked(true);
        }
    }

    menu->addSeparator();
    menu->addAction(QObject::tr("New Group…"), parent,
                    [parent, create_group, apply_group]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            parent, QObject::tr("New Group"),
            QObject::tr("Group name:"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        auto* grp = create_group(name.toStdString());
        if (grp) apply_group(grp->id);
    });

    return menu;
}

static QMenu* buildBulkLayerGroupMenu(QWidget* parent,
                                      const std::vector<app::ItemGroup>& groups,
                                      int layer_count,
                                      std::function<void(std::string)> apply_group,
                                      std::function<app::ItemGroup*(const std::string&)> create_group)
{
    auto* menu = new QMenu(QObject::tr("Group"), parent);

    menu->addAction(QObject::tr("New Group from Selection..."), parent,
                    [parent, layer_count, apply_group, create_group]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            parent, QObject::tr("New Group"),
            QObject::tr("Group name:"), QLineEdit::Normal,
            QObject::tr("%n selected lines", nullptr, layer_count), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        auto* grp = create_group(name.toStdString());
        if (grp) apply_group(grp->id);
    });

    menu->addSeparator();

    if (groups.empty()) {
        menu->addAction(QObject::tr("No existing groups"))->setEnabled(false);
    } else {
        for (const auto& g : groups) {
            menu->addAction(QString::fromStdString(g.name),
                            parent, [gid = g.id, apply_group]() {
                apply_group(gid);
            });
        }
    }

    menu->addSeparator();
    menu->addAction(QObject::tr("Remove from Group"), parent,
                    [apply_group]() { apply_group({}); });

    return menu;
}

// -- Main context menu handler -------------------------------------------------

void LineListPanel::onContextMenuRequested(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item) return;

    const auto   type = itemTypeOf(item);
    const QPoint gpos = m_tree->viewport()->mapToGlobal(pos);

    // -- Project root item ----------------------------------------------------
    if (type == ItemType::Project) {
        QMenu menu(this);

        menu.addAction(tr("Rename Project..."), this, [this] {
            emit renameProjectRequested();
        });
        menu.addSeparator();
        menu.addAction(tr("Save Project"), this, [this] {
            emit saveProjectRequested();
        });
        menu.addAction(tr("Save Project As..."), this, [this] {
            emit saveProjectAsRequested();
        });
        menu.addSeparator();
        auto* open_folder = menu.addAction(tr("Open Project Folder"), this, [this] {
            emit openProjectFolderRequested();
        });
        open_folder->setEnabled(m_project && !m_project->manifestPath().empty());
        menu.addSeparator();
        menu.addAction(tr("Close Project"), this, [this] {
            emit closeProjectRequested();
        });
        menu.addAction(tr("Delete Project..."), this, [this] {
            emit deleteProjectRequested();
        })->setEnabled(m_project && !m_project->manifestPath().empty());

        menu.exec(gpos);

    // -- Layer item ------------------------------------------------------------
    } else if (type == ItemType::Layer) {
        const std::string clicked_id = item->data(0, kRoleId).toString().toStdString();

        std::vector<std::string> ids;
        for (QTreeWidgetItem* sel : m_tree->selectedItems()) {
            if (itemTypeOf(sel) == ItemType::Layer)
                ids.push_back(sel->data(0, kRoleId).toString().toStdString());
        }
        if (std::find(ids.begin(), ids.end(), clicked_id) == ids.end())
            ids = { clicked_id };

        const bool multi    = (ids.size() > 1);
        const int  n        = static_cast<int>(ids.size());
        auto* grp_item      = item->parent();
        const int  cur_pos  = grp_item ? grp_item->indexOfChild(item) : -1;
        const int  grp_size = grp_item ? grp_item->childCount() : 0;
        const bool can_up   = (cur_pos > 0);
        const bool can_down = (cur_pos >= 0 && cur_pos < grp_size - 1);

        QMenu menu(this);

        // -- Open in viewer ----------------------------------------------------
        if (!multi) {
            using M = app::Modality;
            const auto mod = static_cast<M>(item->data(0, kRoleModality).toInt());
            if (mod == M::SubBottom) {
                menu.addAction(tr("Open in Sub-bottom Viewer"), this,
                               [this, clicked_id] {
                    emit openInSubBottomRequested(clicked_id);
                });
            } else if (mod == M::Sidescan) {
                menu.addAction(tr("Open in Waterfall"), this,
                               [this, clicked_id] {
                    emit openInWaterfallRequested(clicked_id);
                });
            }
        }

        menu.addAction(
            multi ? tr("Run %1 Lines").arg(n) : tr("Run Processing"),
            this, [this, ids, multi] {
                if (multi) emit runLayersRequested(ids);
                else       emit runLayerRequested(ids.front());
            });

        menu.addSeparator();

        // -- Export ------------------------------------------------------------
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

        menu.addAction(tr("Merge Lines…"))->setEnabled(false);

        menu.addSeparator();

        // -- Tags and groups (single-layer only) -------------------------------
        if (!multi) {
            const auto* layer = m_project->findLayer(clicked_id);
            if (layer) {
                // Tags submenu
                menu.addMenu(buildTagMenu(this, layer->tags,
                    [this, clicked_id](std::vector<std::string> tags) {
                        m_project->setLayerTags(clicked_id, std::move(tags));
                        refreshLayer(clicked_id);
                        const auto* l = m_project->findLayer(clicked_id);
                        const QString lbl = l ? QString::fromStdString(l->label) : tr("layer");
                        const QString desc = (l && !l->tags.empty())
                            ? tr("Tag applied to %1").arg(lbl)
                            : tr("Tags cleared from %1").arg(lbl);
                        emit activityLogged(desc, 7);
                    }));

                // Group submenu
                menu.addMenu(buildGroupMenu(this,
                    m_project->layerGroups(),
                    layer->group_id,
                    [this, clicked_id](std::string gid) {
                        m_project->setLayerGroup(clicked_id, gid);
                        refresh();
                        const auto* l = m_project->findLayer(clicked_id);
                        const QString lbl = l ? QString::fromStdString(l->label) : tr("layer");
                        if (gid.empty()) {
                            emit activityLogged(tr("%1 removed from group").arg(lbl), 8);
                        } else {
                            const auto* g = m_project->findLayerGroup(gid);
                            const QString gnm = g ? QString::fromStdString(g->name) : tr("group");
                            emit activityLogged(tr("%1 added to \"%2\"").arg(lbl, gnm), 8);
                        }
                    },
                    [this](const std::string& name) -> app::ItemGroup* {
                        auto* grp = m_project->addLayerGroup(name);
                        if (grp) emit activityLogged(
                            tr("Group \"%1\" created").arg(QString::fromStdString(name)), 8);
                        return grp;
                    }));
            }

            menu.addSeparator();

            // -- Order ---------------------------------------------------------
            menu.addAction(tr("Move to Top"),    this, [this, item, grp_size] { moveLayerInTree(item, -grp_size); })->setEnabled(can_up);
            menu.addAction(tr("Move Up"),        this, [this, item]           { moveLayerInTree(item, -1);        })->setEnabled(can_up);
            menu.addAction(tr("Move Down"),      this, [this, item]           { moveLayerInTree(item, +1);        })->setEnabled(can_down);
            menu.addAction(tr("Move to Bottom"), this, [this, item, grp_size] { moveLayerInTree(item, +grp_size); })->setEnabled(can_down);

            menu.addSeparator();

            // -- Nav track -----------------------------------------------------
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
            // -- Multi-layer organisation --------------------------------------
            menu.addSeparator();
            menu.addMenu(buildBulkLayerGroupMenu(this,
                m_project->layerGroups(),
                n,
                [this, ids, n](std::string gid) {
                    m_project->setLayerGroups(ids, gid);
                    refresh();
                    if (gid.empty()) {
                        emit activityLogged(
                            tr("%n layers removed from group", nullptr, n), 8);
                    } else {
                        const auto* g = m_project->findLayerGroup(gid);
                        const QString gnm = g ? QString::fromStdString(g->name) : tr("group");
                        emit activityLogged(
                            tr("%n layers moved to \"%1\"", nullptr, n).arg(gnm), 8);
                    }
                },
                [this](const std::string& name) -> app::ItemGroup* {
                    auto* grp = m_project->addLayerGroup(name);
                    if (grp) emit activityLogged(
                        tr("Group \"%1\" created").arg(QString::fromStdString(name)), 8);
                    return grp;
                }));

            menu.addSeparator();
            menu.addAction(tr("Remove %1 Layers…").arg(n), this, [this, ids] {
                emit removeLayersRequested(ids);
            });
        }

        menu.exec(gpos);

    // -- Layer group item ------------------------------------------------------
    } else if (type == ItemType::LayerGroup) {
        const std::string group_id = item->data(0, kRoleGroupId).toString().toStdString();
        QMenu menu(this);

        menu.addAction(tr("Rename Group…"), this, [this, group_id]() {
            const auto* g = m_project->findLayerGroup(group_id);
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("Rename Group"), tr("New name:"),
                QLineEdit::Normal,
                g ? QString::fromStdString(g->name) : QString(), &ok).trimmed();
            if (ok && !name.isEmpty()) {
                m_project->renameLayerGroup(group_id, name.toStdString());
                refresh();
                emit activityLogged(tr("Group renamed to \"%1\"").arg(name), 8);
            }
        });

        menu.addAction(tr("Delete Group"), this, [this, group_id]() {
            const auto* g = m_project->findLayerGroup(group_id);
            const QString name = g ? QString::fromStdString(g->name) : tr("this group");
            if (QMessageBox::question(this, tr("Delete Group"),
                    tr("Delete \"%1\"? Layers in the group will become ungrouped.").arg(name))
                    == QMessageBox::Yes) {
                m_project->removeLayerGroup(group_id);
                refresh();
                emit activityLogged(tr("Group \"%1\" deleted").arg(name), 8);
            }
        });

        menu.addSeparator();
        menu.addAction(tr("New Group…"), this, [this]() {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("New Group"), tr("Group name:"),
                QLineEdit::Normal, {}, &ok).trimmed();
            if (ok && !name.isEmpty()) {
                m_project->addLayerGroup(name.toStdString());
                refresh();
                emit activityLogged(tr("Group \"%1\" created").arg(name), 8);
            }
        });

        menu.exec(gpos);

    // -- Source item -----------------------------------------------------------
    } else if (type == ItemType::Source) {
        const std::string source_id = item->data(0, kRoleId).toString().toStdString();
        QMenu menu(this);
        menu.addAction(tr("Reveal in Explorer"), this, [this, source_id] {
            emit revealInExplorerRequested(source_id);
        });
        menu.exec(gpos);

    // -- Contact item ----------------------------------------------------------
    } else if (type == ItemType::Contact) {
        const uint64_t contact_id = item->data(0, kRoleId).toULongLong();

        // Find contact to get current tags/group
        const core::Contact* contact = nullptr;
        for (const auto& c : m_project->contacts())
            if (c.id == contact_id) { contact = &c; break; }

        QMenu menu(this);

        if (contact) {
            // Tags submenu
            menu.addMenu(buildTagMenu(this, contact->tags,
                [this, contact_id](std::vector<std::string> tags) {
                    m_project->setContactTags(contact_id, std::move(tags));
                    refreshContacts();
                    // Find the contact again after mutation
                    const core::Contact* c = nullptr;
                    for (const auto& x : m_project->contacts())
                        if (x.id == contact_id) { c = &x; break; }
                    const QString desc = (c && !c->tags.empty())
                        ? tr("Tag applied to contact #%1").arg(contact_id)
                        : tr("Tags cleared from contact #%1").arg(contact_id);
                    emit activityLogged(desc, 7);
                }));

            // Group submenu
            menu.addMenu(buildGroupMenu(this,
                m_project->contactGroups(),
                contact->group_id,
                [this, contact_id](std::string gid) {
                    m_project->setContactGroup(contact_id, gid);
                    refreshContacts();
                    if (gid.empty()) {
                        emit activityLogged(
                            tr("Contact #%1 removed from group").arg(contact_id), 8);
                    } else {
                        const auto* g = m_project->findContactGroup(gid);
                        const QString gnm = g ? QString::fromStdString(g->name) : tr("group");
                        emit activityLogged(
                            tr("Contact #%1 added to \"%2\"").arg(contact_id).arg(gnm), 8);
                    }
                },
                [this](const std::string& name) -> app::ItemGroup* {
                    auto* grp = m_project->addContactGroup(name);
                    if (grp) emit activityLogged(
                        tr("Contact group \"%1\" created").arg(QString::fromStdString(name)), 8);
                    return grp;
                }));

            menu.addSeparator();
        }

        menu.addAction(tr("Remove Contact"), this, [this, contact_id] {
            emit removeContactRequested(contact_id);
        });
        menu.addSeparator();
        menu.addAction(tr("Export Contacts…"), this, [this] {
            emit exportContactsRequested();
        });
        menu.exec(gpos);

    // -- Feature item ----------------------------------------------------------
    } else if (type == ItemType::Feature) {
        const uint64_t feature_id = item->data(0, kRoleId).toULongLong();
        QMenu menu(this);
        menu.addAction(tr("Remove Feature"), this, [this, feature_id] {
            emit removeFeatureRequested(feature_id);
        });
        menu.exec(gpos);

    // -- Contact group item ----------------------------------------------------
    } else if (type == ItemType::ContactGroup) {
        const std::string group_id = item->data(0, kRoleGroupId).toString().toStdString();
        QMenu menu(this);

        menu.addAction(tr("Rename Group…"), this, [this, group_id]() {
            const auto* g = m_project->findContactGroup(group_id);
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("Rename Group"), tr("New name:"),
                QLineEdit::Normal,
                g ? QString::fromStdString(g->name) : QString(), &ok).trimmed();
            if (ok && !name.isEmpty()) {
                m_project->renameContactGroup(group_id, name.toStdString());
                refreshContacts();
                emit activityLogged(tr("Contact group renamed to \"%1\"").arg(name), 8);
            }
        });

        menu.addAction(tr("Delete Group"), this, [this, group_id]() {
            const auto* g = m_project->findContactGroup(group_id);
            const QString name = g ? QString::fromStdString(g->name) : tr("this group");
            if (QMessageBox::question(this, tr("Delete Group"),
                    tr("Delete \"%1\"? Contacts in the group will become ungrouped.").arg(name))
                    == QMessageBox::Yes) {
                m_project->removeContactGroup(group_id);
                refreshContacts();
                emit activityLogged(tr("Contact group \"%1\" deleted").arg(name), 8);
            }
        });

        menu.addSeparator();
        menu.addAction(tr("New Group…"), this, [this]() {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("New Contact Group"), tr("Group name:"),
                QLineEdit::Normal, {}, &ok).trimmed();
            if (ok && !name.isEmpty()) {
                m_project->addContactGroup(name.toStdString());
                refreshContacts();
                emit activityLogged(tr("Contact group \"%1\" created").arg(name), 8);
            }
        });

        menu.exec(gpos);
    }
}

void LineListPanel::moveLayerInTree(QTreeWidgetItem* layer_item, int delta)
{
    if (!layer_item) return;
    auto* par = layer_item->parent();
    if (!par) return;

    const int cur = par->indexOfChild(layer_item);
    const int cnt = par->childCount();
    if (cur < 0) return;

    const int nxt = std::clamp(cur + delta, 0, cnt - 1);
    if (nxt == cur) return;

    par->takeChild(cur);
    par->insertChild(nxt, layer_item);
    m_tree->setCurrentItem(layer_item);
    syncLayerOrderFromTree();
}

} // namespace dolphin::ui
