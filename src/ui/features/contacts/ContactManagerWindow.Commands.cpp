// ContactManagerWindow.Commands.cpp — selection helpers, clipboard (cut/copy/
// paste), rename/favourite, recycle-bin actions, export, and custom-group ops.
// Mutations are routed to MainWindow's undo stack via signals; the window emits
// intents and never touches the project's undo-relevant state directly.
#include "ui/features/contacts/ContactManagerWindow.h"
#include "ui/features/contacts/ContactVisuals.h"
#include "ui/features/contacts/ContactReport.h"
#include "ui/features/contacts/ContactEditorDialog.h"
#include "ui/shared/CoordFormat.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/layers/LayerUtils.h"
#include "core/SpatialRef.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVector>

#include <algorithm>

namespace dolphin::ui {

using namespace dolphin::ui::cmvis;

// -- Selection helpers --------------------------------------------------------

uint64_t ContactManagerWindow::currentRowId() const
{
    if (m_view_mode == 1) {
        auto* it = m_thumbs->currentItem();
        return (it && !it->isHidden()) ? it->data(Qt::UserRole).toULongLong() : 0;
    }
    const int row = m_table->currentRow();
    if (row < 0) return 0;
    auto* item = m_table->item(row, ColLabel);
    return item ? item->data(Qt::UserRole).toULongLong() : 0;
}

std::vector<uint64_t> ContactManagerWindow::selectedIds() const
{
    std::vector<uint64_t> ids;
    if (m_view_mode == 1) {
        for (auto* it : m_thumbs->selectedItems())
            if (!it->isHidden()) ids.push_back(it->data(Qt::UserRole).toULongLong());
        return ids;
    }
    if (!m_table->selectionModel()) return ids;
    for (const auto& idx : m_table->selectionModel()->selectedRows()) {
        if (m_table->isRowHidden(idx.row())) continue;
        if (auto* it = m_table->item(idx.row(), ColLabel))
            ids.push_back(it->data(Qt::UserRole).toULongLong());
    }
    return ids;
}

std::vector<uint64_t> ContactManagerWindow::visibleIdsInOrder() const
{
    std::vector<uint64_t> ids;
    if (m_view_mode == 1) {
        for (int i = 0; i < m_thumbs->count(); ++i)
            if (!m_thumbs->item(i)->isHidden())
                ids.push_back(m_thumbs->item(i)->data(Qt::UserRole).toULongLong());
    } else {
        for (int row = 0; row < m_table->rowCount(); ++row)
            if (!m_table->isRowHidden(row))
                if (auto* it = m_table->item(row, ColLabel))
                    ids.push_back(it->data(Qt::UserRole).toULongLong());
    }
    return ids;
}

const core::Contact* ContactManagerWindow::findContact(uint64_t id) const
{
    if (!m_project) return nullptr;
    for (const auto& c : m_project->contacts())
        if (c.id == id) return &c;
    for (const auto& c : m_project->recycledContacts())   // also the recycle bin
        if (c.id == id) return &c;
    return nullptr;
}

bool ContactManagerWindow::isViewingRecycleBin() const
{
    auto* cur = m_nav->currentItem();
    return cur && cur->data(0, RoleKind).toInt() == NavRecycle;
}

void ContactManagerWindow::selectContact(uint64_t id)
{
    auto findRow = [this, id]() -> int {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            auto* item = m_table->item(row, ColLabel);
            if (item && item->data(Qt::UserRole).toULongLong() == id) return row;
        }
        return -1;
    };
    int row = findRow();
    if (row < 0 && m_nav->topLevelItemCount() > 0) {
        m_nav->setCurrentItem(m_nav->topLevelItem(0));
        row = findRow();
    }
    if (row < 0) return;

    if (m_view_mode == 1) {
        for (int i = 0; i < m_thumbs->count(); ++i) {
            auto* it = m_thumbs->item(i);
            if (it->data(Qt::UserRole).toULongLong() == id) {
                m_thumbs->setCurrentItem(it);
                m_thumbs->scrollToItem(it);
                break;
            }
        }
    } else {
        m_table->selectRow(row);
        m_table->scrollToItem(m_table->item(row, ColLabel));
    }
}

