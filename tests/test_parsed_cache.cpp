// Stage 01 regression tests for the ParsedCache subsystem.
//
// Covers:
//   - stale cache detection (parsedCacheIsValid)
//   - sidescan ping round-trip: cache/raw alignment
//   - metadata persistence: vessel_name, survey_name, frequency_hz, coordinate_ref
//   - nav backfill: readArtifact uses index lat/lon when nav.valid == false
//   - sub-bottom trace round-trip
//   - magnetometer sample round-trip
//
// Entry point: ctest --output-on-failure
//
// No external test framework — a small assertion helper is defined below.

#include "io/cache/ParsedCache.h"
#include "io/IFormatReader.h"
#include "core/Artifact.h"
#include "core/ArtifactIndex.h"
#include "core/SidescanPing.h"
#include "core/SubBottomTrace.h"
#include "core/MagSample.h"
#include "core/SpatialRef.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Tiny assertion helper
// ---------------------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        ++g_fail; \
    } else { \
        ++g_pass; \
    } \
} while (false)

#define CHECK_CLOSE(a, b, eps) do { \
    auto _a = (a); auto _b = (b); \
    if (std::abs(static_cast<double>(_a) - static_cast<double>(_b)) > (eps)) { \
        std::fprintf(stderr, "FAIL  %s:%d  |%s - %s| > %g  (got %g vs %g)\n", \
            __FILE__, __LINE__, #a, #b, static_cast<double>(eps), \
            static_cast<double>(_a), static_cast<double>(_b)); \
        ++g_fail; \
    } else { \
        ++g_pass; \
    } \
} while (false)

// ---------------------------------------------------------------------------
// Synthetic IFormatReader that serves a pre-built artifact list
// ---------------------------------------------------------------------------

namespace {

using namespace dolphin;

class SyntheticReader : public io::IFormatReader {
public:
    io::FormatMeta  meta;
    core::ArtifactIndex source_index;
    std::vector<core::Artifact> artifacts;

    bool open(const std::string&) override { return true; }
    void close() override {}
    bool isOpen() const override { return true; }
    std::string formatName() const override { return "SYNTHETIC"; }
    io::FormatMeta metadata() override { return meta; }

    core::ArtifactIndex buildIndex(io::ProgressFn) override
    {
        return source_index;
    }

    std::optional<core::Artifact> readArtifact(const core::ArtifactIndexEntry& entry) override
    {
        for (const auto& a : artifacts)
            if (core::artifactId(a) == entry.artifact_id)
                return a;
        return std::nullopt;
    }

    // Convenience: add an artifact and a matching index entry
    void add(const core::Artifact& artifact)
    {
        const uint64_t id  = core::artifactId(artifact);
        const int64_t  ts  = core::artifactTimestamp(artifact);
        const auto     typ = core::artifactType(artifact);

        core::ArtifactIndexEntry e;
        e.artifact_id  = id;
        e.type         = typ;
        e.timestamp_us = ts;
        e.file_offset  = 0; // offset irrelevant for synthetic reader
        e.byte_length  = 1;
        e.lat          = 0.0;
        e.lon          = 0.0;
        e.is_projected = false;

        // Grab lat/lon from artifact nav where available
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (!std::is_same_v<T, core::RasterGrid>) {
                e.lat          = v.nav.lat;
                e.lon          = v.nav.lon;
                e.is_projected = v.nav.is_projected;
            }
        }, artifact);

        source_index.entries.push_back(e);
        artifacts.push_back(artifact);
    }
};

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

// ---------------------------------------------------------------------------
// Temp-file RAII helper
// ---------------------------------------------------------------------------

