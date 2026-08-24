// test_raster_cache.cpp — round-trip + staleness tests for SidescanRasterCache.
// Guards the raster-first persistence: a built map raster must save and reload
// byte-identically, and a changed fingerprint must be rejected (forcing a rebuild).
#include "ui/features/map/sidescan/SidescanRasterCache.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/MapTypes.h"
#include "app/display/WaterfallParams.h"
#include "app/display/NavProcessingParams.h"

#include <QDataStream>
#include <QFile>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>

using namespace dolphin;
using namespace dolphin::ui;

static int g_failures = 0;
#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; }       \
    } while (0)

static LayerMapData makeSample()
{
    LayerMapData d;
    d.kind           = LayerMapKind::Swath;
    d.lon_min = -16.2; d.lon_max = -16.1; d.lat_min = 57.3; d.lat_max = 57.4;
    d.is_projected   = false;
    d.show_nav_track = true;
    d.show_beams = true;
    d.beam_spacing = 37;
    d.preview_reduced = true;

    d.nav_track = { {-16.20, 57.30}, {-16.15, 57.35},
                    { std::nan(""), std::nan("") },   // segment break
                    {-16.10, 57.40} };

    SwathCoverage cov;
    cov.channel = core::SidescanChannel::Starboard;
    cov.ribbons = { { {0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0} },
                    { {3.0, 3.0}, {4.0, 4.0} } };
    d.coverage = { cov };
    SwathCoverage hidden = cov;
    hidden.ribbons[0][0] = QPointF(0.25, 0.25);
    d.coverage_nadir_hidden = { hidden };
    d.beam_rays = {
        {core::SidescanChannel::Port, 1'000'000, 7,
         QPointF(-16.20, 57.30), QPointF(-16.21, 57.31)},
        {core::SidescanChannel::Starboard, 1'000'000, 7,
         QPointF(-16.20, 57.30), QPointF(-16.19, 57.29)}
    };

    d.intensity_w = 4; d.intensity_h = 3;
    d.intensity_disp_low = 0.10f; d.intensity_disp_high = 0.90f;
    d.intensity_cache = { 1,2,3,4, 5,6,7,8, 9,10,11,12 };  // 4*3

    d.nav_stats.total_pings   = 1234;
    d.nav_stats.invalid_nav   = 7;
    d.nav_stats.interpolated_nav = 6;
    d.nav_stats.stitch_nav_rejects = 8;
    d.nav_stats.stitch_time_rejects = 9;
    d.nav_stats.stitch_ping_rejects = 10;
    d.nav_stats.stitch_heading_rejects = 11;
    d.nav_stats.skipped_no_position = 12;
    d.nav_stats.skipped_no_heading = 13;
    d.nav_stats.skipped_no_samples = 14;
    d.nav_stats.strips_built = 15;
    d.nav_stats.cells_attempted = 16;
    d.nav_stats.cells_rasterized = 15;
    d.nav_stats.quality_used  = MapSonarQuality::High;
    d.nav_stats.pings_available = 5678;
    d.nav_stats.image_width   = 4;
    d.nav_stats.image_height  = 3;
    d.nav_stats.crs_label     = "EPSG:25828 projected exact";
    return d;
}

static bool overwriteHeaderWord(const std::string& path,
                                qint64             offset,
                                quint32            value)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadWrite) || !file.seek(offset)) return false;

    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << value;
    return ds.status() == QDataStream::Ok;
}

template <typename T>
static bool overwriteValue(const std::string& path, qint64 offset, T value)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadWrite) || !file.seek(offset)) return false;
    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << value;
    return ds.status() == QDataStream::Ok;
}

struct CacheOffsets {
    qint64 summary_has_nav = -1;
    qint64 summary_total = -1;
    qint64 nav_count = -1;
    qint64 coverage_count = -1;
    qint64 first_ribbon_count = -1;
    qint64 first_ribbon_point_count = -1;
    qint64 beam_count = -1;
    qint64 intensity_width = -1;
    qint64 intensity_height = -1;
    qint64 pixel_count = -1;
};

