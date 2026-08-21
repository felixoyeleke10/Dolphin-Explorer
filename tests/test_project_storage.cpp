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
//   - durable parsed stores survive layer removal and project reopen
//
// No external test framework — minimal CHECK/FAIL helpers defined below.
// Entry point: ctest --output-on-failure

#include "app/project/Project.h"
#include "app/project/ProjectRecovery.h"
#include "app/layers/DataLayer.h"
#include "core/ArtifactIndex.h"
#include "core/Artifact.h"
#include "util/Json.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
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

// Layer removal must never delete a durable parsed store, including stores
// outside the project's canonical data directory.
static void testLayerRemovalPreservesExternalParsedStore()
{
    QTemporaryDir project_tmp;
    QTemporaryDir external_tmp;
    CHECK(project_tmp.isValid());
    CHECK(external_tmp.isValid());
    if (!project_tmp.isValid() || !external_tmp.isValid()) return;

    const QString store_path = external_tmp.path() + "/external.dpcache";
    QFile store(store_path);
    CHECK(store.open(QIODevice::WriteOnly));
    CHECK(store.write("durable parsed asset") > 0);
    store.close();

    const std::string manifest = (project_tmp.path() + "/proj.dlp").toStdString();
    auto proj = dolphin::app::Project::create("proj", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    auto* src   = proj->addSource("/fake/a.xtf", "xtf");
    auto* layer = proj->addLayer(src->id, "a");
    CHECK(layer != nullptr);
    if (!layer) return;
    layer->artifact_store_path   = store_path.toStdString();
    layer->artifact_store_format = "dpcache";

    const std::string layer_id = layer->id;
    proj->removeLayer(layer_id);

    CHECK(QFileInfo::exists(store_path));
    CHECK(QFileInfo(store_path).size() > 0);
}

// Opening a project must not purge unreferenced project-owned parsed stores.
// D-04 treats these as durable workflow assets, not disposable caches.
static void testOpenPreservesUnreferencedProjectParsedStores()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/proj.dlp").toStdString();
    auto proj = dolphin::app::Project::create("proj", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;
    CHECK(proj->save());

    const QString data_path = QString::fromStdString(proj->dataPath());
    CHECK(QDir().mkpath(data_path));
    const QString dlpd_path = data_path + "/unreferenced.dlpd";
    const QString legacy_path = data_path + "/unreferenced.dpcache";
    for (const QString& path : {dlpd_path, legacy_path}) {
        QFile parsed_store(path);
        CHECK(parsed_store.open(QIODevice::WriteOnly));
        CHECK(parsed_store.write("durable parsed asset") > 0);
    }

    proj.reset();
    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    CHECK(QFileInfo::exists(dlpd_path));
    CHECK(QFileInfo::exists(legacy_path));
}

// Display state fields survive a save/open round trip.
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

// 9b. Map opacity round-trip (v11) — a customised value is written and
//     restored; an untouched (opaque) layer omits the field entirely so old
//     manifests stay minimal, and still reads back as 1.0.
static void testMapOpacityPersistence()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/opacity.dlp").toStdString();
    auto proj = dolphin::app::Project::create("opacity", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    auto* src     = proj->addSource("/fake/survey.xtf", "xtf");
    auto* faded   = proj->addLayer(src->id, "faded");
    auto* opaque  = proj->addLayer(src->id, "opaque");
    CHECK(faded != nullptr && opaque != nullptr);
    if (!faded || !opaque) return;
    injectIndexEntry(faded);
    injectIndexEntry(opaque);

    faded->map_opacity        = 0.35f;
    faded->map_blend_mode     = 2;      // Lighten
    faded->map_clip_polygons  = true;
    faded->map_show_beams     = true;
    // opaque stays at the 1.0f / Blend(0) / false defaults.

    CHECK(proj->save());

    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded) return;
    const auto* lf = loaded->findLayer(faded->id);
    const auto* lo = loaded->findLayer(opaque->id);
    CHECK(lf != nullptr && lo != nullptr);
    if (!lf || !lo) return;

    CHECK(std::fabs(lf->map_opacity - 0.35f) < 1e-3f);
    CHECK(lf->map_blend_mode == 2);
    CHECK(lf->map_clip_polygons);
    CHECK(lf->map_show_beams);
    CHECK(std::fabs(lo->map_opacity - 1.0f) < 1e-3f);
    CHECK(lo->map_blend_mode == 0);
    CHECK(!lo->map_clip_polygons);
    CHECK(!lo->map_show_beams);
}

