// Stage 04 regression tests for ImportClassifier and import batch deduplication.
//
// Covers:
//   - classifyImportAction returns ImportNew for null project
//   - classifyImportAction returns ImportNew for unknown source path
//   - classifyImportAction returns ReuseExisting when a usable DLPD cache exists
//   - classifyImportAction returns RebuildExisting when cache is missing/stale/incomplete
//   - classifyImportAction prefers pipeline_applied layer when multiple layers share a source
//   - ImportJobManager::importBatch deduplicates repeated paths within one batch
//
// No external test framework — minimal CHECK helper defined below.

#include "app/import/ImportClassifier.h"
#include "app/import/ImportJobManager.h"
#include "app/services/ImportService.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/layers/LayerUtils.h"
#include "io/cache/ParsedCache.h"
#include "core/Artifact.h"
#include "core/ArtifactIndex.h"
#include "core/SpatialRef.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

// ---------------------------------------------------------------------------
// Assertion helpers
// ---------------------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* expr, const char* file, int line)
{
    if (cond) { ++g_pass; }
    else { ++g_fail; std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expr); }
}
#define CHECK(x) check((x), #x, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {
using namespace dolphin;

// Write a minimal but genuinely reusable DLPD containing one sidescan record.
static bool writeValidDlpd(const std::string& path,
                           core::ArtifactIndex* written_index = nullptr)
{
    core::SidescanPing ping;
    ping.id           = 1;
    ping.timestamp_us = 1'000'000;
    ping.nav.valid    = true;
    ping.nav.lat      = 48.0;
    ping.nav.lon      = -52.0;
    ping.samples.push_back({1000, 1.0f});

    core::ArtifactIndex out;
    const std::vector<core::Artifact> artifacts{core::Artifact{std::move(ping)}};
    const bool wrote = io::writeArtifactBufferToCache(path, artifacts, {}, out);
    if (written_index) *written_index = out;
    return wrote && io::parsedCacheIsValid(path);
}

// RAII temp file.
struct TempFile {
    std::string path;
    explicit TempFile(const std::string& suffix = ".dlpd") {
        path = (std::filesystem::temp_directory_path() /
                ("dltest_" + std::to_string(static_cast<uint64_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count()))
                 + suffix)).string();
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(path), ec);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// 1 — null project → ImportNew
// ---------------------------------------------------------------------------

static void testClassifyNullProject()
{
    using Kind = dolphin::app::FileImportAction::Kind;
    const auto result = dolphin::app::classifyImportAction("/some/file.xtf", nullptr);
    CHECK(result.kind == Kind::ImportNew);
    CHECK(result.existing_layer_id.empty());
    CHECK(result.existing_source_id.empty());
}

// ---------------------------------------------------------------------------
// 2 — source not in project → ImportNew
// ---------------------------------------------------------------------------

static void testClassifyUnknownSource()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { ++g_fail; return; }

    const std::string manifest = (tmp.path() + "/proj.dlp").toStdString();
    auto proj = dolphin::app::Project::create("TestProj", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    // Add a different source — not the one we'll classify.
    proj->addSource("/other/file.xtf", "xtf");

    using Kind = dolphin::app::FileImportAction::Kind;
    const auto result = dolphin::app::classifyImportAction("/target/file.xtf", proj.get());
    CHECK(result.kind == Kind::ImportNew);
}

// ---------------------------------------------------------------------------
// 3 — valid DLPD cache → ReuseExisting
// ---------------------------------------------------------------------------

