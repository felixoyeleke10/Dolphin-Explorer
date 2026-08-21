#include "ui/mainwindow/coordinators/SurveyDisplayCoordinator.h"

#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/sidescan/SidescanViewController.h"

namespace dolphin::ui {

SurveyDisplayCoordinator::SurveyDisplayCoordinator(
    SidescanViewController* sidescan, MapView* map, QObject* parent)
    : QObject(parent), m_sidescan(sidescan), m_map(map)
{
}

SurveyDisplayCoordinator::Presentation
SurveyDisplayCoordinator::presentationFor(const std::string& active_layer_id,
                                           const std::string& imported_layer_id)
{
    return active_layer_id.empty() || active_layer_id == imported_layer_id
        ? Presentation::Select : Presentation::Background;
}

bool SurveyDisplayCoordinator::materializeImportedSidescan(
    app::Project* project, const std::string& layer_id,
    const std::string& active_layer_id)
{
    if (!project || !m_sidescan) return false;
    const auto* layer = project->findLayer(layer_id);
    if (!layer || layer->modality != app::Modality::Sidescan) return false;

    m_sidescan->unloadLayer(layer_id);
    m_sidescan->showNavTrackFromIndex(layer_id, project);

    if (presentationFor(active_layer_id, layer_id) == Presentation::Select) {
        emit selectionRequested(layer_id);
    } else {
        m_sidescan->activateLayer(layer_id, project,
                                  /*as_active=*/false,
                                  /*cache_only=*/false);
    }
    return true;
}

void SurveyDisplayCoordinator::completeImportBatch(int materialized_line_count)
{
    if (materialized_line_count < 2 || !m_map) return;
    m_map->requestFrameSurvey();
    m_map->fitToDataAndReset();
}

} // namespace dolphin::ui
