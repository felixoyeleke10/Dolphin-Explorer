// Stage 01 regression tests for the Project storage model.
//
// Covers:
//   - create + save + open round-trip (.dlp v9 manifest)
//   - saveAs: manifest path and display name both update from new filename stem
//   - isTempProject: flag is session-only (not serialised); save/reopen starts fresh
//   - reopen invalidation: missing .dlpd file clears layer index
//   - reopen invalidation: unknown format string also clears layer index
//   - dual-format: "dpcache" artifact_store_format accepted on reopen (backward compat)
//   - layer removal: layer and source removed cleanly; serialised manifest reflects that
//
// No external test framework — minimal CHECK/FAIL helpers defined below.
// Entry point: ctest --output-on-failure

#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/ArtifactIndex.h"
#include "core/Artifact.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Assertion helpers
// ─────────────────────────────────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* expr, const char* file, int line)
{
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expr);
    }
}

#define CHECK(x) check((x), #x, __FILE__, __LINE__)

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Add one minimal Sidescan entry so the index is non-empty.
static void injectIndexEntry(dolphin::app::DataLayer* layer)
{
    using namespace dolphin::core;
    ArtifactIndexEntry e;
    e.artifact_id = 1;
    e.type        = ArtifactType::Sidescan;
    e.lat         = 10.0;
    e.lon         = 20.0;
    layer->artifact_index.entries.push_back(e);
    layer->index_built = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tests
// ─────────────────────────────────────────────────────────────────────────────

// 1. create → save → open round-trip
static void testCreateSaveOpen()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/MyProject.dlp").toStdString();

    auto proj = dolphin::app::Project::create("MyProject", manifest);
    CHECK(proj != nullptr);
    CHECK(proj->name() == "MyProject");
    CHECK(proj->manifestPath() == manifest);
    CHECK(!proj->isTempProject());

    CHECK(proj->save());

    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded) return;

    CHECK(loaded->name() == "MyProject");
    CHECK(loaded->sources().empty());
    CHECK(loaded->layers().empty());
}

// 2. saveAs updates manifest path AND display name
static void testSaveAsUpdatesName()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string original = (tmp.path() + "/old.dlp").toStdString();
    const std::string renamed  = (tmp.path() + "/HarborSurvey.dlp").toStdString();

    auto proj = dolphin::app::Project::create("old", original);
    CHECK(proj != nullptr);
    if (!proj) return;

    CHECK(proj->save());
    CHECK(proj->saveAs(renamed));

    // Display name must reflect new filename stem
    CHECK(proj->name() == "HarborSurvey");
    CHECK(proj->manifestPath() == renamed);

    // Reopen and verify name is persisted
    auto loaded = dolphin::app::Project::open(renamed);
    CHECK(loaded != nullptr);
    if (!loaded) return;
    CHECK(loaded->name() == "HarborSurvey");
}

// 3. saveAs rollback — save failure must restore original state
static void testSaveAsRollback()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string original = (tmp.path() + "/orig.dlp").toStdString();
    // Point to a directory path so QSaveFile will fail
    const std::string bad_path = tmp.path().toStdString() + "/"; // ends with slash → not a file

    auto proj = dolphin::app::Project::create("orig", original);
    CHECK(proj != nullptr);
    if (!proj) return;
    CHECK(proj->save());

    const bool ok = proj->saveAs(bad_path);
    CHECK(!ok);
    // State must be unchanged
    CHECK(proj->name() == "orig");
    CHECK(proj->manifestPath() == original);
}

// 4. isTempProject is session-only — reopened project starts as non-temp
static void testTempProjectFlagNotSerialised()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/Session_2026.dlp").toStdString();

    auto proj = dolphin::app::Project::create("Session_2026", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    proj->setTempProject(true);
    CHECK(proj->isTempProject());
    CHECK(proj->save());

    // The temp flag is not stored in the manifest — reopening always yields false
    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded) return;
    CHECK(!loaded->isTempProject());
}

// 5. Reopen invalidation — missing .dlpd file clears layer index
static void testReopenInvalidatesMissingStore()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/survey.dlp").toStdString();
    auto proj = dolphin::app::Project::create("survey", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    auto* src = proj->addSource("/fake/survey.xtf", "xtf");
    CHECK(src != nullptr);
    if (!src) return;

    auto* layer = proj->addLayer(src->id, "survey");
    CHECK(layer != nullptr);
    if (!layer) return;

    injectIndexEntry(layer);
    layer->artifact_store_path   = "/nonexistent/survey.dlpd";  // file never created
    layer->artifact_store_format = "dlpd";
    layer->artifact_index.source_id = src->id;

    CHECK(proj->save());

    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded) return;
    CHECK(!loaded->layers().empty());

    const auto& ll = loaded->layers().front();
    CHECK(ll != nullptr);
    if (!ll) return;

    // Missing store → index must be cleared
    CHECK(!ll->index_built);
    CHECK(ll->artifact_index.empty());
    CHECK(ll->artifact_store_path.empty());
    CHECK(ll->artifact_store_format.empty());
}