static void testClassifyReuseExisting()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { ++g_fail; return; }

    // Write a valid DLPD to a temp path.
    TempFile dlpd;
    const bool written = writeValidDlpd(dlpd.path);
    CHECK(written);
    if (!written) return;

    const std::string manifest = (tmp.path() + "/proj.dlp").toStdString();
    auto proj = dolphin::app::Project::create("TestProj", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    const std::string src_path = "/survey/line01.xtf";
    auto* src = proj->addSource(src_path, "xtf");
    CHECK(src != nullptr);

    auto* layer = proj->addLayer(src->id, "Line01");
    CHECK(layer != nullptr);
    if (!layer) return;
    layer->artifact_store_path   = dlpd.path;
    layer->artifact_store_format = "dlpd";
    layer->index_built           = true;
    proj->commitLayer(layer->id);

    using Kind = dolphin::app::FileImportAction::Kind;
    const auto result = dolphin::app::classifyImportAction(
        QString::fromStdString(src_path), proj.get());

    CHECK(result.kind == Kind::ReuseExisting);
    CHECK(result.existing_layer_id  == layer->id);
    CHECK(result.existing_source_id == src->id);
}

// ---------------------------------------------------------------------------
// 4 — stale / missing cache → RebuildExisting
// ---------------------------------------------------------------------------

static void testClassifyRebuildExisting()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { ++g_fail; return; }

    const std::string manifest = (tmp.path() + "/proj.dlp").toStdString();
    auto proj = dolphin::app::Project::create("TestProj", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    const std::string src_path = "/survey/line02.xtf";
    auto* src = proj->addSource(src_path, "xtf");
    auto* layer = proj->addLayer(src->id, "Line02");
    if (!layer) { ++g_fail; return; }
    // Point to a nonexistent DLPD — cache is missing.
    layer->artifact_store_path   = "/nonexistent/path/line02.dlpd";
    layer->artifact_store_format = "dlpd";
    layer->index_built           = false;
    proj->commitLayer(layer->id);

    using Kind = dolphin::app::FileImportAction::Kind;
    const auto result = dolphin::app::classifyImportAction(
        QString::fromStdString(src_path), proj.get());

    CHECK(result.kind == Kind::RebuildExisting);
    CHECK(result.existing_source_id == src->id);
}

// ---------------------------------------------------------------------------
// 5 — header-only/truncated cache → RebuildExisting
// ---------------------------------------------------------------------------

static void testClassifyRejectsIncompleteCache()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { ++g_fail; return; }

    TempFile dlpd;
    core::ArtifactIndex written_index;
    CHECK(writeValidDlpd(dlpd.path, &written_index));
    CHECK(written_index.size() == 1);
    if (written_index.empty()) return;

    // Keep the compatible file header but remove the record and footer.
    std::error_code ec;
    std::filesystem::resize_file(
        std::filesystem::path(dlpd.path),
        written_index.entries.front().file_offset, ec);
    CHECK(!ec);
    CHECK(!dolphin::io::parsedCacheIsValid(dlpd.path));

    const std::string manifest = (tmp.path() + "/proj.dlp").toStdString();
    auto proj = dolphin::app::Project::create("TestProj", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    const std::string src_path = "/survey/incomplete.xtf";
    auto* src = proj->addSource(src_path, "xtf");
    auto* layer = proj->addLayer(src->id, "Incomplete");
    if (!layer) { ++g_fail; return; }
    layer->artifact_store_path   = dlpd.path;
    layer->artifact_store_format = "dlpd";
    layer->index_built           = true;
    proj->commitLayer(layer->id);

    using Kind = dolphin::app::FileImportAction::Kind;
    const auto result = dolphin::app::classifyImportAction(
        QString::fromStdString(src_path), proj.get());
    CHECK(result.kind == Kind::RebuildExisting);
    CHECK(result.existing_source_id == src->id);
}

// ---------------------------------------------------------------------------
// 6 — multiple layers sharing source: prefer pipeline_applied=true
// ---------------------------------------------------------------------------