// -- Clipboard / edits --------------------------------------------------------

void ContactManagerWindow::copySelection(bool cut)
{
    const auto ids = selectedIds();
    if (ids.empty()) return;

    m_clipboard.clear();
    QStringList lines;
    lines << QStringLiteral("Label\tSensor\tSource\tClass\tConfidence\tLat\tLon\tDepth\tRange");
    for (uint64_t id : ids) {
        const auto* c = findContact(id);
        if (!c) continue;
        m_clipboard.push_back(*c);

        app::DataLayer* layer = c->line_id.empty() ? nullptr : m_project->findLayer(c->line_id);
        const QString tag = layer ? modalityTag(layer->modality) : tr("Map");
        const QString src = layer ? QString::fromStdString(layer->label)
                                  : QString::fromStdString(c->line_id);
        const bool proj = core::spatialRefIsProjected(c->spatial_ref);
        lines << QStringList{
            QString::fromStdString(c->label), tag, src,
            QString::fromStdString(c->classification), confidenceLabel(c->confidence),
            formatCoord(c->lat, proj, 'N', 'S'), formatCoord(c->lon, proj, 'E', 'W'),
            QString::number(c->depth_m, 'f', 1), QString::number(c->range_m, 'f', 1)
        }.join(QLatin1Char('\t'));
    }
    m_clip_cut = cut;
    if (auto* cb = QApplication::clipboard())
        cb->setText(lines.join(QLatin1Char('\n')));
    updateCommandState();
}

void ContactManagerWindow::pasteClipboard()
{
    if (!m_project || m_clipboard.empty()) return;
    QVector<core::Contact> to_add;
    for (const auto& src : m_clipboard) {
        core::Contact nc = src;
        nc.id = 0;
        nc.created_at = 0.0;
        if (!m_clip_cut) nc.label += " copy";
        to_add.push_back(nc);
    }
    emit contactsAddRequested(to_add);                     // undoable add (project assigns ids)
    if (m_clip_cut) {
        for (const auto& src : m_clipboard)
            emit removeContactRequested(src.id);          // move originals to recycle bin
        m_clipboard.clear();
        m_clip_cut = false;
    }
    updateCommandState();
}

void ContactManagerWindow::renameSelection()
{
    const auto ids = selectedIds();
    if (ids.size() != 1 || !m_project) return;
    const auto* c = findContact(ids.front());
    if (!c) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Contact"),
        tr("Label:"), QLineEdit::Normal, QString::fromStdString(c->label), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    core::Contact updated = *c;
    updated.label = name.trimmed().toStdString();
    const uint64_t id = updated.id;
    emit contactsEditRequested({ *c }, { updated });   // undoable via MainWindow
    selectContact(id);
}

void ContactManagerWindow::toggleFavouriteSelection()
{
    const auto ids = selectedIds();
    if (ids.empty() || !m_project) return;
    QVector<core::Contact> before, after;
    for (uint64_t id : ids) {
        const auto* c = findContact(id);
        if (!c) continue;
        core::Contact updated = *c;
        auto it = std::find(updated.tags.begin(), updated.tags.end(), kFavTag);
        if (it != updated.tags.end()) updated.tags.erase(it);
        else                          updated.tags.push_back(kFavTag);
        before.push_back(*c);
        after.push_back(updated);
    }
    if (!before.isEmpty()) emit contactsEditRequested(before, after);   // one undo step
}

void ContactManagerWindow::invertSelection()
{
    if (m_view_mode == 1) {
        for (int i = 0; i < m_thumbs->count(); ++i) {
            auto* it = m_thumbs->item(i);
            if (!it->isHidden()) it->setSelected(!it->isSelected());
        }
        return;
    }
    auto* sm = m_table->selectionModel();
    auto* model = m_table->model();
    if (!sm || !model) return;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->isRowHidden(row)) continue;
        sm->select(model->index(row, 0), QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    }
}

