// Contact, feature, and contact-group context menus for LineListPanel.
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/panels/LineListPanel.ContextMenu_p.h"
#include "ui/shared/panels/LineListPanel_p.h"

#include "app/project/Project.h"
#include "core/Contact.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTreeWidgetItem>

#include <utility>

using namespace dolphin::ui::detail;

namespace dolphin::ui {

void LineListPanel::showContactContextMenu(QTreeWidgetItem* item,
                                           const QPoint& global_pos)
{
    const uint64_t contact_id = item->data(0, kRoleId).toULongLong();

    // Right-clicking one member of an existing multi-selection must operate on
    // the complete selected contact set. A right-click outside that selection
    // remains a single-contact action.
    std::vector<uint64_t> target_contact_ids;
    bool clicked_is_selected = false;
    for (QTreeWidgetItem* selected : m_tree->selectedItems()) {
        if (itemTypeOf(selected) != ItemType::Contact) continue;
        const uint64_t id = selected->data(0, kRoleId).toULongLong();
        target_contact_ids.push_back(id);
        clicked_is_selected |= id == contact_id;
    }
    if (!clicked_is_selected)
        target_contact_ids = {contact_id};

    const core::Contact* contact = nullptr;
    for (const auto& candidate : m_project->contacts()) {
        if (candidate.id == contact_id) {
            contact = &candidate;
            break;
        }
    }

    QMenu menu(this);

    if (contact) {
        menu.addMenu(buildSymbolMenu(
            this, contact->symbol,
            [this, target_contact_ids](std::string symbol) {
                m_project->setContactSymbols(target_contact_ids, symbol);
                emit activityLogged(
                    tr("Icon changed for %n contact(s)", nullptr,
                       static_cast<int>(target_contact_ids.size())), 8);
            }));

        menu.addMenu(buildTagMenu(
            this, contact->tags,
            [this, contact_id](std::vector<std::string> tags) {
                m_project->setContactTags(contact_id, std::move(tags));
                refreshContacts();

                const core::Contact* updated_contact = nullptr;
                for (const auto& candidate : m_project->contacts()) {
                    if (candidate.id == contact_id) {
                        updated_contact = &candidate;
                        break;
                    }
                }
                const QString description = updated_contact
                                            && !updated_contact->tags.empty()
                    ? tr("Tag applied to contact #%1").arg(contact_id)
                    : tr("Tags cleared from contact #%1").arg(contact_id);
                emit activityLogged(description, 7);
            }));

        menu.addMenu(buildGroupMenu(
            this, m_project->contactGroups(), contact->group_id,
            [this, contact_id](std::string group_id) {
                m_project->setContactGroup(contact_id, group_id);
                refreshContacts();
                if (group_id.empty()) {
                    emit activityLogged(
                        tr("Contact #%1 removed from group").arg(contact_id), 8);
                } else {
                    const auto* group = m_project->findContactGroup(group_id);
                    const QString group_name = group
                        ? QString::fromStdString(group->name)
                        : tr("group");
                    emit activityLogged(
                        tr("Contact #%1 added to \"%2\"").arg(contact_id).arg(group_name),
                        8);
                }
            },
            [this](const std::string& name) -> app::ItemGroup* {
                auto* group = m_project->addContactGroup(name);
                if (group) {
                    emit activityLogged(
                        tr("Contact group \"%1\" created")
                            .arg(QString::fromStdString(name)),
                        8);
                }
                return group;
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
    menu.exec(global_pos);
}

void LineListPanel::showFeatureContextMenu(QTreeWidgetItem* item,
                                           const QPoint& global_pos)
{
    const uint64_t feature_id = item->data(0, kRoleId).toULongLong();
    QMenu menu(this);
    menu.addAction(tr("Remove Feature"), this, [this, feature_id] {
        emit removeFeatureRequested(feature_id);
    });
    menu.exec(global_pos);
}

void LineListPanel::showContactGroupContextMenu(QTreeWidgetItem* item,
                                                const QPoint& global_pos)
{
    const std::string group_id = item->data(0, kRoleGroupId).toString().toStdString();
    QMenu menu(this);

    menu.addAction(tr("Rename Group…"), this, [this, group_id] {
        const auto* group = m_project->findContactGroup(group_id);
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Rename Group"), tr("New name:"), QLineEdit::Normal,
            group ? QString::fromStdString(group->name) : QString(), &ok).trimmed();
        if (ok && !name.isEmpty()) {
            m_project->renameContactGroup(group_id, name.toStdString());
            refreshContacts();
            emit activityLogged(
                tr("Contact group renamed to \"%1\"").arg(name), 8);
        }
    });

    menu.addAction(tr("Delete Group"), this, [this, group_id] {
        const auto* group = m_project->findContactGroup(group_id);
        const QString name = group
            ? QString::fromStdString(group->name)
            : tr("this group");
        if (QMessageBox::question(
                this, tr("Delete Group"),
                tr("Delete \"%1\"? Contacts in the group will become ungrouped.")
                    .arg(name)) == QMessageBox::Yes) {
            m_project->removeContactGroup(group_id);
            refreshContacts();
            emit activityLogged(
                tr("Contact group \"%1\" deleted").arg(name), 8);
        }
    });

    menu.addSeparator();
    menu.addAction(tr("New Group…"), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("New Contact Group"), tr("Group name:"),
            QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !name.isEmpty()) {
            m_project->addContactGroup(name.toStdString());
            refreshContacts();
            emit activityLogged(
                tr("Contact group \"%1\" created").arg(name), 8);
        }
    });

    menu.exec(global_pos);
}

} // namespace dolphin::ui
