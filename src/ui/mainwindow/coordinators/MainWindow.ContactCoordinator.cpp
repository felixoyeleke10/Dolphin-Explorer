// MainWindow.ContactCoordinator.cpp — contact creation/selection and the
// Contact Manager window's lifecycle + undoable-edit wiring. Split out of
// MainWindow.WaterfallCoordinator.cpp (contacts are a distinct feature concern).
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/features/contacts/ContactManagerWindow.h"
#include "ui/features/contacts/ContactVisuals.h"
#include "ui/features/map/MapView.h"
#include "ui/systems/ProjectEventBus.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/Contact.h"
#include "core/SpatialRef.h"

#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QString>
#include <QVector>

namespace dolphin::ui {

void MainWindow::onWaterfallContactCreated(float range_m, double lat, double lon,
                                           bool is_projected,
                                           const QString& classification,
                                           const QString& line_id,
                                           uint64_t abs_row,
                                           int channel_idx,
                                           const QPixmap& snapshot)
{
    if (!currentProject()) return;

    core::Contact c;
    // Leave the label empty: the project assigns a stable, monotonic "Cnnn" from the
    // contact id, so removals never cause a later pick to reuse a surviving number.
    c.lat            = lat;
    c.lon            = lon;
    c.spatial_ref    = is_projected
                     ? core::makeUnknownProjectedSpatialRef()
                     : core::makeWgs84SpatialRef();
    c.classification = classification.toStdString();
    c.line_id        = line_id.toStdString();
    c.range_m        = range_m;
    c.artifact_id    = abs_row;
    c.sample_idx     = static_cast<uint32_t>(channel_idx);

    auto* cmd = new AddContactCommand(currentProject(), c, []() {});
    m_undo_stack->push(cmd);

    // Read back the project-assigned label for the status message.
    QString label;
    for (const auto& ct : currentProject()->contacts())
        if (ct.id == cmd->assignedId()) { label = QString::fromStdString(ct.label); break; }

    // Persist the square pick snapshot as the contact's thumbnail (derived artifact
    // keyed on the assigned id; the Contact Manager loads it by id). Best-effort.
    if (!snapshot.isNull() && cmd->assignedId() != 0) {
        const QString path = cmvis::contactSnapshotPath(currentProject(), cmd->assignedId());
        if (!path.isEmpty()) {
            QFileInfo fi(path);
            QDir().mkpath(fi.absolutePath());
            snapshot.save(path, "PNG");
        }
    }

    appendJobMessage(
        QString("Contact %1 placed — %2 (%3 m)")
            .arg(label)
            .arg(classification)
            .arg(range_m, 0, 'f', 1));
}

void MainWindow::onContactSelected(uint64_t contact_id)
{
    if (m_map_view) m_map_view->setSelectedContact(contact_id);

    if (!currentProject() || !m_inspector) return;
    for (const auto& c : currentProject()->contacts())
        if (c.id == contact_id) { m_inspector->showContact(&c); return; }
}

void MainWindow::onContactPicked(double lat, double lon,
                                 uint64_t /*artifact_id*/, uint32_t /*sample_idx*/)
{
    appendJobMessage(QString("Picked  Lat %1  Lon %2")
        .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6));
}

