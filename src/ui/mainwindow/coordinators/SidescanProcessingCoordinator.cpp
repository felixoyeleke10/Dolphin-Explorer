#include "ui/mainwindow/coordinators/SidescanProcessingCoordinator.h"

#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "ui/systems/DisplayStateManager.h"

#include <unordered_set>

namespace dolphin::ui {
namespace {

bool pipelineDiffers(const WaterfallParams& a, const WaterfallParams& b)
{
    return a.tvg          != b.tvg
        || a.agc          != b.agc
        || a.arn          != b.arn
        || a.destripe     != b.destripe
        || a.beam_pattern != b.beam_pattern
        || a.arc          != b.arc
        || a.ml_enhance   != b.ml_enhance;
}

bool navDiffers(const NavProcessingParams& a, const NavProcessingParams& b)
{
    return a.smooth_enabled     != b.smooth_enabled
        || a.smooth_window      != b.smooth_window
        || a.layback_enabled    != b.layback_enabled
        || a.layback_m          != b.layback_m
        || a.heading_offset_deg != b.heading_offset_deg
        || a.pitch_offset_deg   != b.pitch_offset_deg
        || a.roll_offset_deg    != b.roll_offset_deg;
}

} // namespace

SidescanProcessingCoordinator::SidescanProcessingCoordinator(
    DisplayStateManager* display_state, QObject* parent)
    : QObject(parent), m_display_state(display_state)
{}

std::vector<std::string> SidescanProcessingCoordinator::allSidescanLayerIds(
    const app::Project* project)
{
    std::vector<std::string> ids;
    if (!project) return ids;
    for (const auto& layer : project->layers())
        if (layer && layer->modality == app::Modality::Sidescan)
            ids.push_back(layer->id);
    return ids;
}

SidescanProcessingCoordinator::Result SidescanProcessingCoordinator::commit(
    app::Project* project,
    const std::vector<std::string>& requested_ids,
    const WaterfallParams& display,
    const NavProcessingParams* nav)
{
    Result result;
    if (!project || !m_display_state) return result;

    std::unordered_set<std::string> seen;
    QStringList committed;
    for (const auto& id : requested_ids) {
        if (id.empty() || !seen.insert(id).second) continue;
        auto* layer = project->findLayer(id);
        if (!layer || layer->modality != app::Modality::Sidescan) continue;

        result.pipeline_changed |= pipelineDiffers(layer->sss_display_state.params, display);
        result.geometry_changed |= layer->slant_range_corrected
                                != display.slant_range_correction;
        if (nav)
            result.nav_changed |= !layer->nav_customized
                               || navDiffers(layer->nav_state, *nav);

        m_display_state->setLayerSssDisplay(id, display);
        if (nav) m_display_state->setLayerNav(id, *nav);
        result.layer_ids.push_back(id);
        committed.push_back(QString::fromStdString(id));
    }

    if (!committed.isEmpty())
        emit processingCommitted(committed, result.pipeline_changed,
                                 result.geometry_changed, result.nav_changed);
    return result;
}

} // namespace dolphin::ui
