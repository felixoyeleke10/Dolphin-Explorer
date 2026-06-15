#pragma once
#include "app/project/Project.h"   // QPointer<Project> requires complete type
#include "core/Contact.h"
#include <QObject>
#include <QPointer>
#include <string>

namespace dolphin::app {
class DataLayer;
}

namespace dolphin::ui {

// Routes project-scoped signals through a stable relay so UI components
// connect once at startup rather than rewiring on every project load/replace.
class ProjectEventBus : public QObject {
    Q_OBJECT
public:
    explicit ProjectEventBus(QObject* parent = nullptr);

    void setProject(app::Project* project);
    app::Project* project() const { return m_project; }

signals:
    void projectReplaced(app::Project* project);
    void layerPending(app::DataLayer* layer);
    void layerReady(app::DataLayer* layer);
    void layerRemoved(const std::string& id);
    void layersReordered();
    void contactAdded(const core::Contact& c);
    void contactRemoved(uint64_t id);
    void projectModified();
    void layerDataChanged(const std::string& layer_id);

public:
    void postLayerDataChanged(const std::string& layer_id);
    void postProjectModified();

private:
    QPointer<app::Project> m_project;
};

} // namespace dolphin::ui