static bool locateCacheOffsets(const std::string& path, CacheOffsets& offsets)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) return false;
    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);

    quint32 u32 = 0;
    quint64 u64 = 0;
    qint64 i64 = 0;
    qint32 i32 = 0;
    quint8 u8 = 0;
    bool flag = false;
    double d = 0.0;
    float f = 0.0f;

    ds >> u32 >> u32 >> u32;             // header
    ds >> u64 >> i64 >> u64 >> i32;      // Meta

    offsets.summary_has_nav = file.pos();
    ds >> flag >> d >> d >> d >> flag >> d;
    offsets.summary_total = file.pos();
    ds >> u64 >> u64 >> flag;

    ds >> u8 >> d >> d >> d >> d >> flag >> flag;

    offsets.nav_count = file.pos();
    quint64 nav_count = 0;
    ds >> nav_count;
    for (quint64 i = 0; i < nav_count; ++i) {
        QPointF point;
        ds >> point;
    }

    offsets.coverage_count = file.pos();
    quint64 coverage_count = 0;
    ds >> coverage_count;
    for (quint64 c = 0; c < coverage_count; ++c) {
        ds >> u8;
        if (c == 0) offsets.first_ribbon_count = file.pos();
        quint64 ribbon_count = 0;
        ds >> ribbon_count;
        for (quint64 r = 0; r < ribbon_count; ++r) {
            if (c == 0 && r == 0)
                offsets.first_ribbon_point_count = file.pos();
            quint64 point_count = 0;
            ds >> point_count;
            for (quint64 p = 0; p < point_count; ++p) {
                QPointF point;
                ds >> point;
            }
        }
    }

    quint64 hidden_coverage_count = 0;
    ds >> hidden_coverage_count;
    for (quint64 c = 0; c < hidden_coverage_count; ++c) {
        ds >> u8;
        quint64 ribbon_count = 0;
        ds >> ribbon_count;
        for (quint64 r = 0; r < ribbon_count; ++r) {
            quint64 point_count = 0;
            ds >> point_count;
            for (quint64 p = 0; p < point_count; ++p) {
                QPointF point;
                ds >> point;
            }
        }
    }

    offsets.beam_count = file.pos();
    quint64 beam_count = 0;
    ds >> beam_count;
    for (quint64 i = 0; i < beam_count; ++i) {
        ds >> u8;
        qint64 timestamp = 0;
        quint32 ping_number = 0;
        QPointF origin, outer;
        ds >> timestamp >> ping_number >> origin >> outer;
    }

    offsets.intensity_width = file.pos();
    ds >> i32;
    offsets.intensity_height = file.pos();
    ds >> i32 >> f >> f;
    offsets.pixel_count = file.pos();
    ds >> u64;

    return ds.status() == QDataStream::Ok &&
           offsets.first_ribbon_count >= 0 &&
           offsets.first_ribbon_point_count >= 0;
}

static bool copyFile(const std::string& source, const std::string& destination)
{
    std::error_code ec;
    return std::filesystem::copy_file(
               source, destination,
               std::filesystem::copy_options::overwrite_existing, ec) && !ec;
}

static bool rejectsWithoutMutation(const std::string& path,
                                   const rastercache::Meta& meta)
{
    LayerMapData out;
    out.kind = LayerMapKind::Profile;
    out.intensity_w = 77;
    rastercache::Summary summary;
    summary.track_m = 12345.0;
    const bool loaded = rastercache::load(path, meta, out, summary);
    return !loaded && out.kind == LayerMapKind::Profile &&
           out.intensity_w == 77 && summary.track_m == 12345.0;
}