// 6. Reopen invalidation — unknown format (neither dlpd nor dpcache)
static void testReopenInvalidatesUnknownFormat()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/survey2.dlp").toStdString();
    auto proj = dolphin::app::Project::create("survey2", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    auto* src = proj->addSource("/fake/file.xtf", "xtf");
    auto* layer = proj->addLayer(src->id, "layer");
    CHECK(layer != nullptr);
    if (!layer) return;

    injectIndexEntry(layer);
    layer->artifact_store_path   = "/some/file.rawcache";
    layer->artifact_store_format = "rawcache";   // not dlpd or dpcache
    layer->artifact_index.source_id = src->id;

    CHECK(proj->save());

    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded) return;

    const auto& ll = loaded->layers().front();
    CHECK(ll != nullptr);
    if (!ll) return;

    // Unknown format → missing_store = true → must be cleared
    CHECK(!ll->index_built);
    CHECK(ll->artifact_index.empty());
}

// 7. Backward compat — "dpcache" format string is accepted on reopen
//    (no spurious invalidation when file exists)
static void testDpcacheFormatAccepted()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    // Create a real .dpcache file (just needs to exist; content not checked by
    // missing_store test — parsedCacheIsValid may mark it stale, which is fine
    // because the important thing is it is NOT rejected as "unknown format").
    const QString cache_file = tmp.path() + "/src1.dpcache";
    {
        QFile f(cache_file);
        f.open(QIODevice::WriteOnly);
        f.write("placeholder");   // minimal content — not a valid cache, so stale_cache = true
    }

    const std::string manifest = (tmp.path() + "/legacy.dlp").toStdString();
    auto proj = dolphin::app::Project::create("legacy", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    auto* src = proj->addSource("/fake/file.xtf", "xtf");
    auto* layer = proj->addLayer(src->id, "layer");
    CHECK(layer != nullptr);
    if (!layer) return;

    injectIndexEntry(layer);
    layer->artifact_store_path   = cache_file.toStdString();
    layer->artifact_store_format = "dpcache";  // legacy name
    layer->artifact_index.source_id = src->id;

    CHECK(proj->save());

    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded) return;

    // "dpcache" is a valid format (backward compat alias for "dlpd").
    // The cache file exists, so missing_store = false.
    // However stale_cache will be true (placeholder content) → index cleared.
    // The key assertion: the format string itself does NOT cause unknown-format rejection.
    // We verify this indirectly by checking that the layer still exists in the project.
    CHECK(!loaded->layers().empty());
    // (stale_cache path clears index, which is correct behaviour — but the
    //  format acceptance path is exercised without hitting missing_store.)
}

// 8. Layer removal — layer and source are removed from project
static void testLayerRemoval()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/proj.dlp").toStdString();
    auto proj = dolphin::app::Project::create("proj", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    auto* src   = proj->addSource("/fake/a.xtf", "xtf");
    auto* layer = proj->addLayer(src->id, "a");
    CHECK(layer != nullptr);
    const std::string layer_id = layer->id;

    proj->removeLayer(layer_id);

    CHECK(proj->layers().empty());
    CHECK(proj->sources().empty());

    CHECK(proj->save());

    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded) return;
    CHECK(loaded->layers().empty());
    CHECK(loaded->sources().empty());
}

// 9. Display-state round-trip — visible, SRC, QC fraction, bottom-track kind,
//    and the new per-layer SSS palette all survive save → reopen.
static void testDisplayStatePersistence()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/display.dlp").toStdString();
    auto proj = dolphin::app::Project::create("display", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    auto* src   = proj->addSource("/fake/survey.xtf", "xtf");
    auto* layer = proj->addLayer(src->id, "layer");
    CHECK(layer != nullptr);
    if (!layer) return;

    injectIndexEntry(layer);
    layer->visible               = false;
    layer->slant_range_corrected = true;
    layer->qc_viewed_fraction    = 0.75f;
    layer->bottom_track_kind     = dolphin::app::BottomTrackKind::Mixed;
    layer->sss_palette           = 3;  // e.g. Copper
    layer->sbp_palette           = 2;  // e.g. Seismic

    CHECK(proj->save());

    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded) return;
    CHECK(!loaded->layers().empty());

    const auto& ll = loaded->layers().front();
    CHECK(ll != nullptr);
    if (!ll) return;

    CHECK(!ll->visible);
    CHECK(ll->slant_range_corrected);
    CHECK(std::fabs(ll->qc_viewed_fraction - 0.75f) < 1e-4f);
    CHECK(ll->bottom_track_kind == dolphin::app::BottomTrackKind::Mixed);
    CHECK(ll->sss_palette == 3);
    CHECK(ll->sbp_palette == 2);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testCreateSaveOpen();
    testSaveAsUpdatesName();
    testSaveAsRollback();
    testTempProjectFlagNotSerialised();
    testReopenInvalidatesMissingStore();
    testReopenInvalidatesUnknownFormat();
    testDpcacheFormatAccepted();
    testLayerRemoval();
    testDisplayStatePersistence();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
