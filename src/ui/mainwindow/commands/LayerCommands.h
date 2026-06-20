#pragma once
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/Contact.h"
#include "core/SpatialRef.h"
#include "app/display/NavProcessingParams.h"
#include <QUndoCommand>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dolphin::ui {

// Undoable layer rename.  The caller supplies an on_applied callback that
// refreshes whatever UI depends on the label (tree, inspector, title bar).
class RenameLayerCommand final : public QUndoCommand {
public:
    RenameLayerCommand(app::Project*  project,
                       std::string    layer_id,
                       std::string    old_label,
                       std::string    new_label,
                       std::function<void(const std::string&)> on_applied)
        : QUndoCommand(QStringLiteral("Rename Layer"))
        , m_project(project)
        , m_layer_id(std::move(layer_id))
        , m_old_label(std::move(old_label))
        , m_new_label(std::move(new_label))
        , m_on_applied(std::move(on_applied))
    {}

    void redo() override { apply(m_new_label); }
    void undo() override { apply(m_old_label); }

private:
    void apply(const std::string& label) {
        if (auto* l = m_project->findLayer(m_layer_id))
            l->label = label;
        if (m_on_applied) m_on_applied(m_layer_id);
    }

    app::Project*                            m_project;
    std::string                              m_layer_id;
    std::string                              m_old_label;
    std::string                              m_new_label;
    std::function<void(const std::string&)>  m_on_applied;
};

// Undoable layer visibility toggle.  The caller supplies an on_applied callback
// that updates both the map views and the layer tree checkbox.
class SetLayerVisibleCommand final : public QUndoCommand {
public:
    SetLayerVisibleCommand(std::string  layer_id,
                           bool         old_visible,
                           bool         new_visible,
                           std::function<void(const std::string&, bool)> on_applied)
        : QUndoCommand(new_visible ? QStringLiteral("Show Layer")
                                   : QStringLiteral("Hide Layer"))
        , m_layer_id(std::move(layer_id))
        , m_old_visible(old_visible)
        , m_new_visible(new_visible)
        , m_on_applied(std::move(on_applied))
    {}

    void redo() override { if (m_on_applied) m_on_applied(m_layer_id, m_new_visible); }
    void undo() override { if (m_on_applied) m_on_applied(m_layer_id, m_old_visible); }

private:
    std::string                                       m_layer_id;
    bool                                              m_old_visible;
    bool                                              m_new_visible;
    std::function<void(const std::string&, bool)>     m_on_applied;
};

// Undoable contact placement.  Calls project->addContact on redo and
// project->removeContact on undo.  The on_applied callback marks the project
// dirty; UI refresh is driven by the project's contactAdded/contactRemoved signals.
class AddContactCommand final : public QUndoCommand {
public:
    AddContactCommand(app::Project*         project,
                      core::Contact         contact,
                      std::function<void()> on_applied)
        : QUndoCommand(QStringLiteral("Add Contact"))
        , m_project(project)
        , m_contact(std::move(contact))
        , m_on_applied(std::move(on_applied))
    {}

    void redo() override {
        const auto& before = m_project->contacts();
        const size_t prev_sz = before.size();
        m_project->addContact(m_contact);
        const auto& after = m_project->contacts();
        // Find the newly assigned ID: the contact that was not present before.
        for (auto it = after.begin() + static_cast<ptrdiff_t>(prev_sz); it != after.end(); ++it)
            m_assigned_id = it->id;
        if (m_assigned_id == 0 && !after.empty())
            m_assigned_id = after.back().id;
        // Capture the project-assigned default label so a redo after undo keeps the
        // same name (the contact was created with an empty label).
        if (m_contact.label.empty())
            for (const auto& ct : after)
                if (ct.id == m_assigned_id) { m_contact.label = ct.label; break; }
        if (m_on_applied) m_on_applied();
    }

    void undo() override {
        if (m_assigned_id) m_project->removeContact(m_assigned_id);
        if (m_on_applied) m_on_applied();
    }

    // The id the project assigned on the first redo() (0 before then).
    uint64_t assignedId() const { return m_assigned_id; }

private:
    app::Project*         m_project;
    core::Contact         m_contact;
    uint64_t              m_assigned_id = 0;
    std::function<void()> m_on_applied;
};

// Undoable soft-delete: moves a contact to the project recycle bin (redo) and
// restores it (undo). The contact id is stable across recycle/restore, so only
// the id is stored. Permanent removal ("Delete Forever") is a separate purge.
class RecycleContactCommand final : public QUndoCommand {
public:
    RecycleContactCommand(app::Project* project, uint64_t id)
        : QUndoCommand(QStringLiteral("Delete Contact"))
        , m_project(project)
        , m_id(id)
    {}

    void redo() override { m_project->recycleContact(m_id); }
    void undo() override { m_project->restoreContact(m_id); }

private:
    app::Project* m_project;
    uint64_t      m_id;
};

