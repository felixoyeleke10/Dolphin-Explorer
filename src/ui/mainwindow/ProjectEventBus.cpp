#include "ui/mainwindow/ProjectEventBus.h"
#include "app/project/Project.h"

namespace dolphin::ui {

ProjectEventBus::ProjectEventBus(QObject* parent)
    : QObject(parent)
{}

void ProjectEventBus::setProject(app::Project* project)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);

    m_project = project;

    if (m_project) {
        connect(m_project, &app::Project::layerPending,
                this, &ProjectEventBus::layerPending);
        connect(m_project, &app::Project::layerReady,
                this, &ProjectEventBus::layerReady);
        connect(m_project, &app::Project::layerRemoved,
                this, &ProjectEventBus::layerRemoved);
        connect(m_project, &app::Project::layersReordered,
                this, &ProjectEventBus::layersReordered);
        connect(m_project, &app::Project::contactAdded,
                this, &ProjectEventBus::contactAdded);
        connect(m_project, &app::Project::contactRemoved,
                this, &ProjectEventBus::contactRemoved);
        connect(m_project, &app::Project::modified,
                this, &ProjectEventBus::projectModified);
    }

    emit projectReplaced(m_project);
}

void ProjectEventBus::postLayerDataChanged(const std::string& layer_id)
{
    emit layerDataChanged(layer_id);
}

} // namespace dolphin::ui
