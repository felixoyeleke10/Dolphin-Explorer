#pragma once
#include "core/Contact.h"
#include <QObject>
#include <string>

namespace dolphin::app {
class DataLayer;
class Project;
}

namespace dolphin::ui {

// Routes project-scoped signals through a stable relay so UI components
// connect once at startup rather than rewiring on every project load/replace.
//
// Call setProject(p) whenever the active project changes; the bus disconnects
// the old project, subscribes to the new one, then emits projectReplaced().
// All subscribers automatically receive events from the new project.
class ProjectEventBus : public QObject {
    Q_OBJECT
public:
    explicit ProjectEventBus(QObject* parent = nullptr);

    // Switch the active project. Disconnects old, connects new, emits
    // projectReplaced(). Pass nullptr to clear (project closed).
    void setProject(app::Project* project);

    app::Project* project() const { return m_project; }

signals:
    // Emitted after setProject(); nullptr means the project was closed.
    void projectReplaced(app::Project* project);

    // Forwarded directly from Project signals.
    void layerPending(app::DataLayer* layer);
    void layerReady(app::DataLayer* layer);
    void layerRemoved(const std::string& id);
    void layersReordered();
    void contactAdded(const core::Contact& c);
    void contactRemoved(uint64_t id);
    void projectModified();

    // Emitted when a layer's on-disk data has changed and all open viewers
    // should reload that layer (import/reindex complete, processing finished).
    // Call postLayerDataChanged() from any system that mutates layer storage.
    void layerDataChanged(const std::string& layer_id);

public:
    // Post a layerDataChanged event for the given layer.
    // Routes through the bus so any subscriber gets notified without needing
    // a direct reference to WindowRegistry or individual viewer windows.
    void postLayerDataChanged(const std::string& layer_id);

private:
    app::Project* m_project = nullptr;
};

} // namespace dolphin::ui