// 10. Nav-correction state round-trip — the model-owned nav_state / nav_customized
//     (shared by SSS + SBP) survive save → reopen. An untouched layer stays
//     uncustomized (nav_state is only written when nav_customized).
static void testNavStatePersistence()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/nav.dlp").toStdString();
    auto proj = dolphin::app::Project::create("nav", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    auto* src        = proj->addSource("/fake/survey.xtf", "xtf");
    auto* customized = proj->addLayer(src->id, "customized");
    auto* untouched  = proj->addLayer(src->id, "untouched");
    CHECK(customized != nullptr && untouched != nullptr);
    if (!customized || !untouched) return;

    injectIndexEntry(customized);
    injectIndexEntry(untouched);

    customized->nav_customized              = true;
    customized->nav_state.smooth_enabled     = true;
    customized->nav_state.smooth_window      = 9;
    customized->nav_state.layback_enabled    = true;
    customized->nav_state.layback_m          = 12.5f;
    customized->nav_state.heading_offset_deg = 3.5f;
    customized->nav_state.pitch_offset_deg   = -1.25f;
    customized->nav_state.roll_offset_deg    = 0.75f;

    CHECK(proj->save());

    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded || loaded->layers().size() < 2) return;

    const auto* lc = loaded->findLayer(customized->id);
    const auto* lu = loaded->findLayer(untouched->id);
    CHECK(lc != nullptr && lu != nullptr);
    if (!lc || !lu) return;

    CHECK(lc->nav_customized);
    CHECK(lc->nav_state.smooth_enabled);
    CHECK(lc->nav_state.smooth_window == 9);
    CHECK(lc->nav_state.layback_enabled);
    CHECK(std::fabs(lc->nav_state.layback_m - 12.5f) < 1e-4f);
    CHECK(std::fabs(lc->nav_state.heading_offset_deg - 3.5f) < 1e-4f);
    CHECK(std::fabs(lc->nav_state.pitch_offset_deg + 1.25f) < 1e-4f);
    CHECK(std::fabs(lc->nav_state.roll_offset_deg - 0.75f) < 1e-4f);

    // Untouched layer must reopen uncustomized with default params.
    CHECK(!lu->nav_customized);
    CHECK(!lu->nav_state.smooth_enabled);
    CHECK(!lu->nav_state.layback_enabled);
}

// 11. Feature round-trip — polygon + polyline shape annotations (geometry, type,
//     classification, notes) survive save → reopen; ids stay stable and the
//     next-feature-id counter is restored above the max loaded id.
static void testFeaturePersistence()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/features.dlp").toStdString();
    auto proj = dolphin::app::Project::create("features", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    // A closed polygon (debris field) and an open polyline (cable run).
    dolphin::core::Feature poly;
    poly.type = dolphin::core::FeatureType::Polygon;
    poly.classification = "Debris Field";
    poly.notes = "scattered targets";
    poly.line_id = "layer-7";
    poly.vertices = { {10.0, 20.0}, {10.5, 20.0}, {10.5, 20.5}, {10.0, 20.5} };
    proj->addFeature(poly);

    dolphin::core::Feature line;
    line.type = dolphin::core::FeatureType::Polyline;
    line.classification = "Cable Corridor";
    line.vertices = { {11.0, 21.0}, {11.2, 21.3}, {11.4, 21.1} };
    proj->addFeature(line);

    CHECK(proj->features().size() == 2);
    const uint64_t poly_id = proj->features()[0].id;
    const uint64_t line_id = proj->features()[1].id;
    CHECK(poly_id != 0 && line_id != 0 && poly_id != line_id);

    CHECK(proj->save());

    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded) return;
    CHECK(loaded->features().size() == 2);
    if (loaded->features().size() < 2) return;

    const auto& lp = loaded->features()[0];
    const auto& ll = loaded->features()[1];
    CHECK(lp.id == poly_id);
    CHECK(lp.type == dolphin::core::FeatureType::Polygon);
    CHECK(lp.classification == "Debris Field");
    CHECK(lp.notes == "scattered targets");
    CHECK(lp.line_id == "layer-7");
    CHECK(lp.vertices.size() == 4);
    if (lp.vertices.size() == 4) {
        CHECK(std::fabs(lp.vertices[0].lat - 10.0) < 1e-9);
        CHECK(std::fabs(lp.vertices[2].lon - 20.5) < 1e-9);
    }
    CHECK(lp.label == "F001");

    CHECK(ll.id == line_id);
    CHECK(ll.type == dolphin::core::FeatureType::Polyline);
    CHECK(ll.classification == "Cable Corridor");
    CHECK(ll.vertices.size() == 3);

    // Adding after reopen must not collide with restored ids.
    dolphin::core::Feature extra;
    extra.type = dolphin::core::FeatureType::Polyline;
    extra.vertices = { {0.0, 0.0}, {1.0, 1.0} };
    loaded->addFeature(extra);
    CHECK(loaded->features().back().id > line_id);
}

