// Stage 03 / Slice 03A — performance baseline benchmark.
//
// Measures the key hot paths so optimisation work has before/after numbers.
// All PERF: lines go to stdout; CTest captures them with --output-on-failure.
//
// Scenarios
//   A. ParsedCache buildIndex   — time to scan a DLPD index at 1k / 10k / 50k pings
//   B. ParsedCache sequential reads — read throughput at 10k pings
//   C. Project JSON round-trip  — fromJson + toJson at 10 layers / 100 contacts
//   D. XTF buildIndex (fixture) — index the reduced real-vendor fixtures
//
// Output format (grep-friendly):
//   PERF  <scenario_tag>  <ms>  <count>  <notes>
//
// The benchmark never fails on timing numbers; it exits non-zero only if the
// tested function produces incorrect results or throws.

#include "io/cache/ParsedCache.h"
#include "io/IFormatReader.h"
#include "io/xtf/XtfReader.h"
#include "core/Artifact.h"
#include "core/ArtifactIndex.h"
#include "core/SidescanPing.h"
#include "core/SubBottomTrace.h"
#include "core/SpatialRef.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------

using Clock  = std::chrono::steady_clock;
using Ms     = std::chrono::duration<double, std::milli>;

static double elapsedMs(Clock::time_point start)
{
    return Ms(Clock::now() - start).count();
}

