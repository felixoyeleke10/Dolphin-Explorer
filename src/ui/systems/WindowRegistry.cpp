#include "ui/systems/WindowRegistry.h"
#include <QWidget>
#include <algorithm>

namespace dolphin::ui {

WindowRegistry::WindowRegistry(QObject* parent)
    : QObject(parent)
{}

void WindowRegistry::registerViewer(QWidget* host, IViewerWindow* impl)
{
    m_viewers.erase(
        std::remove_if(m_viewers.begin(), m_viewers.end(),
                       [](const Entry& e) { return e.host.isNull(); }),
        m_viewers.end());

    m_viewers.push_back({QPointer<QWidget>(host), impl});
}

void WindowRegistry::broadcast(ViewerRefreshReason reason,
                                const std::string&  layer_id)
{
    for (auto it = m_viewers.begin(); it != m_viewers.end(); ) {
        if (it->host.isNull()) {
            it = m_viewers.erase(it);
            continue;
        }
        it->impl->onViewerRefresh(reason, layer_id);
        ++it;
    }
}

bool WindowRegistry::anyViewerBusy() const
{
    for (const auto& e : m_viewers) {
        if (e.host.isNull()) continue;
        const auto s = e.impl->viewerDataState();
        if (s == ViewerDataState::Loading || s == ViewerDataState::Processing)
            return true;
    }
    return false;
}

} // namespace dolphin::ui