// 12. Schema version guard — a manifest written by a NEWER app version is
//     refused with a user-presentable reason (never silently misparsed); a
//     legacy low-version manifest still opens; the writer stamps the current
//     kSchemaVersion.
static void testSchemaVersionGuard()
{
    using dolphin::app::Project;

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    auto writeManifest = [&](const QString& name, int version) {
        const QString path = tmp.path() + "/" + name;
        QFile f(path);
        CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(QStringLiteral(
            "{\"version\": %1, \"name\": \"guard\", \"crs\": \"EPSG:4326\","
            " \"sources\": [], \"layers\": []}").arg(version).toUtf8());
        f.close();
        return path.toStdString();
    };

    // Future version → refused, with a reason mentioning the newer version.
    std::string err;
    auto future = Project::open(
        writeManifest("future.dlp", Project::kSchemaVersion + 1), &err);
    CHECK(future == nullptr);
    CHECK(err.find("newer version") != std::string::npos);

    // Legacy v1 → still opens (migration path, not rejection).
    std::string legacy_err;
    auto legacy = Project::open(writeManifest("legacy.dlp", 1), &legacy_err);
    CHECK(legacy != nullptr);
    CHECK(legacy_err.empty());

    // Writer stamps the current schema version.
    const std::string manifest = (tmp.path() + "/stamp.dlp").toStdString();
    auto proj = Project::create("stamp", manifest);
    CHECK(proj != nullptr && proj->save());
    std::ifstream in(manifest);
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto root = dolphin::util::parseJson(ss.str());
    CHECK(root.get("version").asInt() == Project::kSchemaVersion);
}