// -- Recycle bin --------------------------------------------------------------

void ContactManagerWindow::deleteSelection()
{
    // Inside the recycle bin, "Delete" means permanent removal.
    if (isViewingRecycleBin()) { purgeSelection(); return; }

    const auto ids = selectedIds();
    if (ids.empty()) return;
    if (m_confirm_delete) {
        const auto btn = QMessageBox::question(this, tr("Delete Contacts"),
            tr("Move %n selected contact(s) to the Recycle Bin?", nullptr, static_cast<int>(ids.size())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes) return;
    }
    for (uint64_t id : ids) emit removeContactRequested(id);   // → undoable recycle
}

void ContactManagerWindow::restoreSelection()
{
    if (!m_project) return;
    const auto ids = selectedIds();
    for (uint64_t id : ids) m_project->restoreContact(id);   // → bus → refresh
}

void ContactManagerWindow::purgeSelection()
{
    if (!m_project) return;
    const auto ids = selectedIds();
    if (ids.empty()) return;
    const auto btn = QMessageBox::question(this, tr("Delete Forever"),
        tr("Permanently delete %n contact(s)? This cannot be undone.", nullptr,
           static_cast<int>(ids.size())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn != QMessageBox::Yes) return;
    for (uint64_t id : ids) {
        const QString snap = contactSnapshotPath(m_project, id);   // drop derived thumbnail
        if (!snap.isEmpty()) QFile::remove(snap);
        m_project->purgeContact(id);     // → bus → refresh
    }
}

void ContactManagerWindow::emptyRecycleBin()
{
    if (!m_project || m_project->recycledContacts().empty()) return;
    const auto btn = QMessageBox::question(this, tr("Empty Recycle Bin"),
        tr("Permanently delete all %n contact(s) in the Recycle Bin? This cannot be undone.",
           nullptr, static_cast<int>(m_project->recycledContacts().size())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn != QMessageBox::Yes) return;
    for (const auto& c : m_project->recycledContacts()) {
        const QString snap = contactSnapshotPath(m_project, c.id);   // drop derived thumbnails
        if (!snap.isEmpty()) QFile::remove(snap);
    }
    m_project->emptyRecycleBin();   // → bus → refresh
}

// -- Export -------------------------------------------------------------------

void ContactManagerWindow::exportContacts()
{
    if (!m_project) return;

    // Export the visible set (current folder + search) in whichever view is active —
    // the same scope the report and the on-screen list use.
    const std::vector<uint64_t> ids = visibleIdsInOrder();

    std::vector<core::Contact> rows;
    rows.reserve(ids.size());
    for (uint64_t id : ids)
        if (const auto* c = findContact(id)) rows.push_back(*c);

    // Scope title from the current folder (breadcrumb crumb, minus its count).
    QString scope = tr("All Contacts");
    if (auto* cur = m_nav->currentItem()) {
        scope = cur->text(0);
        const int paren = scope.lastIndexOf(QLatin1String(" ("));
        if (paren > 0) scope = scope.left(paren);
    }
    exportContactSet(rows, tr("Contact Report — %1").arg(scope));
}

void ContactManagerWindow::exportContactSet(const std::vector<core::Contact>& rows,
                                            const QString& title)
{
    if (!m_project) return;
    const QString path = ContactReport::exportInteractive(this, title, rows, m_project);
    if (!path.isEmpty()) statusBar()->showMessage(tr("Exported: %1").arg(path), 5000);
}

// -- Custom groups ------------------------------------------------------------

void ContactManagerWindow::newGroup()
{
    if (!m_project) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Group"),
        tr("Group name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    m_expanded_facets.insert(GroupGroup);   // expand the section once the bus refresh lands
    emit groupAddRequested(name.trimmed());   // undoable via MainWindow
}

void ContactManagerWindow::renameGroup(const std::string& id, const QString& current_name)
{
    if (!m_project) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Group"),
        tr("Group name:"), QLineEdit::Normal, current_name, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    emit groupRenameRequested(QString::fromStdString(id), name.trimmed());   // undoable
}

void ContactManagerWindow::deleteGroup(const std::string& id, const QString& name)
{
    if (!m_project) return;
    const auto btn = QMessageBox::question(this, tr("Delete Group"),
        tr("Delete the group \"%1\"?\nIts contacts are kept and moved to Ungrouped.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn != QMessageBox::Yes) return;
    emit groupRemoveRequested(QString::fromStdString(id));   // undoable
}

void ContactManagerWindow::assignSelectionToGroup(const std::string& group_id)
{
    if (!m_project) return;
    const auto ids = selectedIds();
    if (ids.empty()) return;
    QVector<core::Contact> before, after;
    for (uint64_t id : ids) {
        const auto* c = findContact(id);
        if (!c || c->group_id == group_id) continue;
        core::Contact updated = *c;
        updated.group_id = group_id;
        before.push_back(*c);
        after.push_back(updated);
    }
    if (!before.isEmpty()) emit contactsEditRequested(before, after);   // one undo step
}

void ContactManagerWindow::showNavContextMenu(const QPoint& global_pos, QTreeWidgetItem* item)
{
    if (!m_project) return;
    const int kind  = item ? item->data(0, RoleKind).toInt()  : -1;
    const int facet = item ? item->data(0, RoleFacet).toInt() : -1;
    const bool in_group_area = (kind == NavGroup)
                            || (kind == NavFacetHeader && facet == GroupGroup);
    if (!in_group_area) return;   // group management lives in the Group section

    QMenu menu(this);
    auto* add = menu.addAction(tr("New Group…"));
    connect(add, &QAction::triggered, this, [this]() { newGroup(); });

    if (kind == NavGroup) {
        const std::string id = item->data(0, RoleGroupId).toString().toStdString();
        if (!id.empty()) {
            QString gname = QString::fromStdString(id);
            if (auto* g = m_project->findContactGroup(id)) gname = QString::fromStdString(g->name);
            menu.addSeparator();
            auto* ren = menu.addAction(tr("Rename Group…"));
            connect(ren, &QAction::triggered, this, [this, id, gname]() { renameGroup(id, gname); });
            auto* del = menu.addAction(tr("Delete Group"));
            connect(del, &QAction::triggered, this, [this, id, gname]() { deleteGroup(id, gname); });
        }
    }
    menu.exec(global_pos);
}

// -- Contact editor -----------------------------------------------------------

void ContactManagerWindow::openContactEditor(uint64_t id)
{
    if (!m_project || id == 0) return;
    if (!findContact(id)) return;

    // Sync map / preview selection to the contact being edited.
    emit contactActivated(id);

    std::vector<uint64_t> ids = visibleIdsInOrder();
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);

    if (!m_editor) {
        m_editor = new ContactEditorDialog(m_project, ids, id, this);
        m_editor->setAttribute(Qt::WA_DeleteOnClose);
        if (m_snapshot_provider)
            m_editor->setSnapshotProvider(m_snapshot_provider);

        // Route the editor's intents through the manager's existing undoable signals.
        connect(m_editor, &ContactEditorDialog::contactSaveRequested, this,
                [this](const core::Contact& before, const core::Contact& after) {
                    emit contactsEditRequested({ before }, { after });
                });
        connect(m_editor, &ContactEditorDialog::removeContactRequested, this,
                [this](uint64_t rid) { emit removeContactRequested(rid); });
        connect(m_editor, &ContactEditorDialog::exportRequested, this,
                [this](uint64_t rid) {
                    // Export just the contact being edited, not the whole view.
                    if (const auto* c = findContact(rid))
                        exportContactSet({ *c },
                            tr("Contact Report — %1").arg(QString::fromStdString(c->label)));
                });
        connect(m_editor, &ContactEditorDialog::contactActivated, this,
                [this](uint64_t rid) { emit contactActivated(rid); });
    } else {
        m_editor->showContact(ids, id);
    }
    m_editor->show();
    m_editor->raise();
    m_editor->activateWindow();
}

} // namespace dolphin::ui