static void testClassifyBestLayerPrefersPipelined()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { ++g_fail; return; }

    TempFile dlpd_raw;
    TempFile dlpd_proc;
    if (!writeValidDlpd(dlpd_raw.path)) { ++g_fail; return; }
    if (!writeValidDlpd(dlpd_proc.path)) { ++g_fail; return; }

    const std::string manifest = (tmp.path() + "/proj.dlp").toStdString();
    auto proj = dolphin::app::Project::create("TestProj", manifest);
    if (!proj) { ++g_fail; return; }

    const std::string src_path = "/survey/dual.xtf";
    auto* src = proj->addSource(src_path, "xtf");

    // Raw layer — valid cache, pipeline not applied.
    auto* raw = proj->addLayer(src->id, "DualRaw");
    if (!raw) { ++g_fail; return; }
    raw->artifact_store_path   = dlpd_raw.path;
    raw->artifact_store_format = "dlpd";
    raw->index_built           = true;
    raw->pipeline_applied      = false;
    proj->commitLayer(raw->id);

    // Processed layer — valid cache, pipeline applied.
    auto* proc = proj->addLayer(src->id, "DualProc");
    if (!proc) { ++g_fail; return; }
    proc->artifact_store_path   = dlpd_proc.path;
    proc->artifact_store_format = "dlpd";
    proc->index_built            = true;
    proc->pipeline_applied       = true;
    proj->commitLayer(proc->id);

    using Kind = dolphin::app::FileImportAction::Kind;
    const auto result = dolphin::app::classifyImportAction(
        QString::fromStdString(src_path), proj.get());

    CHECK(result.kind == Kind::ReuseExisting);
    // Must pick the pipeline_applied layer.
    CHECK(result.existing_layer_id == proc->id);
}

// ---------------------------------------------------------------------------
// 7 — modality-aware reuse: mixed source imported as SSS, now requesting SBP
//     must NOT report ReuseExisting (the SBP layer doesn't exist yet).
// ---------------------------------------------------------------------------

static void testClassifyModalityAware()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { ++g_fail; return; }

    TempFile dlpd;
    if (!writeValidDlpd(dlpd.path)) { ++g_fail; return; }

    const std::string manifest = (tmp.path() + "/proj.dlp").toStdString();
    auto proj = dolphin::app::Project::create("TestProj", manifest);
    if (!proj) { ++g_fail; return; }

    const std::string src_path = "/survey/mixed.xtf";
    auto* src = proj->addSource(src_path, "xtf");
    auto* sss = proj->addLayer(src->id, "Mixed [SSS]");
    if (!sss) { ++g_fail; return; }
    sss->artifact_store_path   = dlpd.path;
    sss->artifact_store_format = "dlpd";
    sss->index_built           = true;
    sss->modality              = dolphin::app::Modality::Sidescan;
    proj->commitLayer(sss->id);

    using Kind = dolphin::app::FileImportAction::Kind;
    using AT   = dolphin::core::ArtifactType;

    // Requesting SBP from a source that has only an SSS layer (valid cache) must
    // create the SBP layer — i.e. ImportNew, NOT ReuseExisting.
    const auto sbp = dolphin::app::classifyImportAction(
        QString::fromStdString(src_path), proj.get(), { AT::SubBottom });
    CHECK(sbp.kind == Kind::ImportNew);
    CHECK(sbp.existing_source_id == src->id);

    // Requesting the modality that already exists (SSS, valid cache) still reuses.
    const auto sss_again = dolphin::app::classifyImportAction(
        QString::fromStdString(src_path), proj.get(), { AT::Sidescan });
    CHECK(sss_again.kind == Kind::ReuseExisting);
    CHECK(sss_again.existing_layer_id == sss->id);

    // Legacy whole-file request (no module filter) keeps source-level reuse.
    const auto legacy = dolphin::app::classifyImportAction(
        QString::fromStdString(src_path), proj.get());
    CHECK(legacy.kind == Kind::ReuseExisting);
}

// ---------------------------------------------------------------------------
// 8 — ImportJobManager: batch deduplication
// ---------------------------------------------------------------------------