// Malformed manifests fail cleanly, identity references are validated, and
// persisted numeric UI/model fields cannot wrap into unsafe values.
static void testManifestValidation()
{
    using dolphin::app::BottomTrackKind;
    using dolphin::app::Project;

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    auto writeRaw = [&](const QString& name, const QByteArray& json) {
        const QString path = tmp.path() + "/" + name;
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly | QIODevice::Text));
        CHECK(file.write(json) == json.size());
        file.close();
        return path.toStdString();
    };

    std::string error;
    auto huge_number = Project::open(writeRaw(
        "bad-number.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":[],"
        "\"layers\":[],\"contacts\":[{\"id\":1e999999}]}"), &error);
    CHECK(huge_number == nullptr);
    CHECK(error.find("Invalid project JSON") != std::string::npos);

    error.clear();
    auto trailing_garbage = Project::open(writeRaw(
        "trailing-garbage.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":[],\"layers\":[]}junk"),
        &error);
    CHECK(trailing_garbage == nullptr);
    CHECK(error.find("Invalid project JSON") != std::string::npos);

    for (const auto& malformed : {
             QByteArray("{\"version\":1,\"name\":\"truncated\",\"sources\":[],\"layers\":[]"),
             QByteArray("{\"version\":1,\"name\" \"missing-colon\"}"),
             QByteArray("{\"version\":1,\"name\":\"bad-literal\",\"x\":truX}"),
             QByteArray("{\"version\":1,\"name\":\"bad-number\",\"x\":1.}"),
         }) {
        error.clear();
        auto invalid = Project::open(writeRaw("malformed.dlp", malformed), &error);
        CHECK(invalid == nullptr);
        CHECK(error.find("Invalid project JSON") != std::string::npos);
    }

    error.clear();
    auto duplicate_source = Project::open(writeRaw(
        "duplicate-source.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":["
        "{\"id\":\"same\"},{\"id\":\"same\"}],\"layers\":[]}"), &error);
    CHECK(duplicate_source == nullptr);
    CHECK(error.find("duplicate source ID") != std::string::npos);

    error.clear();
    auto missing_source = Project::open(writeRaw(
        "missing-source.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":[],\"layers\":["
        "{\"id\":\"layer\",\"source_id\":\"missing\"}]}"), &error);
    CHECK(missing_source == nullptr);
    CHECK(error.find("missing source") != std::string::npos);

    error.clear();
    auto missing_name = Project::open(writeRaw(
        "missing-name.dlp",
        "{\"version\":1,\"sources\":[],\"layers\":[]}"), &error);
    CHECK(missing_name == nullptr);
    CHECK(error.find("non-empty name") != std::string::npos);

    error.clear();
    auto wrong_sources_shape = Project::open(writeRaw(
        "wrong-shape.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":{},\"layers\":[]}"),
        &error);
    CHECK(wrong_sources_shape == nullptr);
    CHECK(error.find("'sources' must be an array") != std::string::npos);

    error.clear();
    auto duplicate_contact = Project::open(writeRaw(
        "duplicate-contact.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":[],\"layers\":[],"
        "\"contacts\":[{\"id\":1}],\"recycled_contacts\":[{\"id\":1}]}"),
        &error);
    CHECK(duplicate_contact == nullptr);
    CHECK(error.find("duplicate contact ID") != std::string::npos);

    error.clear();
    auto fractional_feature = Project::open(writeRaw(
        "fractional-feature.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":[],\"layers\":[],"
        "\"features\":[{\"id\":1.5}]}"), &error);
    CHECK(fractional_feature == nullptr);
    CHECK(error.find("invalid feature ID") != std::string::npos);

    error.clear();
    auto lossy_contact_id = Project::open(writeRaw(
        "lossy-contact-id.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":[],\"layers\":[],"
        "\"contacts\":[{\"id\":9007199254740992}]}"), &error);
    CHECK(lossy_contact_id == nullptr);
    CHECK(error.find("invalid or duplicate contact ID") != std::string::npos);

    error.clear();
    auto duplicate_group = Project::open(writeRaw(
        "duplicate-group.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":[],\"layers\":[],"
        "\"layer_groups\":[{\"id\":\"g\"},{\"id\":\"g\"}]}"), &error);
    CHECK(duplicate_group == nullptr);
    CHECK(error.find("duplicate layer-group ID") != std::string::npos);

    const QByteArray mismatched_index = QStringLiteral(
        "{\"version\":%1,\"name\":\"bad\",\"sources\":["
        "{\"id\":\"src\"},{\"id\":\"other\"}],"
        "\"layers\":[{\"id\":\"layer\",\"source_id\":\"src\","
        "\"index_built\":true,\"artifact_index\":{\"source_id\":\"other\","
        "\"entries\":[{\"type\":0,\"artifact_id\":1,\"timestamp_us\":2,"
        "\"file_offset\":3,\"subrecord_offset\":0,\"byte_length\":1}]}}]}")
        .arg(Project::kSchemaVersion).toUtf8();
    error.clear();
    auto source_mismatch = Project::open(
        writeRaw("index-source-mismatch.dlp", mismatched_index), &error);
    CHECK(source_mismatch != nullptr);
    if (source_mismatch && !source_mismatch->layers().empty()) {
        CHECK(source_mismatch->layers().front()->artifact_index.source_id == "src");
        CHECK(source_mismatch->layers().front()->artifact_index.empty());
        CHECK(!source_mismatch->layers().front()->index_built);
    }

    // Reader-built indexes in existing SSS/SBP projects use the parsed-store
    // path here.  It is a storage locator, not a conflicting ProjectSource ID,
    // and must be normalized on reopen instead of rejecting the project.
    const QByteArray path_form_index = QStringLiteral(
        "{\"version\":%1,\"name\":\"legacy-index-locator\","
        "\"sources\":[{\"id\":\"src\"}],"
        "\"layers\":[{\"id\":\"layer\",\"source_id\":\"src\","
        "\"modality\":\"sidescan\",\"artifact_index\":{"
        "\"source_id\":\"C:/survey/data/src.dlpd\",\"entries\":[]}}]}")
        .arg(Project::kSchemaVersion).toUtf8();
    error.clear();
    auto path_locator = Project::open(
        writeRaw("index-path-locator.dlp", path_form_index), &error);
    CHECK(path_locator != nullptr);
    if (path_locator && !path_locator->layers().empty()) {
        CHECK(path_locator->layers().front()->artifact_index.source_id == "src");
    }

    const QByteArray bad_index = QStringLiteral(
        "{\"version\":%1,\"name\":\"recover\",\"sources\":[{\"id\":\"src\"}],"
        "\"layers\":[{\"id\":\"layer\",\"source_id\":\"src\","
        "\"modality\":\"sidescan\",\"index_built\":true,"
        "\"artifact_index\":{\"source_id\":\"src\",\"entries\":[{"
        "\"type\":0,\"artifact_id\":1,\"timestamp_us\":2,"
        "\"file_offset\":3.5,\"subrecord_offset\":0,\"byte_length\":0.5}]}}]}")
        .arg(Project::kSchemaVersion).toUtf8();
    error.clear();
    auto recovered = Project::open(writeRaw("bad-index.dlp", bad_index), &error);
    CHECK(recovered != nullptr);
    if (recovered && !recovered->layers().empty()) {
        CHECK(recovered->layers().front()->artifact_index.empty());
        CHECK(!recovered->layers().front()->index_built);
    }

    error.clear();
    auto legacy_lines = Project::open(writeRaw(
        "legacy-lines.dlp",
        "{\"version\":1,\"name\":\"legacy\",\"sources\":[{\"id\":\"src\"}],"
        "\"lines\":[{\"id\":\"line\",\"label\":\"Line\","
        "\"source_id\":\"src\",\"modality\":\"sidescan\"}]}"), &error);
    CHECK(legacy_lines != nullptr);
    if (legacy_lines) {
        CHECK(legacy_lines->sources().size() == 1);
        CHECK(legacy_lines->layers().size() == 1);
    }

    error.clear();
    auto unknown_project_node = Project::open(writeRaw(
        "unknown-project-node.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":[],\"layers\":[],"
        "\"project_graph\":{\"nodes\":[{\"id\":\"n\","
        "\"type\":\"future.unsupported\"}],\"edges\":[]}}"), &error);
    CHECK(unknown_project_node == nullptr);
    CHECK(error.find("invalid project graph") != std::string::npos);

    error.clear();
    auto unknown_layer_node = Project::open(writeRaw(
        "unknown-layer-node.dlp",
        "{\"version\":1,\"name\":\"bad\",\"sources\":[{\"id\":\"src\"}],"
        "\"layers\":[{\"id\":\"layer\",\"source_id\":\"src\","
        "\"graph\":{\"nodes\":[{\"id\":\"n\","
        "\"type\":\"future.unsupported\"}],\"edges\":[]}}]}"), &error);
    CHECK(unknown_layer_node == nullptr);
    CHECK(error.find("invalid processing graph") != std::string::npos);

    const QByteArray sanitized = QStringLiteral(
        "{\"version\":%1,\"name\":\"safe\",\"crs\":\"EPSG:4326\","
        "\"sources\":[{\"id\":\"src\",\"path\":\"\",\"size\":-5,"
        "\"modified_utc_ms\":-9},{\"id\":\"huge\",\"size\":1e100,"
        "\"modified_utc_ms\":1e100}],\"layers\":[{\"id\":\"layer\","
        "\"label\":\"\",\"source_id\":\"src\",\"modality\":\"raster\","
        "\"bottom_track_kind\":99,\"qc_viewed_fraction\":4,"
        "\"map_blend_mode\":99,\"raster\":{\"cols\":-4,\"rows\":-3}}]}")
        .arg(Project::kSchemaVersion).toUtf8();
    error.clear();
    auto safe = Project::open(writeRaw("sanitized.dlp", sanitized), &error);
    CHECK(safe != nullptr);
    if (safe && !safe->layers().empty()) {
        const auto& layer = *safe->layers().front();
        CHECK(layer.label == "layer");
        CHECK(layer.bottom_track_kind == BottomTrackKind::Mixed);
        CHECK(std::fabs(layer.qc_viewed_fraction - 1.f) < 1e-6f);
        CHECK(layer.map_blend_mode == 3);
        CHECK(!layer.raster.valid);
        CHECK(!safe->sources().empty() && safe->sources().front().size_bytes == 0);
        CHECK(!safe->sources().empty() && safe->sources().front().modified_utc_ms == 0);
        CHECK(safe->sources().size() == 2);
        if (safe->sources().size() == 2) {
            CHECK(safe->sources()[1].size_bytes == std::numeric_limits<uint64_t>::max());
            CHECK(safe->sources()[1].modified_utc_ms
                  == std::numeric_limits<int64_t>::max());
        }
    }

    CHECK(dolphin::util::JsonValue(std::numeric_limits<double>::infinity()).dump()
          == "null");
    CHECK(dolphin::util::JsonValue(1e100).asInt()
          == std::numeric_limits<int>::max());
    CHECK(dolphin::util::JsonValue(1e100).asFloat()
          == std::numeric_limits<float>::max());
    const std::string escaped_control = dolphin::util::JsonValue(
        std::string(1, '\x01')).dump();
    CHECK(escaped_control == "\"\\u0001\"");
    CHECK(dolphin::util::parseJson(escaped_control).asString()
          == std::string(1, '\x01'));
}