void MainWindow::onContactManagerOpen()
{
    if (!m_contact_mgr_win) {
        auto* win = new ContactManagerWindow(nullptr);
        win->setAttribute(Qt::WA_DeleteOnClose);
        m_contact_mgr_win = win;

        // Select a row → navigate (map highlight + inspector detail for editing).
        connect(win, &ContactManagerWindow::contactActivated,
                this, &MainWindow::onContactSelected);

        // Delete = undoable soft-delete into the project recycle bin (undo restores).
        connect(win, &ContactManagerWindow::removeContactRequested, this,
                [this](uint64_t id) {
                    if (currentProject())
                        m_undo_stack->push(new RecycleContactCommand(currentProject(), id));
                });

        // Rename / favourite / group-assign — undoable edits (one macro per action).
        connect(win, &ContactManagerWindow::contactsEditRequested, this,
                [this](const QVector<core::Contact>& before, const QVector<core::Contact>& after) {
                    if (!currentProject() || before.isEmpty() || before.size() != after.size()) return;
                    m_undo_stack->beginMacro(tr("Edit Contacts"));
                    for (int i = 0; i < before.size(); ++i)
                        m_undo_stack->push(new UpdateContactCommand(currentProject(), before[i], after[i]));
                    m_undo_stack->endMacro();
                });

        // Paste — undoable add of the pasted contacts.
        connect(win, &ContactManagerWindow::contactsAddRequested, this,
                [this](const QVector<core::Contact>& contacts) {
                    if (!currentProject() || contacts.isEmpty()) return;
                    m_undo_stack->beginMacro(tr("Add Contacts"));
                    for (const auto& c : contacts)
                        m_undo_stack->push(new AddContactCommand(currentProject(), c, []() {}));
                    m_undo_stack->endMacro();
                });

        // Group entity ops — undoable.
        connect(win, &ContactManagerWindow::groupAddRequested, this,
                [this](const QString& name) {
                    if (currentProject())
                        m_undo_stack->push(new AddContactGroupCommand(currentProject(), name.toStdString()));
                });
        connect(win, &ContactManagerWindow::groupRenameRequested, this,
                [this](const QString& id, const QString& name) {
                    if (!currentProject()) return;
                    std::string old_name;
                    if (auto* g = currentProject()->findContactGroup(id.toStdString())) old_name = g->name;
                    m_undo_stack->push(new RenameContactGroupCommand(
                        currentProject(), id.toStdString(), old_name, name.toStdString()));
                });
        connect(win, &ContactManagerWindow::groupRemoveRequested, this,
                [this](const QString& id) {
                    if (currentProject())
                        m_undo_stack->push(new RemoveContactGroupCommand(currentProject(), id.toStdString()));
                });
        connect(win, &ContactManagerWindow::groupAddAndAssignRequested, this,
                [this](const QString& name, const QVector<uint64_t>& ids) {
                    if (!currentProject()) return;
                    m_undo_stack->beginMacro(tr("New Group"));
                    auto* add = new AddContactGroupCommand(currentProject(), name.toStdString());
                    m_undo_stack->push(add);
                    const std::string gid = add->groupId();
                    if (!gid.empty())
                        for (uint64_t id : ids)
                            for (const auto& c : currentProject()->contacts())
                                if (c.id == id) {
                                    core::Contact after = c; after.group_id = gid;
                                    m_undo_stack->push(new UpdateContactCommand(currentProject(), c, after));
                                    break;
                                }
                    m_undo_stack->endMacro();
                });

        // Keep the list live: project-scoped signals route through the stable bus, so
        // these survive project load/replace. Context = win, so they die with it.
        if (m_event_bus) {
            // Reactive: the window mirrors project state from these signals, so any
            // contact add/remove/edit/group change (from here OR anywhere else)
            // refreshes it — no manual refresh() calls inside the window.
            connect(m_event_bus, &ProjectEventBus::contactAdded, win,
                    [win](const core::Contact&) { win->refresh(); });
            connect(m_event_bus, &ProjectEventBus::contactRemoved, win,
                    [win](uint64_t) { win->refresh(); });
            connect(m_event_bus, &ProjectEventBus::contactUpdated, win,
                    [win](uint64_t) { win->refresh(); });
            connect(m_event_bus, &ProjectEventBus::contactGroupsChanged, win,
                    [win]() { win->refresh(); });
            connect(m_event_bus, &ProjectEventBus::recycleBinChanged, win,
                    [win]() { win->refresh(); });
            connect(m_event_bus, &ProjectEventBus::projectReplaced, win,
                    [win](app::Project* p) { win->setProject(p); });
        }
    }

    static_cast<ContactManagerWindow*>(m_contact_mgr_win.data())->setProject(currentProject());
    m_contact_mgr_win->show();
    m_contact_mgr_win->raise();
    m_contact_mgr_win->activateWindow();
}

} // namespace dolphin::ui
