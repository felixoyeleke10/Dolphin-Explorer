// LayerDisplayCoordinator.cpp — active-layer identity + back/forward nav stack.
#include "ui/mainwindow/LayerDisplayCoordinator.h"
#include "ui/mainwindow/ProjectSessionController.h"
#include "app/project/Project.h"

#include <algorithm>
#include <unordered_set>

namespace dolphin::ui {

LayerDisplayCoordinator::LayerDisplayCoordinator(ProjectSessionController* psc,
                                                 QObject* parent)
    : QObject(parent), m_psc(psc)
{}

// -- Private helpers ---------------------------------------------------------

app::Project* LayerDisplayCoordinator::project() const
{
    return m_psc ? m_psc->project().get() : nullptr;
}

void LayerDisplayCoordinator::emitNavigationChanged()
{
    const bool has_proj = project() != nullptr;
    const bool back  = has_proj && m_navigation_index > 0;
    const bool fwd   = has_proj
                       && m_navigation_index >= 0
                       && m_navigation_index + 1
                              < static_cast<int>(m_navigation_history.size());
    emit navigationChanged(back, fwd);
}

// -- Active layer ------------------------------------------------------------

void LayerDisplayCoordinator::setActiveLayer(const std::string& id)
{
    m_active_layer_id = id;
}

void LayerDisplayCoordinator::clearActiveLayer()
{
    m_active_layer_id.clear();
}

// -- History -----------------------------------------------------------------

void LayerDisplayCoordinator::recordSelection(const std::string& layer_id)
{
    if (!project() || layer_id.empty() || !project()->findLayer(layer_id))
        return;

    // No-op if the top of stack is already this layer.
    if (m_navigation_index >= 0
            && m_navigation_index < static_cast<int>(m_navigation_history.size())
            && m_navigation_history[static_cast<size_t>(m_navigation_index)] == layer_id)
        return;

    // Truncate forward branch on a new user selection.
    if (m_navigation_index + 1 < static_cast<int>(m_navigation_history.size())) {
        m_navigation_history.erase(
            m_navigation_history.begin() + m_navigation_index + 1,
            m_navigation_history.end());
    }

    m_navigation_history.push_back(layer_id);

    if (static_cast<int>(m_navigation_history.size()) > kNavHistoryLimit) {
        const int excess = static_cast<int>(m_navigation_history.size()) - kNavHistoryLimit;
        m_navigation_history.erase(
            m_navigation_history.begin(),
            m_navigation_history.begin() + excess);
        m_navigation_index = std::max(0, m_navigation_index - excess);
    }

    m_navigation_index = static_cast<int>(m_navigation_history.size()) - 1;
    emitNavigationChanged();
}

void LayerDisplayCoordinator::pruneHistory(const app::Project* proj)
{
    if (!proj) {
        clearHistory();
        return;
    }

    std::unordered_set<std::string> live;
    for (const auto& l : proj->layers())
        live.insert(l->id);

    std::vector<std::string> kept;
    kept.reserve(m_navigation_history.size());
    for (const auto& id : m_navigation_history) {
        if (live.count(id)) {
            if (kept.empty() || kept.back() != id)
                kept.push_back(id);
        }
    }

    m_navigation_history = std::move(kept);
    if (m_navigation_history.empty()) {
        m_navigation_index = -1;
    } else {
        m_navigation_index = std::clamp(
            m_navigation_index, 0,
            static_cast<int>(m_navigation_history.size()) - 1);
    }
    emitNavigationChanged();
}

void LayerDisplayCoordinator::clearHistory()
{
    m_navigation_history.clear();
    m_navigation_index = -1;
    // Do not touch m_replaying: clearHistory can be called mid-replay (e.g.
    // project close triggered from within onLayerSelected).
    emitNavigationChanged();
}

// -- Navigation slots --------------------------------------------------------

void LayerDisplayCoordinator::navigateBack()
{
    if (!project() || m_navigation_index <= 0) {
        emitNavigationChanged();
        return;
    }

    --m_navigation_index;
    const std::string id = m_navigation_history[static_cast<size_t>(m_navigation_index)];
    m_replaying = true;
    emit layerActivationRequested(id);
    m_replaying = false;
    emitNavigationChanged();
}

void LayerDisplayCoordinator::navigateForward()
{
    if (!project()
            || m_navigation_index < 0
            || m_navigation_index + 1 >= static_cast<int>(m_navigation_history.size())) {
        emitNavigationChanged();
        return;
    }

    ++m_navigation_index;
    const std::string id = m_navigation_history[static_cast<size_t>(m_navigation_index)];
    m_replaying = true;
    emit layerActivationRequested(id);
    m_replaying = false;
    emitNavigationChanged();
}

} // namespace dolphin::ui