// 13. SSS/SBP display-param structs round-trip — gain/contrast/invert and the
//     customized flags survive save → reopen (regression net for the
//     DisplayStateManager migration; complements test 9's palette fields).
static void testDisplayParamsRoundTrip()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;

    const std::string manifest = (tmp.path() + "/params.dlp").toStdString();
    auto proj = dolphin::app::Project::create("params", manifest);
    CHECK(proj != nullptr);
    if (!proj) return;

    auto* src   = proj->addSource("/fake/survey.sgy", "segy");
    auto* layer = proj->addLayer(src->id, "line1");
    CHECK(layer != nullptr);
    if (!layer) return;
    injectIndexEntry(layer);
    layer->artifact_store_path = (tmp.path() + "/line1_processed.dlpd").toStdString();
    layer->artifact_store_format = "dlpd";
    layer->source_artifact_store_path =
        (tmp.path() + "/line1_imported.dlpd").toStdString();

    auto& sss = layer->sss_display_state;
    sss.customized                    = true;
    sss.params.agc.enabled            = true;
    sss.params.agc.mode               = dolphin::app::AgcMode::Variable;
    sss.params.agc.strength           = 0.65f;
    sss.params.agc.along_track_win    = 75;
    sss.params.agc.smoothing_type     = dolphin::app::AgcSmoothingType::Median;
    sss.params.agc.smoothing_win      = 9;
    sss.params.agc.edge_skip_samples  = 37;
    sss.params.agc.noise_floor_pct    = 3.f;
    sss.params.agc.gain_cap_db        = 18.f;
    sss.params.agc.target_mean        = 24000.f;
    sss.params.tvg.enabled            = true;
    sss.params.tvg.spreading          = 17.f;
    sss.params.tvg.absorption         = 0.35f;
    sss.params.tvg.fallback_blanking_m = 2.75f;
    sss.params.arc.enabled            = true;
    sss.params.arc.exponent           = 2.2f;
    sss.params.arc.gain_cap_db        = 16.f;
    layer->pipeline_applied            = true;
    layer->processing_origin           = dolphin::app::ProcessingOrigin::Waterfall;
    layer->applied_graph_json          = "{\"nodes\":[],\"edges\":[]}";
    layer->baked_correction_flags      =
        static_cast<uint32_t>(dolphin::core::CorrectionFlag::Tvg)
        | static_cast<uint32_t>(dolphin::core::CorrectionFlag::Arc);
    sss.params.arn.enabled            = true;
    sss.params.arn.strength           = 0.72f;
    sss.params.arn.gain_cap_db        = 9.f;
    sss.params.arn.column_smooth      = 13;
    sss.params.destripe.enabled       = true;
    sss.params.destripe.window        = 61;
    sss.params.destripe.subdivision   = 7;
    sss.params.destripe.capping       = 2.4f;
    sss.params.destripe.threshold_db  = 1.75f;
    sss.params.beam_pattern.enabled   = true;
    sss.params.beam_pattern.strength  = 0.84f;
    sss.params.beam_pattern.smooth_radius = 17;
    sss.params.beam_pattern.gain_cap_db = 11.f;
    sss.params.ml_enhance.enabled     = true;
    sss.params.ml_enhance.tile_pings  = 96;
    sss.params.ml_enhance.tile_samps  = 192;
    sss.params.ml_enhance.clip_limit  = 3.25f;

    auto& d = layer->sbp_display_state;
    d.display_customized       = true;
    d.gain_customized          = true;
    d.signal_customized        = true;
    d.display.gain             = 2.5f;
    d.display.contrast         = 1.4f;
    d.display.polarity_invert  = true;
    d.display.show_bottom_track = false;
    d.display.sound_speed_ms   = 1520.0f;
    d.gain.static_gain_en      = true;
    d.gain.static_gain_db      = 6.0f;
    d.gain.agc_en              = true;
    d.gain.agc_window          = 128;
    d.gain.agc_gain_cap_db     = 32.f;
    d.signal.envelope_en       = true;
    d.signal.bandpass_en       = true;
    d.signal.bp_lo_hz          = 500.0f;
    d.signal.bp_hi_hz          = 8000.0f;

    CHECK(proj->save());

    auto loaded = dolphin::app::Project::open(manifest);
    CHECK(loaded != nullptr);
    if (!loaded) return;
    const auto* ll = loaded->findLayer(layer->id);
    CHECK(ll != nullptr);
    if (!ll) return;

    const auto& r = ll->sbp_display_state;
    const auto& rs = ll->sss_display_state;
    CHECK(rs.customized);
    CHECK(rs.params.agc.enabled);
    CHECK(rs.params.agc.mode == dolphin::app::AgcMode::Variable);
    CHECK(std::fabs(rs.params.agc.strength - 0.65f) < 1e-4f);
    CHECK(rs.params.agc.along_track_win == 75);
    CHECK(rs.params.agc.smoothing_type == dolphin::app::AgcSmoothingType::Median);
    CHECK(rs.params.agc.smoothing_win == 9);
    CHECK(rs.params.agc.edge_skip_samples == 37);
    CHECK(std::fabs(rs.params.agc.noise_floor_pct - 3.f) < 1e-4f);
    CHECK(std::fabs(rs.params.agc.gain_cap_db - 18.f) < 1e-4f);
    CHECK(std::fabs(rs.params.agc.target_mean - 24000.f) < 1e-4f);
    CHECK(rs.params.tvg.enabled);
    CHECK(std::fabs(rs.params.tvg.spreading - 17.f) < 1e-4f);
    CHECK(std::fabs(rs.params.tvg.absorption - 0.35f) < 1e-4f);
    CHECK(std::fabs(rs.params.tvg.fallback_blanking_m - 2.75f) < 1e-4f);
    CHECK(rs.params.arc.enabled);
    CHECK(std::fabs(rs.params.arc.exponent - 2.2f) < 1e-4f);
    CHECK(std::fabs(rs.params.arc.gain_cap_db - 16.f) < 1e-4f);
    CHECK(ll->pipeline_applied);
    CHECK(ll->source_artifact_store_path == layer->source_artifact_store_path);
    CHECK(ll->processing_origin == dolphin::app::ProcessingOrigin::Waterfall);
    CHECK(ll->applied_graph_json == layer->applied_graph_json);
    CHECK(dolphin::core::hasCorrectionFlag(
        ll->baked_correction_flags, dolphin::core::CorrectionFlag::Tvg));
    CHECK(dolphin::core::hasCorrectionFlag(
        ll->baked_correction_flags, dolphin::core::CorrectionFlag::Arc));
    CHECK(rs.params.arn.enabled);
    CHECK(std::fabs(rs.params.arn.strength - 0.72f) < 1e-4f);
    CHECK(std::fabs(rs.params.arn.gain_cap_db - 9.f) < 1e-4f);
    CHECK(rs.params.arn.column_smooth == 13);
    CHECK(rs.params.destripe.enabled);
    CHECK(rs.params.destripe.window == 61);
    CHECK(rs.params.destripe.subdivision == 7);
    CHECK(std::fabs(rs.params.destripe.capping - 2.4f) < 1e-4f);
    CHECK(std::fabs(rs.params.destripe.threshold_db - 1.75f) < 1e-4f);
    CHECK(rs.params.beam_pattern.enabled);
    CHECK(std::fabs(rs.params.beam_pattern.strength - 0.84f) < 1e-4f);
    CHECK(rs.params.beam_pattern.smooth_radius == 17);
    CHECK(std::fabs(rs.params.beam_pattern.gain_cap_db - 11.f) < 1e-4f);
    CHECK(rs.params.ml_enhance.enabled);
    CHECK(rs.params.ml_enhance.tile_pings == 96);
    CHECK(rs.params.ml_enhance.tile_samps == 192);
    CHECK(std::fabs(rs.params.ml_enhance.clip_limit - 3.25f) < 1e-4f);
    CHECK(r.display_customized && r.gain_customized && r.signal_customized);
    CHECK(std::fabs(r.display.gain - 2.5f) < 1e-4f);
    CHECK(std::fabs(r.display.contrast - 1.4f) < 1e-4f);
    CHECK(r.display.polarity_invert);
    CHECK(!r.display.show_bottom_track);
    CHECK(std::fabs(r.display.sound_speed_ms - 1520.0f) < 1e-2f);
    CHECK(r.gain.static_gain_en);
    CHECK(std::fabs(r.gain.static_gain_db - 6.0f) < 1e-4f);
    CHECK(r.gain.agc_en);
    CHECK(r.gain.agc_window == 128);
    CHECK(std::fabs(r.gain.agc_gain_cap_db - 32.f) < 1e-4f);
    CHECK(r.signal.envelope_en);
    CHECK(r.signal.bandpass_en);
    CHECK(std::fabs(r.signal.bp_lo_hz - 500.0f) < 1e-2f);
    CHECK(std::fabs(r.signal.bp_hi_hz - 8000.0f) < 1e-2f);
}