static void reportPerf(const char* tag, double ms, int count, const char* notes = "")
{
    std::printf("PERF  %-42s  %8.2f ms  n=%-7d  %s\n", tag, ms, count, notes);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Failure tracking
// ---------------------------------------------------------------------------

static int g_checks = 0;
static int g_fail = 0;

#define REQUIRE(expr) do { \
    ++g_checks; \
    if (!(expr)) { \
        std::fprintf(stderr, "FATAL  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        ++g_fail; \
    } \
} while (false)

// ---------------------------------------------------------------------------
// Synthetic data builders
// ---------------------------------------------------------------------------

namespace {
using namespace dolphin;

// Build a SidescanPing with N samples (slant-range amplitude data)
static core::SidescanPing makePing(uint64_t id, int sample_count = 512)
{
    core::SidescanPing p;
    p.id             = id;
    p.timestamp_us   = static_cast<int64_t>(id) * 1'000'000LL;
    p.channel        = (id % 2 == 0) ? core::SidescanChannel::Port
                                     : core::SidescanChannel::Starboard;
    p.nav.lat        = 57.0 + id * 1e-5;
    p.nav.lon        = 10.0 + id * 1e-5;
    p.nav.valid      = true;
    p.nav.is_projected = false;
    p.frequency_hz   = 300'000.f;
    p.sample_rate_hz = 40'000.f;
    p.slant_range_m  = 75.f;
    p.samples.resize(static_cast<std::size_t>(sample_count));
    for (int i = 0; i < sample_count; ++i) {
        p.samples[i].amplitude = static_cast<uint16_t>((i * 137) & 0xFFFF);
        p.samples[i].range_m   = p.slant_range_m * static_cast<float>(i) /
                                  static_cast<float>(sample_count);
    }
    return p;
}

// RAII temp file — removed on destruction
struct TempFile {
    std::string path;
    explicit TempFile(const std::string& suffix = ".dlpd")
    {
        path = (std::filesystem::temp_directory_path() /
                ("dolphin_perf_" +
                 std::to_string(static_cast<uint64_t>(
                     Clock::now().time_since_epoch().count())) + suffix)).string();
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(path), ec);
    }
};

// Write N SSS pings to a temp DLPD and return the path.
// source_index is populated for use in subsequent read benchmarks.
static TempFile writeSyntheticDlpd(int n_pings, core::ArtifactIndex& out_index)
{
    TempFile tmp;
    io::FormatMeta meta;
    meta.format_name = "SYNTHETIC";
    meta.sonar_name  = "BenchmarkSonar";
    meta.coordinate_ref = core::makeWgs84SpatialRef();

    class BufReader : public io::IFormatReader {
    public:
        std::vector<core::Artifact> artifacts;
        io::FormatMeta              m;
        bool open(const std::string&) override { return true; }
        void close() override {}
        bool isOpen() const override { return true; }
        std::string formatName() const override { return "SYNTHETIC"; }
        io::FormatMeta metadata() override { return m; }
        core::ArtifactIndex buildIndex(io::ProgressFn) override { return {}; }
        std::optional<core::Artifact> readArtifact(const core::ArtifactIndexEntry& e) override
        {
            for (const auto& a : artifacts)
                if (core::artifactId(a) == e.artifact_id) return a;
            return std::nullopt;
        }
    } reader;
    reader.m = meta;
    for (int i = 0; i < n_pings; ++i)
        reader.artifacts.push_back(makePing(static_cast<uint64_t>(i)));

    // Build the source index that writeParsedCache will iterate over.
    out_index.source_id = tmp.path;
    for (int i = 0; i < n_pings; ++i) {
        core::ArtifactIndexEntry e;
        e.artifact_id  = static_cast<uint64_t>(i);
        e.type         = core::ArtifactType::Sidescan;
        e.timestamp_us = static_cast<int64_t>(i) * 1'000'000LL;
        e.lat          = 57.0 + i * 1e-5;
        e.lon          = 10.0 + i * 1e-5;
        e.is_projected = false;
        e.byte_length  = 1;
        out_index.entries.push_back(e);
    }

    core::ArtifactIndex written_index;
    // writeParsedCache(path, source_index, reader, cache_index)
    const bool ok = io::writeParsedCache(tmp.path, out_index, reader, written_index);
    REQUIRE(ok);
    out_index = std::move(written_index);
    return tmp;
}

} // namespace

// ---------------------------------------------------------------------------
// Scenario A — ParsedCache buildIndex
// ---------------------------------------------------------------------------

static void benchCacheBuildIndex(int n_pings)
{
    core::ArtifactIndex dummy_index;
    auto tmp = writeSyntheticDlpd(n_pings, dummy_index);

    // Cold open — not OS-cache-flushed (no portable API), but at least
    // ensures the reader is constructed fresh each time.
    auto t0 = Clock::now();
    dolphin::io::ParsedCacheReader reader;
    const bool opened = reader.open(tmp.path);
    REQUIRE(opened);
    const auto idx = reader.buildIndex();
    double ms = elapsedMs(t0);

    REQUIRE(static_cast<int>(idx.entries.size()) == n_pings);

    char tag[64];
    std::snprintf(tag, sizeof(tag), "cache_build_index_%dk", n_pings / 1000);
    reportPerf(tag, ms, n_pings, "open+buildIndex");
}

// ---------------------------------------------------------------------------
// Scenario B — ParsedCache sequential reads
// ---------------------------------------------------------------------------

static void benchCacheSequentialRead(int n_pings)
{
    core::ArtifactIndex idx;
    auto tmp = writeSyntheticDlpd(n_pings, idx);

    dolphin::io::ParsedCacheReader reader;
    REQUIRE(reader.open(tmp.path));
    const auto built_idx = reader.buildIndex();
    REQUIRE(!built_idx.entries.empty());

    // Re-open to avoid warm-index effects
    reader.close();
    REQUIRE(reader.open(tmp.path));

    int read_count = 0;
    auto t0 = Clock::now();
    for (const auto& e : built_idx.entries) {
        auto art = reader.readArtifact(e);
        REQUIRE(art.has_value());
        ++read_count;
    }
    double ms = elapsedMs(t0);

    char tag[64];
    std::snprintf(tag, sizeof(tag), "cache_seq_read_%dk", n_pings / 1000);
    reportPerf(tag, ms, read_count, "readArtifact per entry");
}

// ---------------------------------------------------------------------------
// Scenario C — Project JSON round-trip
// ---------------------------------------------------------------------------

static void benchProjectStorageRoundTrip(int n_layers, int n_contacts)
{
    using namespace dolphin;

    QTemporaryDir tmp_dir;
    if (!tmp_dir.isValid()) { ++g_fail; return; }

    const std::string manifest =
        (tmp_dir.path() + "/PerfTest.dlp").toStdString();
    auto proj = app::Project::create("PerfBench", manifest);
    REQUIRE(proj != nullptr);

    // Populate layers
    for (int i = 0; i < n_layers; ++i) {
        const std::string src_path = "/data/line_" + std::to_string(i) + ".xtf";
        auto* src = proj->addSource(src_path, "xtf");
        if (!src) continue;
        auto* layer = proj->addLayer(src->id, "Line " + std::to_string(i));
        if (!layer) continue;
        layer->index_built = true;
        layer->slant_range_corrected = (i % 3 == 0);
        layer->sss_palette = i % 10;
        // Minimal artifact index entry
        core::ArtifactIndexEntry e;
        e.artifact_id = static_cast<uint64_t>(i);
        e.type        = core::ArtifactType::Sidescan;
        e.lat         = 57.0 + i * 0.001;
        e.lon         = 10.0 + i * 0.001;
        layer->artifact_index.entries.push_back(e);
        layer->artifact_index.source_id = src->id;
        proj->commitLayer(layer->id);
    }

    // Populate contacts
    for (int i = 0; i < n_contacts; ++i) {
        core::Contact c;
        c.label  = "C" + std::to_string(i);
        c.lat    = 57.0 + i * 1e-4;
        c.lon    = 10.0 + i * 1e-4;
        c.range_m = static_cast<float>(i % 100);
        proj->addContact(c);
    }

    // Warm save
    REQUIRE(proj->save());

    // Benchmark: save
    const int reps = 10;
    auto t0 = Clock::now();
    for (int i = 0; i < reps; ++i)
        REQUIRE(proj->save());
    const double save_ms = elapsedMs(t0) / reps;
    reportPerf("project_save", save_ms, n_layers,
               (std::to_string(n_layers) + "L " + std::to_string(n_contacts) + "C").c_str());

    // Benchmark: open
    std::shared_ptr<app::Project> proj2;
    t0 = Clock::now();
    for (int i = 0; i < reps; ++i)
        proj2 = app::Project::open(manifest);
    const double open_ms = elapsedMs(t0) / reps;
    reportPerf("project_open", open_ms, n_layers,
               (std::to_string(n_layers) + "L " + std::to_string(n_contacts) + "C").c_str());

    REQUIRE(proj2 != nullptr);
    REQUIRE(static_cast<int>(proj2->layers().size()) == n_layers);
}

// ---------------------------------------------------------------------------
// Scenario D — XTF buildIndex on real fixtures
// ---------------------------------------------------------------------------

#ifdef XTF_FIXTURE_DIR
static void benchXtfBuildIndex(const char* filename)
{
    const std::string path = std::string(XTF_FIXTURE_DIR) + "/" + filename;
    if (!std::filesystem::exists(path)) {
        std::printf("PERF  xtf_build_index_%-28s  SKIP (file not found)\n", filename);
        return;
    }

    dolphin::io::XtfReader reader;
    REQUIRE(reader.open(path));

    auto t0 = Clock::now();
    const auto idx = reader.buildIndex();
    double ms = elapsedMs(t0);

    char tag[80];
    std::snprintf(tag, sizeof(tag), "xtf_build_index_%s", filename);
    reportPerf(tag, ms, static_cast<int>(idx.entries.size()), "buildIndex");
}
#endif

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    std::printf("--- Dolphin Explorer Performance Baseline ---\n");
    std::printf("    Stage 03 / Slice 03A\n\n");
    std::fflush(stdout);

    // -- Scenario A: cache buildIndex at multiple scales -------------------
    benchCacheBuildIndex(1000);
    benchCacheBuildIndex(10000);
    benchCacheBuildIndex(50000);

    // -- Scenario B: sequential reads at 10k pings -------------------------
    benchCacheSequentialRead(10000);

    // -- Scenario C: project JSON round-trip -------------------------------
    benchProjectStorageRoundTrip(10, 100);
    benchProjectStorageRoundTrip(50, 500);

    // -- Scenario D: real XTF fixture indexing (small) ---------------------
#ifdef XTF_FIXTURE_DIR
    benchXtfBuildIndex("fix016_edgetech4200_isis_reduced.xtf");
    benchXtfBuildIndex("fix017_tst500k_32bit_reduced.xtf");
#endif

    std::printf("\n");
    if (g_fail > 0) {
        std::fprintf(stderr, "BENCHMARK ABORTED: %d correctness failure(s)\n", g_fail);
        return 1;
    }
    std::printf("Benchmark complete — %d correctness checks passed.\n", g_checks);
    std::fflush(stdout);
    return 0;
}
