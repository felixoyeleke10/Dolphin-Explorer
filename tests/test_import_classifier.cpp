// Stage 04 regression tests for ImportClassifier and import batch deduplication.
//
// Covers:
//   - classifyImportAction returns ImportNew for null project
//   - classifyImportAction returns ImportNew for unknown source path
//   - classifyImportAction returns ReuseExisting when a valid DLPD cache exists
//   - classifyImportAction returns RebuildExisting when cache is missing/stale
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
#include "io/IFormatReader.h"
#include "core/Artifact.h"
#include "core/ArtifactIndex.h"
#include "core/SpatialRef.h"

#include <QCoreApplication>
#include <QTemporaryDir>

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

// Minimal IFormatReader that serves nothing (for writing header-only DLPDs).
class NullReader : public io::IFormatReader {
public:
    bool open(const std::string&) override { return true; }
    void close() override {}
    bool isOpen() const override { return true; }
    std::string formatName() const override { return "NULL"; }
    io::FormatMeta metadata() override { return {}; }
    core::ArtifactIndex buildIndex(io::ProgressFn) override { return {}; }
    std::optional<core::Artifact> readArtifact(const core::ArtifactIndexEntry&) override
    { return std::nullopt; }
};

// Write a minimal valid DLPD (header only — no records).
// parsedCacheIsValid returns true for this file.
static bool writeDummyDlpd(const std::string& path)
{
    NullReader reader;
    core::ArtifactIndex src, out;
    // writeParsedCache with empty source_index writes the file header and returns
    // false (empty cache), but the resulting file passes parsedCacheIsValid.
    io::writeParsedCache(path, src, reader, out);
    return io::parsedCacheIsValid(path);
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
    const bool written = writeDummyDlpd(dlpd.path);
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
// 5 — multiple layers sharing source: prefer pipeline_applied=true
// ---------------------------------------------------------------------------

static void testClassifyBestLayerPrefersPipelined()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { ++g_fail; return; }

    TempFile dlpd_raw;
    TempFile dlpd_proc;
    if (!writeDummyDlpd(dlpd_raw.path)) { ++g_fail; return; }
    if (!writeDummyDlpd(dlpd_proc.path)) { ++g_fail; return; }

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
// 6 — modality-aware reuse: mixed source imported as SSS, now requesting SBP
//     must NOT report ReuseExisting (the SBP layer doesn't exist yet).
// ---------------------------------------------------------------------------

static void testClassifyModalityAware()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { ++g_fail; return; }

    TempFile dlpd;
    if (!writeDummyDlpd(dlpd.path)) { ++g_fail; return; }

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
// 7 — ImportJobManager: batch deduplication
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
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testClassifyNullProject();
    testClassifyUnknownSource();
    testClassifyReuseExisting();
    testClassifyRebuildExisting();
    testClassifyBestLayerPrefersPipelined();
    testClassifyModalityAware();
    testBatchDedup();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
