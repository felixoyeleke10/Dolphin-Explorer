#pragma once
#include "app/project/Project.h"

namespace dolphin::app {

// RAII guard that defers project::save() to the end of a multi-step operation.
//
// Usage:
//   {
//     ProjectTransaction tx(m_project.get());
//     /* mutate layers, sources, contacts … */
//     tx.commit();   // save() called once on destruction
//   }                // destructor: calls save() iff commit() was called
//
// If the scope exits without commit() (e.g. early return after validation
// failure), no save() is issued — the project remains at its last saved state.
class ProjectTransaction {
public:
    explicit ProjectTransaction(Project* project) noexcept
        : m_project(project) {}

    ~ProjectTransaction() { if (m_committed && m_project) m_project->save(); }

    ProjectTransaction(const ProjectTransaction&)            = delete;
    ProjectTransaction& operator=(const ProjectTransaction&) = delete;

    // Mark the transaction as successful — save() will run on destruction.
    void commit() noexcept { m_committed = true; }

    // Cancel the transaction — prevents save() on destruction.
    void rollback() noexcept { m_committed = false; }

private:
    Project* m_project;
    bool     m_committed = false;
};

} // namespace dolphin::app
