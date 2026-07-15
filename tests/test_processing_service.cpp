#include "app/services/ProcessingService.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

static int g_pass = 0;
static int g_fail = 0;

static void check(bool condition, const char* expression, const char* file, int line)
{
    if (condition) {
        ++g_pass;
    } else {
        ++g_fail;
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expression);
    }
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    CHECK(temp.isValid());

    const std::string manifest = temp.filePath(QStringLiteral("processing.dlp")).toStdString();
    auto project = dolphin::app::Project::create("processing", manifest);
    CHECK(project != nullptr);
    if (!project) return 1;

    const std::string missing_store =
        temp.filePath(QStringLiteral("missing.dlpd")).toStdString();
    auto* source = project->addSource(missing_store, "dlpd");
    auto* layer = source ? project->addLayer(source->id, "Line 1") : nullptr;
    CHECK(source != nullptr);
    CHECK(layer != nullptr);
    if (!source || !layer) return 1;

    layer->index_built = true;
    layer->artifact_store_path = missing_store;
    layer->artifact_store_format = "dlpd";

    dolphin::app::ProcessingService service;
    int started = 0;
    int failures = 0;
    int batch_total = -1;
    QEventLoop batch_loop;
    QObject::connect(&service, &dolphin::app::ProcessingService::runStarted,
                     &app, [&](const std::string&) { ++started; });
    QObject::connect(&service, &dolphin::app::ProcessingService::runFailed,
                     &app, [&](const std::string&, const std::string&) { ++failures; });
    QObject::connect(&service, &dolphin::app::ProcessingService::batchComplete,
                     &app, [&](int, int total) {
                         batch_total = total;
                         batch_loop.quit();
                     });

    service.runAll(*project);
    CHECK(started == 1);

    // runAll reserves the path synchronously, so a same-path single run must
    // fail before it can launch a competing writer.
    service.runLayer(*project, layer, source->path);
    CHECK(started == 1);
    CHECK(failures == 1);

    bool batch_timeout = false;
    QTimer::singleShot(5000, &batch_loop, [&] {
        batch_timeout = true;
        batch_loop.quit();
    });
    batch_loop.exec();
    CHECK(!batch_timeout);
    CHECK(batch_total == 1);
    CHECK(failures == 2); // collision plus the batch's missing-store failure

    // Completion releases the reservation; a later single run may start.
    QEventLoop single_loop;
    bool single_timeout = false;
    QObject::connect(&service, &dolphin::app::ProcessingService::runFailed,
                     &single_loop, [&](const std::string&, const std::string&) {
                         if (failures >= 3) single_loop.quit();
                     });
    service.runLayer(*project, layer, source->path);
    CHECK(started == 2);
    QTimer::singleShot(5000, &single_loop, [&] {
        single_timeout = true;
        single_loop.quit();
    });
    single_loop.exec();
    CHECK(!single_timeout);
    CHECK(failures == 3);

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