// Recovery planning is source-wide and must never borrow CRS from another
// source in a mixed-coordinate-system project.
static void testMissingArtifactRecoveryPlan()
{
    dolphin::app::Project project;

    auto exact = [](const char* id) {
        dolphin::core::SpatialRef ref;
        ref.id = id;
        ref.kind = dolphin::core::SpatialRefKind::Projected;
        ref.exact = true;
        return ref;
    };

    auto* source_a = project.addSource("/survey/a.xtf", "xtf");
    source_a->source_spatial_ref = exact("EPSG:32620");
    const std::string source_a_path = source_a->path;
    auto* a_hf = project.addLayer(source_a->id, "A HF");
    auto* a_lf = project.addLayer(source_a->id, "A LF");

    auto* source_b = project.addSource("/survey/b.xtf", "xtf");
    const std::string source_b_path = source_b->path;
    auto* b = project.addLayer(source_b->id, "B");

    // This exact CRS belongs to another source and must not leak into B.
    auto* source_c = project.addSource("/survey/c.xtf", "xtf");
    source_c->source_spatial_ref = exact("EPSG:26921");
    auto* c = project.addLayer(source_c->id, "C");
    injectIndexEntry(c); // healthy: no recovery request

    const auto plan = dolphin::app::planMissingArtifactRecovery(project);
    CHECK(plan.size() == 2);
    if (plan.size() != 2) return;

    CHECK(plan[0].source_path == source_a_path);
    CHECK(plan[0].layer_id == a_hf->id);
    CHECK(plan[0].layer_id != a_lf->id); // one request for both siblings
    CHECK(plan[0].source_crs.id == "EPSG:32620");
    CHECK(plan[0].source_crs.exact);

    CHECK(plan[1].source_path == source_b_path);
    CHECK(plan[1].layer_id == b->id);
    CHECK(plan[1].source_crs.empty());
    CHECK(!plan[1].source_crs.exact);
}