static void testBatchDedup()
{
    app::ImportService svc;
    app::ImportJobManager mgr(&svc);

    QTemporaryDir tmp;
    if (!tmp.isValid()) { ++g_fail; return; }
    const std::string manifest = (tmp.path() + "/proj.dlp").toStdString();
    auto proj = dolphin::app::Project::create("TestProj", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;
    mgr.setProject(proj);

    // Two ReuseExisting actions with the same path — only one should be processed.
    // ReuseExisting emits jobCompleted without touching ImportService, so no I/O needed.
    int completed = 0;
    QObject::connect(&mgr, &app::ImportJobManager::jobCompleted,
                     [&](const std::string&) { ++completed; });

    app::FileImportAction a;
    a.path = "/survey/same.xtf";
    a.kind = app::FileImportAction::Kind::ReuseExisting;
    a.existing_layer_id = "layer1";

    mgr.importBatch({a, a});  // same path twice

    CHECK(completed == 1);  // dedup fired; second action suppressed
}

// ---------------------------------------------------------------------------
// 9 — cache-index rebuild keeps the logical ProjectSource ID
// ---------------------------------------------------------------------------

static void testRebuildCanonicalizesArtifactSourceId()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { ++g_fail; return; }

    const std::string store_path = (tmp.path() + "/source.dlpd").toStdString();
    CHECK(writeValidDlpd(store_path));

    const std::string manifest = (tmp.path() + "/proj.dlp").toStdString();
    auto project = dolphin::app::Project::create("RebuildIdentity", manifest);
    CHECK(project != nullptr);
    if (!project) return;

    auto* source = project->addSource("/survey/source.xtf", "xtf");
    CHECK(source != nullptr);
    if (!source) return;
    const std::string source_id = source->id;

    auto* layer = project->addLayer(source_id, "Source");
    CHECK(layer != nullptr);
    if (!layer) return;
    const std::string layer_id = layer->id;
    layer->artifact_store_path   = store_path;
    layer->artifact_store_format = "dlpd";
    layer->modality              = dolphin::app::Modality::Sidescan;
    layer->index_built           = false;
    layer->artifact_index.entries.clear();

    dolphin::app::ImportService service;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool rebuilt = false;
    bool failed = false;
    bool timed_out = false;
    QObject::connect(&service, &dolphin::app::ImportService::cacheIndexRebuilt,
                     &loop, [&](const std::string& completed_layer_id) {
        if (completed_layer_id != layer_id) return;
        rebuilt = true;
        loop.quit();
    });
    QObject::connect(&service, &dolphin::app::ImportService::indexingFailed,
                     &loop, [&](const std::string& failed_layer_id,
                                const std::string&) {
        if (failed_layer_id != layer_id) return;
        failed = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        timed_out = true;
        loop.quit();
    });

    timeout.start(5000);
    service.rebuildCacheIndex(layer_id, project);
    if (!rebuilt && !failed)
        loop.exec();
    timeout.stop();

    CHECK(!timed_out);
    CHECK(!failed);
    CHECK(rebuilt);
    auto* rebuilt_layer = project->findLayer(layer_id);
    CHECK(rebuilt_layer != nullptr);
    if (!rebuilt_layer) return;
    CHECK(rebuilt_layer->artifact_index.source_id == source_id);
    CHECK(!rebuilt_layer->artifact_index.empty());
    CHECK(project->save());

    project.reset();
    auto reopened = dolphin::app::Project::open(manifest);
    CHECK(reopened != nullptr);
    if (reopened) {
        auto* reopened_layer = reopened->findLayer(layer_id);
        CHECK(reopened_layer != nullptr);
        if (reopened_layer)
            CHECK(reopened_layer->artifact_index.source_id == source_id);
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testClassifyNullProject();
    testClassifyUnknownSource();
    testClassifyReuseExisting();
    testClassifyRebuildExisting();
    testClassifyRejectsIncompleteCache();
    testClassifyBestLayerPrefersPipelined();
    testClassifyModalityAware();
    testBatchDedup();
    testRebuildCanonicalizesArtifactSourceId();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
