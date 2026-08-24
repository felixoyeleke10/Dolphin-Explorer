#include "ui/mainwindow/coordinators/SidescanProcessingCoordinator.h"
#include "app/display/WaterfallParams.h"
#include "ui/systems/AppState.h"
#include "ui/systems/DisplayStateManager.h"
#include "app/project/Project.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid());

    auto project = dolphin::app::Project::create(
        "sss-system", (dir.path() + "/sss-system.dlp").toStdString());
    CHECK(project != nullptr);
    auto* source = project->addSource("survey.xtf", "xtf");
    CHECK(source != nullptr);
    auto* line1 = project->addLayer(source->id, "Line 1");
    auto* line2 = project->addLayer(source->id, "Line 2");
    auto* sbp   = project->addLayer(source->id, "SBP");
    CHECK(line1 && line2 && sbp);
    line1->modality = dolphin::app::Modality::Sidescan;
    line2->modality = dolphin::app::Modality::Sidescan;
    sbp->modality   = dolphin::app::Modality::SubBottom;

    dolphin::ui::AppState app_state;
    dolphin::ui::DisplayStateManager display(&app_state);
    display.setProject(project.get());
    dolphin::ui::SidescanProcessingCoordinator coordinator(&display);

    int notifications = 0;
    int expected_notification_size = 1;
    QObject::connect(&coordinator,
        &dolphin::ui::SidescanProcessingCoordinator::processingCommitted,
        [&](const QStringList& ids, bool, bool, bool) {
            ++notifications;
            CHECK(ids.size() == expected_notification_size);
        });

    dolphin::ui::WaterfallParams params;
    params.tvg.enabled = true;
    params.tvg.spreading = 17.f;
    params.agc.enabled = true;
    params.agc.strength = 0.63f;
    params.slant_range_correction = true;
    dolphin::ui::NavProcessingParams nav;
    nav.smooth_enabled = true;
    nav.smooth_window = 9;

    const auto one = coordinator.commit(
        project.get(), {line1->id, line1->id, sbp->id}, params, &nav);
    CHECK(one.layer_ids.size() == 1);
    CHECK(one.pipeline_changed);
    CHECK(one.geometry_changed);
    CHECK(one.nav_changed);
    CHECK(notifications == 1);
    CHECK(line1->sss_display_state.customized);
    CHECK(line1->sss_display_state.params.tvg.enabled);
    CHECK(line1->sss_display_state.params.tvg.spreading == 17.f);
    CHECK(line1->sss_display_state.params.agc.enabled);
    CHECK(line1->sss_display_state.params.agc.strength == 0.63f);
    CHECK(line1->slant_range_corrected);
    CHECK(line1->nav_customized && line1->nav_state.smooth_window == 9);
    CHECK(!line2->sss_display_state.customized);
    CHECK(!sbp->sss_display_state.customized);

    const auto all_ids = dolphin::ui::SidescanProcessingCoordinator::allSidescanLayerIds(
        project.get());
    CHECK(all_ids.size() == 2);
    expected_notification_size = 2;
    const auto all = coordinator.commit(project.get(), all_ids, params, &nav);
    CHECK(all.layer_ids.size() == 2);
    CHECK(all.pipeline_changed_layer_ids.size() == 1);
    CHECK(all.pipeline_changed_layer_ids.front() == line2->id);
    CHECK(line2->sss_display_state.customized);
    CHECK(line2->slant_range_corrected);
    CHECK(sbp->modality == dolphin::app::Modality::SubBottom);

    // Applying an explicitly empty chain is a universal revert request for
    // every baked target. It must still persist the unchecked controls.
    line1->pipeline_applied = true;
    line1->processing_origin = dolphin::app::ProcessingOrigin::Waterfall;
    line2->pipeline_applied = true;
    line2->processing_origin = dolphin::app::ProcessingOrigin::NodeGraph;
    dolphin::ui::WaterfallParams none;
    const auto cleared = coordinator.commit(project.get(), all_ids, none, nullptr);
    CHECK(cleared.revert_layer_ids.size() == 2);
    CHECK(cleared.revert_layer_ids[0] == line1->id);
    CHECK(cleared.revert_layer_ids[1] == line2->id);
    CHECK(line1->sss_display_state.customized);
    CHECK(!line1->sss_display_state.params.tvg.enabled);
    CHECK(!line1->sss_display_state.params.agc.enabled);
    CHECK(!line1->slant_range_corrected);

    // Re-applying the exact same state must not schedule another mosaic build.
    const auto unchanged = coordinator.commit(project.get(), all_ids, none, nullptr);
    CHECK(!unchanged.pipeline_changed);
    CHECK(!unchanged.geometry_changed);
    CHECK(unchanged.pipeline_changed_layer_ids.empty());
    CHECK(unchanged.geometry_changed_layer_ids.empty());

    dolphin::ui::WaterfallParams display_only = none;
    display_only.gain = 1.25f;
    display_only.contrast = 1.1f;
    expected_notification_size = 1;
    const auto recolor = coordinator.commit(
        project.get(), {line1->id}, display_only, nullptr);
    CHECK(!recolor.pipeline_changed);
    CHECK(recolor.pipeline_changed_layer_ids.empty());
    CHECK(recolor.display_changed_layer_ids.size() == 1);
    CHECK(recolor.display_changed_layer_ids.front() == line1->id);

    // Every UI entry point consumes this one invalidation policy. Navigation
    // changes require geometry rebuilds, and reverted layers must be excluded.
    dolphin::ui::SidescanProcessingCoordinator::Result changed;
    changed.display_changed_layer_ids = {"appearance"};
    changed.pipeline_changed_layer_ids = {"amplitude"};
    changed.geometry_changed_layer_ids = {"geometry", "reverted"};
    changed.nav_changed_layer_ids = {"navigation"};
    changed.revert_layer_ids = {"reverted"};
    const auto invalidations =
        dolphin::ui::SidescanProcessingCoordinator::invalidationsFor(changed);
    CHECK(invalidations.size() == 4);
    CHECK(invalidations[0].id == "appearance");
    CHECK(invalidations[0].change == dolphin::ui::SidescanInvalidation::Appearance);
    CHECK(invalidations[1].change == dolphin::ui::SidescanInvalidation::Amplitude);
    CHECK(invalidations[2].id == "geometry");
    CHECK(invalidations[3].id == "navigation");
    CHECK(invalidations[3].change == dolphin::ui::SidescanInvalidation::Geometry);

    // Direct node-graph revert clears every scientific correction but preserves
    // harmless appearance choices, so baseline data cannot look processed.
    display_only.tvg.enabled = true;
    display_only.arc.enabled = true;
    display_only.arn.enabled = true;
    display_only.slant_range_correction = true;
    const auto appearance = dolphin::ui::withoutSidescanProcessing(display_only);
    CHECK(!dolphin::ui::hasSidescanProcessing(appearance));
    CHECK(appearance.gain == display_only.gain);
    CHECK(appearance.contrast == display_only.contrast);
    CHECK(appearance.display_channel == display_only.display_channel);

    std::printf("SidescanProcessingCoordinator: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