struct TempFile {
    std::string path;
    explicit TempFile(const std::string& suffix = ".dlpd")
    {
        namespace fs = std::filesystem;
        path = (fs::temp_directory_path()
               / ("dolphin_test_" + std::to_string(
                   static_cast<uint64_t>(
                       std::chrono::steady_clock::now().time_since_epoch().count()
                   )) + suffix)).string();
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(path), ec);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Helper to build a minimal SidescanPing
// ---------------------------------------------------------------------------

static dolphin::core::SidescanPing makePing(uint64_t id, int64_t ts_us,
                                            double lat, double lon,
                                            bool nav_valid = true)
{
    using namespace dolphin::core;
    SidescanPing p;
    p.id             = id;
    p.timestamp_us   = ts_us;
    p.channel        = SidescanChannel::Port;
    p.nav.lat        = lat;
    p.nav.lon        = lon;
    p.nav.valid      = nav_valid;
    p.nav.is_projected = false;
    p.frequency_hz   = 300'000.f;
    p.sample_rate_hz = 40'000.f;
    p.slant_range_m  = 75.f;
    SidescanSample s1; s1.amplitude = 1000; s1.range_m = 10.f;
    SidescanSample s2; s2.amplitude = 2000; s2.range_m = 20.f;
    p.samples = { s1, s2 };
    return p;
}

// ---------------------------------------------------------------------------
// Test: parsedCacheIsValid rejects nonexistent / empty / wrong-magic files
// ---------------------------------------------------------------------------

static void test_stale_cache_detection()
{
    // Nonexistent path
    CHECK(!dolphin::io::parsedCacheIsValid("/nonexistent/path/cache.dlpd"));
    CHECK(!dolphin::io::parsedCacheIsValid(""));

    // Empty file
    TempFile tmp;
    {
        FILE* f = nullptr;
#ifdef _WIN32
        fopen_s(&f, tmp.path.c_str(), "wb");
#else
        f = std::fopen(tmp.path.c_str(), "wb");
#endif
        if (f) std::fclose(f);
    }
    CHECK(!dolphin::io::parsedCacheIsValid(tmp.path));

    // File with wrong magic
    TempFile tmp2;
    {
        FILE* f = nullptr;
#ifdef _WIN32
        fopen_s(&f, tmp2.path.c_str(), "wb");
#else
        f = std::fopen(tmp2.path.c_str(), "wb");
#endif
        if (f) {
            const char bad[8] = {'X','X','X','X','X','X','X','X'};
            std::fwrite(bad, 1, 8, f);
            std::fclose(f);
        }
    }
    CHECK(!dolphin::io::parsedCacheIsValid(tmp2.path));

    // Compatible header only: no records/footer means there is nothing reusable.
    TempFile header_only;
    SyntheticReader header_reader;
    header_reader.add(makePing(1, 1'000'000, 48.0, -52.0));
    dolphin::core::ArtifactIndex header_index;
    CHECK(dolphin::io::writeParsedCache(
        header_only.path, header_reader.source_index, header_reader, header_index));
    CHECK(header_index.size() == 1);
    std::error_code ec;
    std::filesystem::resize_file(
        std::filesystem::path(header_only.path),
        header_index.entries.front().file_offset, ec);
    CHECK(!ec);
    CHECK(!dolphin::io::parsedCacheIsValid(header_only.path));

    // Compatible header plus a truncated record is also unusable.
    TempFile truncated;
    SyntheticReader truncated_reader;
    truncated_reader.add(makePing(2, 2'000'000, 48.1, -52.1));
    dolphin::core::ArtifactIndex truncated_index;
    CHECK(dolphin::io::writeParsedCache(
        truncated.path, truncated_reader.source_index, truncated_reader,
        truncated_index));
    const auto& entry = truncated_index.entries.front();
    std::filesystem::resize_file(
        std::filesystem::path(truncated.path),
        entry.file_offset + entry.byte_length - 1, ec);
    CHECK(!ec);
    CHECK(!dolphin::io::parsedCacheIsValid(truncated.path));
}

// ---------------------------------------------------------------------------
// Test: empty/unsupported writes never publish or replace a durable cache
// ---------------------------------------------------------------------------

static void test_empty_writes_not_published()
{
    using namespace dolphin;

    NullReader null_reader;
    core::ArtifactIndex empty_source;
    core::ArtifactIndex empty_output;

    TempFile fresh;
    CHECK(!io::writeParsedCache(
        fresh.path, empty_source, null_reader, empty_output));
    CHECK(!std::filesystem::exists(std::filesystem::path(fresh.path)));

    // A failed rewrite must preserve the previously published cache.
    TempFile existing;
    SyntheticReader valid_reader;
    valid_reader.add(makePing(3, 3'000'000, 48.2, -52.2));
    core::ArtifactIndex valid_index;
    CHECK(io::writeParsedCache(
        existing.path, valid_reader.source_index, valid_reader, valid_index));
    const auto original_size = std::filesystem::file_size(existing.path);
    CHECK(!io::writeParsedCache(
        existing.path, empty_source, null_reader, empty_output));
    CHECK(std::filesystem::file_size(existing.path) == original_size);
    CHECK(io::parsedCacheIsValid(existing.path));

    // Non-empty input containing only unsupported artifact types must not leave
    // a misleading header-only DLPD either.
    TempFile unsupported;
    core::RasterGrid grid;
    grid.id = 4;
    std::vector<core::Artifact> buffer{core::Artifact{std::move(grid)}};
    core::ArtifactIndex unsupported_index;
    CHECK(!io::writeArtifactBufferToCache(
        unsupported.path, buffer, {}, unsupported_index));
    CHECK(!std::filesystem::exists(std::filesystem::path(unsupported.path)));

    // Mixed supported/unsupported output must fail as a unit. Publishing only
    // the supported prefix would silently change the processing result.
    TempFile mixed;
    auto ping = makePing(5, 5'000'000, 48.3, -52.3);
    core::RasterGrid mixed_grid;
    mixed_grid.id = 6;
    std::vector<core::Artifact> mixed_buffer{
        core::Artifact{std::move(ping)}, core::Artifact{std::move(mixed_grid)}};
    core::ArtifactIndex mixed_index;
    CHECK(!io::writeArtifactBufferToCache(
        mixed.path, mixed_buffer, {}, mixed_index));
    CHECK(!std::filesystem::exists(std::filesystem::path(mixed.path)));

    // An indexed artifact that cannot be decoded is also a failed cache build,
    // not permission to publish an incomplete subset.
    TempFile undecodable;
    core::ArtifactIndex claimed_source;
    core::ArtifactIndexEntry claimed_entry;
    claimed_entry.artifact_id = 7;
    claimed_entry.type = core::ArtifactType::Sidescan;
    claimed_source.entries.push_back(claimed_entry);
    CHECK(!io::writeParsedCache(
        undecodable.path, claimed_source, null_reader, empty_output));
    CHECK(!std::filesystem::exists(std::filesystem::path(undecodable.path)));

    // A valid rebuild must atomically replace an existing destination on
    // Windows as well as POSIX.
    SyntheticReader replacement_reader;
    replacement_reader.add(makePing(8, 8'000'000, 49.0, -53.0));
    replacement_reader.add(makePing(9, 9'000'000, 49.1, -53.1));
    core::ArtifactIndex replacement_index;
    CHECK(io::writeParsedCache(existing.path, replacement_reader.source_index,
                               replacement_reader, replacement_index));
    CHECK(replacement_index.size() == 2);
    io::ParsedCacheReader replaced;
    CHECK(replaced.open(existing.path));
    const auto replaced_index = replaced.quickIndex();
    CHECK(replaced_index.size() == 2);
    CHECK(replaced_index.entries.front().artifact_id == 8);
}

static void test_footerless_cache_repaired()
{
    using namespace dolphin;
    TempFile legacy;
    SyntheticReader writer;
    writer.add(makePing(10, 10'000'000, 50.0, -54.0));
    writer.add(makePing(11, 11'000'000, 50.1, -54.1));
    core::ArtifactIndex written;
    CHECK(io::writeParsedCache(
        legacy.path, writer.source_index, writer, written));

    const auto& last = written.entries.back();
    std::error_code ec;
    std::filesystem::resize_file(
        legacy.path, last.file_offset + last.byte_length, ec);
    CHECK(!ec);
    CHECK(!io::parsedCacheIsValid(legacy.path));

    io::ParsedCacheReader scanner;
    CHECK(scanner.open(legacy.path));
    const auto scanned = scanner.buildIndex();
    CHECK(scanned.size() == 2);
    scanner.close();

    CHECK(io::parsedCacheIsValid(legacy.path));
    io::ParsedCacheReader reopened;
    CHECK(reopened.open(legacy.path));
    CHECK(reopened.quickIndex().size() == 2);
}

// ---------------------------------------------------------------------------
// Test: sidescan round-trip (cache/raw alignment)
// ---------------------------------------------------------------------------

static void test_sidescan_round_trip()
{
    using namespace dolphin;
    TempFile tmp;

    SyntheticReader reader;
    reader.source_index.source_id = "test_source";
    reader.add(makePing(1, 1'000'000, 48.8566, 2.3522));
    reader.add(makePing(2, 2'000'000, 48.8600, 2.3550));

    core::ArtifactIndex cache_index;
    const bool wrote = io::writeParsedCache(tmp.path, reader.source_index, reader, cache_index);
    CHECK(wrote);
    CHECK(cache_index.size() == 2);

    // Open via ParsedCacheReader
    io::ParsedCacheReader cr;
    CHECK(cr.open(tmp.path));
    CHECK(cr.isOpen());

    core::ArtifactIndex read_index = cr.buildIndex();
    CHECK(read_index.size() == 2);

    // Verify first artifact fields are preserved
    const auto& entry0 = read_index.entries[0];
    CHECK(entry0.artifact_id == 1);
    CHECK(entry0.type == core::ArtifactType::Sidescan);
    CHECK(entry0.timestamp_us == 1'000'000);
    CHECK_CLOSE(entry0.lat, 48.8566, 1e-9);
    CHECK_CLOSE(entry0.lon, 2.3522,  1e-9);

    // Read artifact and verify sample data survives round-trip
    auto art0 = cr.readArtifact(entry0);
    CHECK(art0.has_value());
    CHECK(core::artifactType(*art0) == core::ArtifactType::Sidescan);
    const auto& ping = std::get<core::SidescanPing>(*art0);
    CHECK(ping.id == 1);
    CHECK(ping.samples.size() == 2);
    CHECK(ping.samples[0].amplitude == 1000);
    CHECK_CLOSE(ping.samples[0].range_m, 10.f, 1e-5f);
    CHECK(ping.samples[1].amplitude == 2000);
    CHECK_CLOSE(ping.frequency_hz, 300'000.f, 1.f);
    CHECK_CLOSE(ping.slant_range_m, 75.f, 1e-4f);
    CHECK_CLOSE(ping.nav.lat, 48.8566, 1e-9);
    CHECK_CLOSE(ping.nav.lon, 2.3522,  1e-9);
    CHECK(ping.nav.valid);
}

// ---------------------------------------------------------------------------
// Test: metadata persistence
// ---------------------------------------------------------------------------

static void test_metadata_persistence()
{
    using namespace dolphin;
    TempFile tmp;

    SyntheticReader reader;
    reader.source_index.source_id = "meta_test";
    reader.meta.vessel_name    = "MV Surveyor";
    reader.meta.survey_name    = "Survey 2024-Alpha";
    reader.meta.frequency_hz   = 100'000.f;
    reader.meta.coordinate_ref = core::makeWgs84SpatialRef();

    reader.add(makePing(10, 5'000'000, 51.5, -0.12));

    core::ArtifactIndex cache_index;
    CHECK(io::writeParsedCache(tmp.path, reader.source_index, reader, cache_index));

    // Read metadata back
    io::ParsedCacheReader cr;
    CHECK(cr.open(tmp.path));
    cr.buildIndex(); // populates derived time fields
    const io::FormatMeta meta = cr.metadata();

    CHECK(meta.vessel_name == "MV Surveyor");
    CHECK(meta.survey_name == "Survey 2024-Alpha");
    CHECK_CLOSE(meta.frequency_hz, 100'000.f, 1.f);
    CHECK(meta.coordinate_ref.kind == core::SpatialRefKind::Geographic);
    CHECK(meta.coordinate_ref.id == "EPSG:4326");
    CHECK(meta.coordinate_ref.exact);
    CHECK(meta.artifact_count == 1);
    CHECK(meta.sidescan_ping_count == 1);
}

// ---------------------------------------------------------------------------
// Test: parsedCacheIsValid accepts freshly written cache
// ---------------------------------------------------------------------------

static void test_valid_cache_accepted()
{
    using namespace dolphin;
    TempFile tmp;

    SyntheticReader reader;
    reader.source_index.source_id = "validity_test";
    reader.add(makePing(99, 1'000, 10.0, 20.0));

    core::ArtifactIndex cache_index;
    CHECK(io::writeParsedCache(tmp.path, reader.source_index, reader, cache_index));
    CHECK(io::parsedCacheIsValid(tmp.path));
}

// ---------------------------------------------------------------------------
// Test: nav backfill — readArtifact uses index lat/lon when nav.valid == false
// ---------------------------------------------------------------------------

static void test_nav_backfill()
{
    using namespace dolphin;
    TempFile tmp;

    SyntheticReader reader;
    reader.source_index.source_id = "backfill_test";

    // Ping with no nav, but the index entry will carry backfilled coords
    auto ping = makePing(77, 9'000'000, 0.0, 0.0, /*nav_valid=*/false);
    ping.nav.lat = 0.0;
    ping.nav.lon = 0.0;
    ping.nav.valid = false;

    // We manually set the index entry lat/lon to simulate PACKET_NAV backfill
    core::ArtifactIndexEntry e;
    e.artifact_id  = ping.id;
    e.type         = core::ArtifactType::Sidescan;
    e.timestamp_us = ping.timestamp_us;
    e.file_offset  = 0;
    e.byte_length  = 1;
    e.lat          = 55.1234;
    e.lon          = -1.9876;
    e.is_projected = false;
    reader.source_index.entries.push_back(e);
    reader.artifacts.push_back(ping);

    core::ArtifactIndex cache_index;
    CHECK(io::writeParsedCache(tmp.path, reader.source_index, reader, cache_index));

    io::ParsedCacheReader cr;
    CHECK(cr.open(tmp.path));
    core::ArtifactIndex read_index = cr.buildIndex();
    CHECK(read_index.size() == 1);

    // Inject the backfilled coords into the index entry (as ImportService would)
    auto entry = read_index.entries[0];
    entry.lat = 55.1234;
    entry.lon = -1.9876;

    auto art = cr.readArtifact(entry);
    CHECK(art.has_value());
    const auto& result = std::get<core::SidescanPing>(*art);
    // Nav backfill path: nav.valid was false, index provided the coords
    CHECK(result.nav.valid);
    CHECK_CLOSE(result.nav.lat, 55.1234, 1e-9);
    CHECK_CLOSE(result.nav.lon, -1.9876, 1e-9);
}

// ---------------------------------------------------------------------------
// Test: sub-bottom trace round-trip
// ---------------------------------------------------------------------------

static void test_subbottom_round_trip()
{
    using namespace dolphin;
    TempFile tmp;

    SyntheticReader reader;
    reader.source_index.source_id = "sbb_test";

    core::SubBottomTrace trace;
    trace.id             = 200;
    trace.timestamp_us   = 3'000'000;
    trace.nav.lat        = 52.0;
    trace.nav.lon        = 4.0;
    trace.nav.valid      = true;
    trace.frequency_hz   = 3'500.f;
    trace.sample_rate_hz = 20'000.f;
    trace.tow_depth_m    = 8.5f;
    trace.two_way_time_s    = 0.05f;
    trace.bottom_sample_idx = 42;
    trace.samples           = { 0.1f, -0.2f, 0.3f };

    core::ArtifactIndexEntry e;
    e.artifact_id  = trace.id;
    e.type         = core::ArtifactType::SubBottom;
    e.timestamp_us = trace.timestamp_us;
    e.file_offset  = 0;
    e.byte_length  = 1;
    e.lat          = trace.nav.lat;
    e.lon          = trace.nav.lon;
    reader.source_index.entries.push_back(e);
    reader.artifacts.push_back(trace);

    core::ArtifactIndex cache_index;
    CHECK(io::writeParsedCache(tmp.path, reader.source_index, reader, cache_index));

    io::ParsedCacheReader cr;
    CHECK(cr.open(tmp.path));
    core::ArtifactIndex read_index = cr.buildIndex();
    CHECK(read_index.size() == 1);
    CHECK(read_index.entries[0].type == core::ArtifactType::SubBottom);

    auto art = cr.readArtifact(read_index.entries[0]);
    CHECK(art.has_value());
    const auto& t = std::get<core::SubBottomTrace>(*art);
    CHECK(t.id == 200);
    CHECK_CLOSE(t.frequency_hz,   3'500.f,  1.f);
    CHECK_CLOSE(t.tow_depth_m,    8.5f,     1e-4f);
    CHECK_CLOSE(t.two_way_time_s, 0.05f,    1e-6f);
    CHECK(t.bottom_sample_idx == 42);
    CHECK(t.samples.size() == 3);
    CHECK_CLOSE(t.samples[0],  0.1f,  1e-6f);
    CHECK_CLOSE(t.samples[1], -0.2f,  1e-6f);
    CHECK_CLOSE(t.samples[2],  0.3f,  1e-6f);
}

// ---------------------------------------------------------------------------
// Test: magnetometer sample round-trip
// ---------------------------------------------------------------------------

static void test_mag_round_trip()
{
    using namespace dolphin;
    TempFile tmp;

    SyntheticReader reader;
    reader.source_index.source_id = "mag_test";

    core::MagSample mag;
    mag.id           = 300;
    mag.timestamp_us = 4'000'000;
    mag.nav.lat      = 60.0;
    mag.nav.lon      = 10.0;
    mag.nav.valid    = true;
    mag.total_nT     = 52'000.f;
    mag.residual_nT  = 12.5f;
    mag.igrf_nT      = 51'987.5f;

    core::ArtifactIndexEntry e;
    e.artifact_id  = mag.id;
    e.type         = core::ArtifactType::Magnetometer;
    e.timestamp_us = mag.timestamp_us;
    e.file_offset  = 0;
    e.byte_length  = 1;
    e.lat          = mag.nav.lat;
    e.lon          = mag.nav.lon;
    reader.source_index.entries.push_back(e);
    reader.artifacts.push_back(mag);

    core::ArtifactIndex cache_index;
    CHECK(io::writeParsedCache(tmp.path, reader.source_index, reader, cache_index));

    io::ParsedCacheReader cr;
    CHECK(cr.open(tmp.path));
    core::ArtifactIndex read_index = cr.buildIndex();
    CHECK(read_index.size() == 1);
    CHECK(read_index.entries[0].type == core::ArtifactType::Magnetometer);

    auto art = cr.readArtifact(read_index.entries[0]);
    CHECK(art.has_value());
    const auto& m = std::get<core::MagSample>(*art);
    CHECK(m.id == 300);
    CHECK_CLOSE(m.total_nT,    52'000.f,  1.f);
    CHECK_CLOSE(m.residual_nT, 12.5f,     1e-4f);
    CHECK_CLOSE(m.igrf_nT,     51'987.5f, 1.f);
    CHECK_CLOSE(m.nav.lat, 60.0, 1e-9);
    CHECK_CLOSE(m.nav.lon, 10.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Test: time span derived from index after buildIndex
// ---------------------------------------------------------------------------

static void test_time_span_from_index()
{
    using namespace dolphin;
    TempFile tmp;

    SyntheticReader reader;
    reader.source_index.source_id = "timespan_test";
    reader.add(makePing(1, 1'000'000, 0.0, 0.0));   // t = 1.0 s
    reader.add(makePing(2, 5'000'000, 0.0, 0.0));   // t = 5.0 s

    core::ArtifactIndex cache_index;
    CHECK(io::writeParsedCache(tmp.path, reader.source_index, reader, cache_index));

    io::ParsedCacheReader cr;
    CHECK(cr.open(tmp.path));
    cr.buildIndex();
    const io::FormatMeta meta = cr.metadata();

    CHECK_CLOSE(meta.start_time, 1.0, 1e-6);
    CHECK_CLOSE(meta.end_time,   5.0, 1e-6);
    CHECK(meta.artifact_count == 2);
    CHECK(meta.sidescan_ping_count == 2);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_stale_cache_detection();
    test_empty_writes_not_published();
    test_footerless_cache_repaired();
    test_sidescan_round_trip();
    test_metadata_persistence();
    test_valid_cache_accepted();
    test_nav_backfill();
    test_subbottom_round_trip();
    test_mag_round_trip();
    test_time_span_from_index();

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