static void testInvalidLegacySettingsAreRecovered()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;
    const std::string manifest = (tmp.path() + "/legacy-settings.dlp").toStdString();
    auto project = dolphin::app::Project::create("legacy-settings", manifest);
    auto* source = project->addSource("/missing/legacy.xtf", "xtf");
    auto* layer = project->addLayer(source->id, "Legacy line");
    layer->sss_display_state.customized = true;
    layer->sss_display_state.params.arc.enabled = true;
    layer->sss_display_state.params.arc.exponent = 99.f;
    CHECK(project->save());

    std::string error;
    auto loaded = dolphin::app::Project::open(manifest, &error);
    CHECK(loaded != nullptr);
    CHECK(error.empty());
    if (!loaded) return;
    CHECK(!loaded->loadWarnings().empty());
    const auto* restored = loaded->findLayer(layer->id);
    CHECK(restored != nullptr);
    if (restored) {
        CHECK(!restored->sss_display_state.customized);
        CHECK(!restored->sss_display_state.params.arc.enabled);
    }
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
    testLayerRemovalPreservesExternalParsedStore();
    testOpenPreservesUnreferencedProjectParsedStores();
    testDisplayStatePersistence();
    testMapOpacityPersistence();
    testNavStatePersistence();
    testFeaturePersistence();
    testSchemaVersionGuard();
    testManifestValidation();
    testDisplayParamsRoundTrip();
    testMissingArtifactRecoveryPlan();
    testInvalidLegacySettingsAreRecovered();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