// Undoable edit of a single contact's fields (rename, favourite tag, group
// assignment — all of which are Project::updateContact). Stores the full before
// and after so redo/undo just re-apply the matching snapshot. The id is stable.
class UpdateContactCommand final : public QUndoCommand {
public:
    UpdateContactCommand(app::Project* project, core::Contact before, core::Contact after)
        : QUndoCommand(QStringLiteral("Edit Contact"))
        , m_project(project)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {}

    void redo() override { m_project->updateContact(m_after); }
    void undo() override { m_project->updateContact(m_before); }

private:
    app::Project* m_project;
    core::Contact m_before;
    core::Contact m_after;
};

// Undoable contact-group creation. Exposes the new id so a "create + assign"
// macro can attach contacts to the just-made group.
class AddContactGroupCommand final : public QUndoCommand {
public:
    AddContactGroupCommand(app::Project* project, std::string name)
        : QUndoCommand(QStringLiteral("New Group")), m_project(project), m_name(std::move(name)) {}

    void redo() override { if (auto* g = m_project->addContactGroup(m_name)) m_id = g->id; }
    void undo() override { if (!m_id.empty()) m_project->removeContactGroup(m_id); }
    const std::string& groupId() const { return m_id; }

private:
    app::Project* m_project;
    std::string   m_name;
    std::string   m_id;
};

// Undoable contact-group rename (id stable).
class RenameContactGroupCommand final : public QUndoCommand {
public:
    RenameContactGroupCommand(app::Project* project, std::string id,
                              std::string old_name, std::string new_name)
        : QUndoCommand(QStringLiteral("Rename Group"))
        , m_project(project), m_id(std::move(id))
        , m_old(std::move(old_name)), m_new(std::move(new_name)) {}

    void redo() override { m_project->renameContactGroup(m_id, m_new); }
    void undo() override { m_project->renameContactGroup(m_id, m_old); }

private:
    app::Project* m_project;
    std::string   m_id, m_old, m_new;
};

// Undoable contact-group deletion. Snapshots the group name + member contact ids
// (captured before redo); undo re-creates the group and re-assigns its members.
// The re-created group gets a fresh id, which is kept for any subsequent redo.
class RemoveContactGroupCommand final : public QUndoCommand {
public:
    RemoveContactGroupCommand(app::Project* project, std::string id)
        : QUndoCommand(QStringLiteral("Delete Group")), m_project(project), m_id(std::move(id))
    {
        if (auto* g = m_project->findContactGroup(m_id)) m_name = g->name;
        for (const auto& c : m_project->contacts())
            if (c.group_id == m_id) m_members.push_back(c.id);
    }

    void redo() override { m_project->removeContactGroup(m_id); }
    void undo() override {
        if (auto* g = m_project->addContactGroup(m_name)) {
            m_id = g->id;
            for (uint64_t cid : m_members) m_project->setContactGroup(cid, m_id);
        }
    }

private:
    app::Project*         m_project;
    std::string           m_id, m_name;
    std::vector<uint64_t> m_members;
};

// Undoable source CRS assignment.  The on_apply callback receives the SpatialRef to
// apply and is responsible for the full project-transaction + viewer reload sequence.
class SetSourceCrsCommand final : public QUndoCommand {
public:
    SetSourceCrsCommand(core::SpatialRef                              old_ref,
                        core::SpatialRef                              new_ref,
                        std::function<void(const core::SpatialRef&)> on_apply)
        : QUndoCommand(QStringLiteral("Set Source CRS"))
        , m_old_ref(std::move(old_ref))
        , m_new_ref(std::move(new_ref))
        , m_on_apply(std::move(on_apply))
    {}

    void redo() override { if (m_on_apply) m_on_apply(m_new_ref); }
    void undo() override { if (m_on_apply) m_on_apply(m_old_ref); }

private:
    core::SpatialRef                              m_old_ref;
    core::SpatialRef                              m_new_ref;
    std::function<void(const core::SpatialRef&)>  m_on_apply;
};

// Undoable bulk nav-parameter assignment across all sidescan layers.  Stores
// full snapshots of the per-layer param map before and after so undo/redo
// correctly handles layers that were absent from the map before the first apply.
class SetNavParamsAllCommand final : public QUndoCommand {
    using ParamMap = std::unordered_map<std::string, NavProcessingParams>;
    using ApplyFn  = std::function<void(const ParamMap&)>;
public:
    SetNavParamsAllCommand(ParamMap old_state, ParamMap new_state, ApplyFn on_apply)
        : QUndoCommand(QStringLiteral("Apply Nav Corrections"))
        , m_old_state(std::move(old_state))
        , m_new_state(std::move(new_state))
        , m_on_apply(std::move(on_apply))
    {}

    void redo() override { if (m_on_apply) m_on_apply(m_new_state); }
    void undo() override { if (m_on_apply) m_on_apply(m_old_state); }

private:
    ParamMap  m_old_state;
    ParamMap  m_new_state;
    ApplyFn   m_on_apply;
};

} // namespace dolphin::ui
