#pragma once
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/Contact.h"
#include "core/SpatialRef.h"
#include "ui/features/waterfall/NavProcessingParams.h"
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
        if (m_on_applied) m_on_applied();
    }

    void undo() override {
        if (m_assigned_id) m_project->removeContact(m_assigned_id);
        if (m_on_applied) m_on_applied();
    }

private:
    app::Project*         m_project;
    core::Contact         m_contact;
    uint64_t              m_assigned_id = 0;
    std::function<void()> m_on_applied;
};

// Undoable contact deletion.  Stores the full contact so it can be re-added on undo.
// The id will be re-assigned by the project on each re-add — callers must not cache
// the original id across an undo/redo cycle.
class RemoveContactCommand final : public QUndoCommand {
public:
    RemoveContactCommand(app::Project*         project,
                         core::Contact         contact,
                         std::function<void()> on_applied)
        : QUndoCommand(QStringLiteral("Remove Contact"))
        , m_project(project)
        , m_contact(std::move(contact))
        , m_on_applied(std::move(on_applied))
    {}

    void redo() override {
        m_project->removeContact(m_contact.id);
        if (m_on_applied) m_on_applied();
    }

    void undo() override {
        const size_t prev_sz = m_project->contacts().size();
        m_project->addContact(m_contact);
        const auto& after = m_project->contacts();
        for (auto it = after.begin() + static_cast<ptrdiff_t>(prev_sz); it != after.end(); ++it)
            m_contact.id = it->id;
        if (m_contact.id == 0 && !after.empty())
            m_contact.id = after.back().id;
        if (m_on_applied) m_on_applied();
    }

private:
    app::Project*         m_project;
    core::Contact         m_contact;
    std::function<void()> m_on_applied;
};

// Undoable bulk contact clear.  Stores all contacts before removal so they can
// be restored on undo.  Note: ids are re-assigned by the project on each re-add,
// so m_contacts is updated in-place after each undo to keep redo ids consistent.
class ClearContactsCommand final : public QUndoCommand {
public:
    ClearContactsCommand(app::Project*               project,
                         std::vector<core::Contact>  contacts,
                         std::function<void()>       on_applied)
        : QUndoCommand(QStringLiteral("Clear Contacts"))
        , m_project(project)
        , m_contacts(std::move(contacts))
        , m_on_applied(std::move(on_applied))
    {}

    void redo() override {
        for (const auto& c : m_contacts)
            m_project->removeContact(c.id);
        if (m_on_applied) m_on_applied();
    }

    void undo() override {
        for (auto& c : m_contacts) {
            const size_t prev_sz = m_project->contacts().size();
            m_project->addContact(c);
            const auto& after = m_project->contacts();
            for (auto it = after.begin() + static_cast<ptrdiff_t>(prev_sz); it != after.end(); ++it)
                c.id = it->id;
            if (c.id == 0 && !after.empty())
                c.id = after.back().id;
        }
        if (m_on_applied) m_on_applied();
    }

private:
    app::Project*              m_project;
    std::vector<core::Contact> m_contacts;
    std::function<void()>      m_on_applied;
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