int main()
{
    namespace fs = std::filesystem;
    const std::string path =
        (fs::temp_directory_path() / "dolphin_raster_cache_test.draster").string();
    std::error_code ec; fs::remove(path, ec);

    rastercache::Meta meta;
    meta.src_size  = 999999ULL;
    meta.src_mtime = 1718600000000LL;
    meta.nav_hash  = 0x0123456789ABCDEFULL;
    meta.quality   = static_cast<int>(MapSonarQuality::High);

    rastercache::Summary sum;
    sum.has_sample_nav     = true;
    sum.sample_lat         = 57.31;
    sum.sample_lon         = -16.19;
    sum.sample_alt         = 12.5;
    sum.sample_is_proj     = false;
    sum.track_m            = 5000.0;
    sum.total_ssc_entries  = 2000;
    sum.preview_port_count = 1000;
    sum.quality_reduced    = true;

    const LayerMapData src = makeSample();

    // -- save --
    CHECK(rastercache::save(path, meta, sum, src), "save() returned false");

    // -- load with matching meta --
    {
        LayerMapData out;
        rastercache::Summary osum;
        const bool ok = rastercache::load(path, meta, out, osum);
        CHECK(ok, "load() with matching meta returned false");

        CHECK(out.kind == src.kind, "kind mismatch");
        CHECK(out.lon_min == src.lon_min && out.lon_max == src.lon_max, "lon bounds mismatch");
        CHECK(out.lat_min == src.lat_min && out.lat_max == src.lat_max, "lat bounds mismatch");
        CHECK(out.is_projected == src.is_projected, "is_projected mismatch");
        CHECK(!out.show_nav_track,
              "cache restored live nav-track visibility state");
        CHECK(!out.show_beams,
              "cache restored project-owned beam visibility state");
        CHECK(out.beam_spacing == 10,
              "cache restored project-owned beam spacing state");
        CHECK(out.preview_reduced == src.preview_reduced, "preview_reduced mismatch");

        CHECK(out.nav_track.size() == src.nav_track.size(), "nav_track size mismatch");
        CHECK(out.nav_track[0] == src.nav_track[0], "nav_track[0] mismatch");
        CHECK(std::isnan(out.nav_track[2].x()) && std::isnan(out.nav_track[2].y()),
              "nav_track NaN gap not preserved");
        CHECK(out.nav_track[3] == src.nav_track[3], "nav_track[3] mismatch");

        CHECK(out.coverage.size() == 1, "coverage size mismatch");
        CHECK(out.coverage[0].channel == src.coverage[0].channel, "coverage channel mismatch");
        CHECK(out.coverage[0].ribbons.size() == 2, "ribbon count mismatch");
        CHECK(out.coverage[0].ribbons[0].size() == 3, "ribbon[0] size mismatch");
        CHECK(out.coverage[0].ribbons[0][1] == src.coverage[0].ribbons[0][1], "ribbon point mismatch");
        CHECK(out.coverage_nadir_hidden.size() == 1, "hidden coverage size mismatch");
        CHECK(out.coverage_nadir_hidden[0].ribbons[0][0]
              == src.coverage_nadir_hidden[0].ribbons[0][0],
              "hidden coverage point mismatch");
        CHECK(out.beam_rays.size() == src.beam_rays.size(), "beam-ray count mismatch");
        CHECK(out.beam_rays[0].channel == src.beam_rays[0].channel, "beam-ray channel mismatch");
        CHECK(out.beam_rays[0].timestamp_us == src.beam_rays[0].timestamp_us, "beam-ray timestamp mismatch");
        CHECK(out.beam_rays[0].ping_number == src.beam_rays[0].ping_number, "beam-ray ping mismatch");
        CHECK(out.beam_rays[0].origin == src.beam_rays[0].origin, "beam-ray origin mismatch");
        CHECK(out.beam_rays[0].outer == src.beam_rays[0].outer, "beam-ray endpoint mismatch");

        CHECK(out.intensity_w == 4 && out.intensity_h == 3, "intensity dims mismatch");
        CHECK(out.intensity_disp_low == src.intensity_disp_low, "disp_low mismatch");
        CHECK(out.intensity_disp_high == src.intensity_disp_high, "disp_high mismatch");
        CHECK(out.intensity_cache == src.intensity_cache, "intensity pixels mismatch");

        CHECK(out.nav_stats.total_pings == 1234, "nav_stats.total_pings mismatch");
        CHECK(out.nav_stats.interpolated_nav == 6,
              "nav_stats.interpolated_nav mismatch");
        CHECK(out.nav_stats.stitch_nav_rejects == 8,
              "nav_stats.stitch_nav_rejects mismatch");
        CHECK(out.nav_stats.stitch_time_rejects == 9,
              "nav_stats.stitch_time_rejects mismatch");
        CHECK(out.nav_stats.stitch_ping_rejects == 10,
              "nav_stats.stitch_ping_rejects mismatch");
        CHECK(out.nav_stats.stitch_heading_rejects == 11,
              "nav_stats.stitch_heading_rejects mismatch");
        CHECK(out.nav_stats.skipped_no_position == 12,
              "nav_stats.skipped_no_position mismatch");
        CHECK(out.nav_stats.skipped_no_heading == 13,
              "nav_stats.skipped_no_heading mismatch");
        CHECK(out.nav_stats.skipped_no_samples == 14,
              "nav_stats.skipped_no_samples mismatch");
        CHECK(out.nav_stats.strips_built == 15, "nav_stats.strips_built mismatch");
        CHECK(out.nav_stats.cells_attempted == 16, "nav_stats.cells_attempted mismatch");
        CHECK(out.nav_stats.cells_rasterized == 15, "nav_stats.cells_rasterized mismatch");
        CHECK(out.nav_stats.quality_used == MapSonarQuality::High, "nav_stats.quality_used mismatch");
        CHECK(out.nav_stats.pings_available == 5678, "nav_stats.pings_available mismatch");
        CHECK(out.nav_stats.crs_label == "EPSG:25828 projected exact", "nav_stats.crs_label mismatch");

        CHECK(osum.has_sample_nav == true, "summary.has_sample_nav mismatch");
        CHECK(osum.sample_lon == -16.19, "summary.sample_lon mismatch");
        CHECK(osum.total_ssc_entries == 2000, "summary.total_ssc_entries mismatch");
        CHECK(osum.quality_reduced == true, "summary.quality_reduced mismatch");
    }

    // -- revision invalidation: derived rasters are trustworthy only when both
    //    their byte layout and the raster-building algorithm are current. --
    {
        const std::string legacy_path = path + ".legacy-v1";
        const std::string old_algorithm_path = path + ".old-algorithm";
        fs::remove(legacy_path, ec);
        fs::remove(old_algorithm_path, ec);

        ec.clear();
        const bool copied_legacy = fs::copy_file(
            path, legacy_path, fs::copy_options::overwrite_existing, ec);
        CHECK(copied_legacy && !ec, "failed to create legacy-version cache fixture");
        CHECK(overwriteHeaderWord(legacy_path, sizeof(quint32), 1),
              "failed to stamp legacy cache format version");

        LayerMapData out;
        rastercache::Summary osum;
        CHECK(!rastercache::isFresh(legacy_path, meta),
              "isFresh accepted a legacy format-v1 raster");
        CHECK(!rastercache::load(legacy_path, meta, out, osum),
              "load accepted a legacy format-v1 raster");

        ec.clear();
        const bool copied_algorithm = fs::copy_file(
            path, old_algorithm_path, fs::copy_options::overwrite_existing, ec);
        CHECK(copied_algorithm && !ec, "failed to create old-algorithm cache fixture");
        CHECK(overwriteHeaderWord(
                  old_algorithm_path,
                  static_cast<qint64>(2 * sizeof(quint32)),
                  static_cast<quint32>(rastercache::kRasterAlgorithmRevision - 1)),
              "failed to stamp old raster algorithm revision");

        CHECK(!rastercache::isFresh(old_algorithm_path, meta),
              "isFresh accepted a mismatched raster algorithm revision");
        CHECK(!rastercache::load(old_algorithm_path, meta, out, osum),
              "load accepted a mismatched raster algorithm revision");

        fs::remove(legacy_path, ec);
        fs::remove(old_algorithm_path, ec);
    }

    // -- hostile/corrupt payloads: every serialized count and raster dimension
    //    is rejected before it can drive reserve(), resize(), or raw reads. --
    {
        CacheOffsets offsets;
        CHECK(locateCacheOffsets(path, offsets),
              "failed to locate raster cache fields for corruption tests");
        const std::string corrupt_path = path + ".corrupt";
        const quint64 impossible_count = (std::numeric_limits<quint64>::max)();

        CHECK(copyFile(path, corrupt_path), "failed to copy nav-count fixture");
        CHECK(overwriteValue(corrupt_path, offsets.nav_count, impossible_count),
              "failed to corrupt nav point count");
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted huge nav count or mutated outputs");

        CHECK(copyFile(path, corrupt_path), "failed to copy coverage-count fixture");
        CHECK(overwriteValue(corrupt_path, offsets.coverage_count, impossible_count),
              "failed to corrupt coverage count");
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted huge coverage count or mutated outputs");

        CHECK(copyFile(path, corrupt_path), "failed to copy ribbon-count fixture");
        CHECK(overwriteValue(corrupt_path, offsets.first_ribbon_count,
                             impossible_count),
              "failed to corrupt ribbon count");
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted huge ribbon count or mutated outputs");

        CHECK(copyFile(path, corrupt_path), "failed to copy point-count fixture");
        CHECK(overwriteValue(corrupt_path, offsets.first_ribbon_point_count,
                             impossible_count),
              "failed to corrupt ribbon point count");
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted huge ribbon point count or mutated outputs");

        CHECK(copyFile(path, corrupt_path), "failed to copy beam-count fixture");
        CHECK(overwriteValue(corrupt_path, offsets.beam_count, impossible_count),
              "failed to corrupt beam count");
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted huge beam count or mutated outputs");

        CHECK(copyFile(path, corrupt_path), "failed to copy negative-dimension fixture");
        CHECK(overwriteValue(corrupt_path, offsets.intensity_width,
                             static_cast<qint32>(-1)),
              "failed to stamp negative raster width");
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted a negative raster width or mutated outputs");

        CHECK(copyFile(path, corrupt_path), "failed to copy huge-dimension fixture");
        CHECK(overwriteValue(corrupt_path, offsets.intensity_height,
                             static_cast<qint32>(20000)),
              "failed to stamp excessive raster height");
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted an excessive raster height or mutated outputs");

        CHECK(copyFile(path, corrupt_path), "failed to copy pixel-mismatch fixture");
        CHECK(overwriteValue(corrupt_path, offsets.pixel_count,
                             static_cast<quint64>(11)),
              "failed to stamp mismatched raster pixel count");
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted iw*ih != pixel count or mutated outputs");

        CHECK(copyFile(path, corrupt_path), "failed to copy pixel-overflow fixture");
        CHECK(overwriteValue(corrupt_path, offsets.pixel_count, impossible_count),
              "failed to stamp overflowing raster pixel count");
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted overflowing raster bytes or mutated outputs");

        CHECK(copyFile(path, corrupt_path), "failed to copy truncated fixture");
        {
            QFile truncated(QString::fromStdString(corrupt_path));
            CHECK(truncated.open(QIODevice::ReadWrite),
                  "failed to open truncated fixture");
            if (truncated.isOpen())
                CHECK(truncated.resize(truncated.size() - 1),
                      "failed to truncate cache fixture");
        }
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted a truncated cache or mutated outputs");

        // Reproduce the old prebuild bug: real raster/nav data paired with an
        // absent summary must rebuild, never report a cached "no GPS" result.
        CHECK(copyFile(path, corrupt_path), "failed to copy blank-summary fixture");
        CHECK(overwriteValue(corrupt_path, offsets.summary_has_nav,
                             static_cast<quint8>(0)),
              "failed to clear cached summary nav flag");
        CHECK(overwriteValue(corrupt_path, offsets.summary_total,
                             static_cast<quint64>(0)),
              "failed to clear cached summary entry count");
        CHECK(rejectsWithoutMutation(corrupt_path, meta),
              "load accepted real raster data with an absent summary");

        LayerMapData bad_dimensions = src;
        bad_dimensions.intensity_w = 5;
        CHECK(!rastercache::save(path + ".bad-save", meta, sum, bad_dimensions),
              "save accepted inconsistent raster dimensions");
        rastercache::Summary blank_summary;
        CHECK(!rastercache::save(path + ".blank-summary", meta, blank_summary, src),
              "save accepted real raster data with a blank summary");

        fs::remove(corrupt_path, ec);
        fs::remove(path + ".bad-save", ec);
        fs::remove(path + ".blank-summary", ec);
    }

    // -- staleness: each differing meta field must reject the cache --
    {
        LayerMapData out; rastercache::Summary osum;
        rastercache::Meta m2 = meta; m2.nav_hash ^= 1ULL;
        CHECK(!rastercache::load(path, m2, out, osum), "stale nav_hash accepted");
        m2 = meta; m2.src_size += 1;
        CHECK(!rastercache::load(path, m2, out, osum), "stale src_size accepted");
        m2 = meta; m2.src_mtime += 1;
        CHECK(!rastercache::load(path, m2, out, osum), "stale src_mtime accepted");
        m2 = meta; m2.quality = static_cast<int>(MapSonarQuality::Medium);
        CHECK(!rastercache::load(path, m2, out, osum), "stale quality accepted");
    }

    // -- missing file --
    {
        LayerMapData out; rastercache::Summary osum;
        CHECK(!rastercache::load(path + ".nope", meta, out, osum),
              "load() of missing file returned true");
    }

    // -- isFresh: matching meta true; stale / missing false (header-only probe) --
    {
        CHECK(rastercache::isFresh(path, meta), "isFresh rejected a matching cache");
        rastercache::Meta m2 = meta; m2.nav_hash ^= 1ULL;
        CHECK(!rastercache::isFresh(path, m2), "isFresh accepted a stale nav_hash");
        m2 = meta; m2.quality = static_cast<int>(MapSonarQuality::Medium);
        CHECK(!rastercache::isFresh(path, m2), "isFresh accepted a stale quality");
        CHECK(!rastercache::isFresh(path + ".nope", meta),
              "isFresh accepted a missing file");
    }

    // -- makeMeta fingerprint: stable across a save/reload float round-trip, but
    //    sensitive to real parameter changes. This is the guarantee that reopening a
    //    project (params reloaded from JSON) reuses the persisted raster instead of
    //    re-decoding pings. --
    {
        const std::string dummy = path + ".src";   // need not exist; hash ignores file size
        NavProcessingParams nav;
        SssGeorefParams georef;
        WaterfallParams p;
        p.tvg.enabled = true; p.tvg.spreading = 30.0f; p.tvg.absorption = 0.5f;
        p.arc.enabled = true; p.arc.exponent = 1.5f;   p.arc.gain_cap_db = 12.0f;
        p.arn.enabled = true; p.arn.strength = 0.8f;

        auto hashOf = [&](const WaterfallParams& w, const SssGeorefParams& g) {
            return rastercache::makeMeta(dummy, nav, g, MapSonarQuality::High,
                                         "EPSG:4326", w).nav_hash;
        };
        const auto base = hashOf(p, georef);

        CHECK(hashOf(p, georef) == base, "makeMeta not deterministic for identical params");
        SssGeorefParams hidden_nadir = georef;
        hidden_nadir.show_nadir = !georef.show_nadir;
        CHECK(hashOf(p, hidden_nadir) == base,
              "display-only nadir toggle invalidated the raster cache");

        // Sub-quantum drift (what a float→double→JSON→float round-trip introduces)
        // must NOT change the fingerprint.
        WaterfallParams drift = p;
        drift.tvg.spreading += 1e-6f;
        drift.arn.strength  += 2e-6f;
        CHECK(hashOf(drift, georef) == base,
              "makeMeta fingerprint shifted under sub-quantum float drift (would re-decode on reopen)");

        // A real change must change the fingerprint (so the mosaic rebuilds).
        WaterfallParams changed = p; changed.arn.strength = 0.6f;
        CHECK(hashOf(changed, georef) != base, "makeMeta ignored a real param change");

        WaterfallParams toggled = p; toggled.destripe.enabled = true;
        CHECK(hashOf(toggled, georef) != base, "makeMeta ignored a stage toggle");

        WaterfallParams pal = p; pal.beam_pattern.enabled = true;
        CHECK(hashOf(pal, georef) != base, "makeMeta ignored a beam-pattern toggle");

        auto expectImagingChange = [&](auto mutate, const char* message) {
            WaterfallParams changed_imaging = p;
            mutate(changed_imaging);
            CHECK(hashOf(changed_imaging, georef) != base, message);
        };
        expectImagingChange([](auto& w) { w.arn.gain_cap_db = 18.f; },
                           "makeMeta ignored ARN gain cap");
        expectImagingChange([](auto& w) { w.arn.column_smooth = 19; },
                           "makeMeta ignored ARN smoothing");
        expectImagingChange([](auto& w) { w.destripe.window = 71; },
                           "makeMeta ignored destripe window");
        expectImagingChange([](auto& w) { w.destripe.subdivision = 9; },
                           "makeMeta ignored destripe subdivision");
        expectImagingChange([](auto& w) { w.destripe.capping = 3.f; },
                           "makeMeta ignored destripe cap");
        expectImagingChange([](auto& w) { w.destripe.threshold_db = 2.f; },
                           "makeMeta ignored destripe threshold");
        expectImagingChange([](auto& w) { w.beam_pattern.strength = 0.6f; },
                           "makeMeta ignored beam-pattern strength");
        expectImagingChange([](auto& w) { w.beam_pattern.smooth_radius = 23; },
                           "makeMeta ignored beam-pattern smoothing");
        expectImagingChange([](auto& w) { w.beam_pattern.gain_cap_db = 18.f; },
                           "makeMeta ignored beam-pattern gain cap");
        expectImagingChange([](auto& w) { w.ml_enhance.enabled = true; },
                           "makeMeta ignored adaptive-contrast enable");
        expectImagingChange([](auto& w) { w.ml_enhance.tile_pings = 96; },
                           "makeMeta ignored adaptive-contrast ping tile");
        expectImagingChange([](auto& w) { w.ml_enhance.tile_samps = 192; },
                           "makeMeta ignored adaptive-contrast sample tile");
        expectImagingChange([](auto& w) { w.ml_enhance.clip_limit = 3.f; },
                           "makeMeta ignored adaptive-contrast clip limit");

        // Every AGC control changes the rendered amplitudes and therefore must
        // invalidate both 2D and 3D map mosaics.
        auto expectAgcChange = [&](auto mutate, const char* message) {
            WaterfallParams changed_agc = p;
            mutate(changed_agc.agc);
            CHECK(hashOf(changed_agc, georef) != base, message);
        };
        expectAgcChange([](auto& a) { a.enabled = true; },
                        "makeMeta ignored AGC enable");
        expectAgcChange([](auto& a) { a.mode = dolphin::app::AgcMode::Variable; },
                        "makeMeta ignored AGC mode");
        expectAgcChange([](auto& a) { a.strength = 0.7f; },
                        "makeMeta ignored AGC strength");
        expectAgcChange([](auto& a) { a.along_track_win = 73; },
                        "makeMeta ignored AGC along-track window");
        expectAgcChange([](auto& a) { a.smoothing_type = dolphin::app::AgcSmoothingType::Median; },
                        "makeMeta ignored AGC smoothing type");
        expectAgcChange([](auto& a) { a.smoothing_win = 11; },
                        "makeMeta ignored AGC smoothing window");
        expectAgcChange([](auto& a) { a.edge_skip_samples = 91; },
                        "makeMeta ignored AGC edge skip");
        expectAgcChange([](auto& a) { a.noise_floor_pct = 4.f; },
                        "makeMeta ignored AGC noise floor");
        expectAgcChange([](auto& a) { a.gain_cap_db = 18.f; },
                        "makeMeta ignored AGC gain cap");

        WaterfallParams arc_exponent = p;
        arc_exponent.arc.exponent = 2.3f;
        CHECK(hashOf(arc_exponent, georef) != base,
              "makeMeta ignored ARC exponent");
        WaterfallParams arc_cap = p;
        arc_cap.arc.gain_cap_db = 18.f;
        CHECK(hashOf(arc_cap, georef) != base,
              "makeMeta ignored ARC gain cap");

        auto expectGeorefChange = [&](auto mutate, const char* message) {
            SssGeorefParams changed_georef = georef;
            mutate(changed_georef);
            CHECK(hashOf(p, changed_georef) != base, message);
        };
        expectGeorefChange([](SssGeorefParams& g) {
            g.nav_source = SssNavPositionSource::FishSensor;
        }, "makeMeta ignored georef nav_source");
        expectGeorefChange([](SssGeorefParams& g) {
            g.heading_source = SssHeadingSource::VesselShip;
        }, "makeMeta ignored georef heading_source");
        expectGeorefChange([](SssGeorefParams& g) { g.enable_layback = true; },
                           "makeMeta ignored georef enable_layback");
        expectGeorefChange([](SssGeorefParams& g) { g.use_file_layback = false; },
                           "makeMeta ignored georef use_file_layback");
        expectGeorefChange([](SssGeorefParams& g) { g.manual_layback_m = 12.5; },
                           "makeMeta ignored georef manual_layback_m");
        expectGeorefChange([](SssGeorefParams& g) { g.x_offset_m = 1.25; },
                           "makeMeta ignored georef x_offset_m");
        expectGeorefChange([](SssGeorefParams& g) { g.y_offset_m = -2.5; },
                           "makeMeta ignored georef y_offset_m");
        expectGeorefChange([](SssGeorefParams& g) { g.heading_offset_deg = 3.0; },
                           "makeMeta ignored georef heading_offset_deg");
        expectGeorefChange([](SssGeorefParams& g) { g.swap_port_starboard = true; },
                           "makeMeta ignored georef swap_port_starboard");
        expectGeorefChange([](SssGeorefParams& g) {
            g.smoothing_mode = SssNavSmoothingMode::MovingAverage;
        }, "makeMeta ignored georef smoothing_mode");
        expectGeorefChange([](SssGeorefParams& g) { g.smoothing_window = 9; },
                           "makeMeta ignored georef smoothing_window");
        expectGeorefChange([](SssGeorefParams& g) { g.debug_ping_lines_only = true; },
                           "makeMeta ignored georef debug_ping_lines_only");
        expectGeorefChange([](SssGeorefParams& g) {
                               g.presentation_domain = core::SidescanRangeDomain::Ground;
                           },
                           "makeMeta ignored georef slant_range_corrected");

        SssGeorefParams georef_drift = georef;
        georef_drift.x_offset_m += 1e-6;
        CHECK(hashOf(p, georef_drift) == base,
              "makeMeta georef fingerprint shifted under sub-quantum float drift");
    }

    // The application-wide auto-stretch switch must have literal, identical
    // semantics on the map: identity bounds inherit the cached line stretch
    // only while enabled. Explicit display bounds always override the switch.
    {
        IntensityCache intensity;
        intensity.w = 1;
        intensity.h = 1;
        intensity.disp_low = 0.10f;
        intensity.disp_high = 0.20f;
        intensity.pixels = std::make_shared<std::vector<uint16_t>>(
            std::initializer_list<uint16_t>{6555}); // stored amplitude+1 ~= 10%

        const auto automatic = SidescanViewController::colorizeIntensityCache(
            intensity, std::nullopt, PaletteIndex::Greyscale, true);
        const auto full_range = SidescanViewController::colorizeIntensityCache(
            intensity, std::nullopt, PaletteIndex::Greyscale, false);
        CHECK(!automatic.isNull() && !full_range.isNull(),
              "map auto-stretch colourization returned a null image");
        CHECK(automatic.pixel(0, 0) != full_range.pixel(0, 0),
              "map ignored the disabled auto-stretch setting");

        SonarDisplayParams explicit_range;
        explicit_range.display_low = 0.10f;
        explicit_range.display_high = 0.20f;
        const auto explicit_on = SidescanViewController::colorizeIntensityCache(
            intensity, explicit_range, PaletteIndex::Greyscale, true);
        const auto explicit_off = SidescanViewController::colorizeIntensityCache(
            intensity, explicit_range, PaletteIndex::Greyscale, false);
        CHECK(explicit_on.pixel(0, 0) == explicit_off.pixel(0, 0),
              "auto-stretch overrode an explicit map display range");

        QImage gpu_image = makeGpuIntensityImage(intensity);
        CHECK(!gpu_image.isNull() && gpu_image.format() == QImage::Format_Grayscale16,
              "GPU intensity handoff did not produce Grayscale16");
        intensity.pixels.reset();
        CHECK(*reinterpret_cast<const uint16_t*>(gpu_image.constBits()) == 6555,
              "GPU intensity handoff did not retain shared buffer lifetime");
    }

    fs::remove(path, ec);

    if (g_failures == 0) { std::printf("test_raster_cache: ALL PASS\n"); return 0; }
    std::printf("test_raster_cache: %d FAILURE(S)\n", g_failures);
    return 1;
}
