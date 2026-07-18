#include "ui/mainwindow/coordinators/SidescanProcessingCoordinator.h"
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
    CHECK(line2->sss_display_state.customized);
    CHECK(line2->slant_range_corrected);
    CHECK(sbp->modality == dolphin::app::Modality::SubBottom);

    std::printf("SidescanProcessingCoordinator: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
