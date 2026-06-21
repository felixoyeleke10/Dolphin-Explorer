// test_raster_cache.cpp — round-trip + staleness tests for SidescanRasterCache.
// Guards the raster-first persistence: a built map raster must save and reload
// byte-identically, and a changed fingerprint must be rejected (forcing a rebuild).
#include "ui/features/map/sidescan/SidescanRasterCache.h"
#include "ui/features/map/MapTypes.h"
#include "app/display/WaterfallParams.h"
#include "app/display/NavProcessingParams.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
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
    d.preview_reduced = true;

    d.nav_track = { {-16.20, 57.30}, {-16.15, 57.35},
                    { std::nan(""), std::nan("") },   // segment break
                    {-16.10, 57.40} };

    SwathCoverage cov;
    cov.channel = core::SidescanChannel::Starboard;
    cov.ribbons = { { {0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0} },
                    { {3.0, 3.0}, {4.0, 4.0} } };
    d.coverage = { cov };

    d.intensity_w = 4; d.intensity_h = 3;
    d.intensity_disp_low = 0.10f; d.intensity_disp_high = 0.90f;
    d.intensity_cache = { 1,2,3,4, 5,6,7,8, 9,10,11,12 };  // 4*3

    d.nav_stats.total_pings   = 1234;
    d.nav_stats.invalid_nav   = 7;
    d.nav_stats.quality_used  = MapSonarQuality::High;
    d.nav_stats.pings_available = 5678;
    d.nav_stats.image_width   = 4;
    d.nav_stats.image_height  = 3;
    d.nav_stats.crs_label     = "EPSG:25828 projected exact";
    return d;
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
        CHECK(out.show_nav_track == src.show_nav_track, "show_nav_track mismatch");
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

        CHECK(out.intensity_w == 4 && out.intensity_h == 3, "intensity dims mismatch");
        CHECK(out.intensity_disp_low == src.intensity_disp_low, "disp_low mismatch");
        CHECK(out.intensity_disp_high == src.intensity_disp_high, "disp_high mismatch");
        CHECK(out.intensity_cache == src.intensity_cache, "intensity pixels mismatch");

        CHECK(out.nav_stats.total_pings == 1234, "nav_stats.total_pings mismatch");
        CHECK(out.nav_stats.quality_used == MapSonarQuality::High, "nav_stats.quality_used mismatch");
        CHECK(out.nav_stats.pings_available == 5678, "nav_stats.pings_available mismatch");
        CHECK(out.nav_stats.crs_label == "EPSG:25828 projected exact", "nav_stats.crs_label mismatch");

        CHECK(osum.has_sample_nav == true, "summary.has_sample_nav mismatch");
        CHECK(osum.sample_lon == -16.19, "summary.sample_lon mismatch");
        CHECK(osum.total_ssc_entries == 2000, "summary.total_ssc_entries mismatch");
        CHECK(osum.quality_reduced == true, "summary.quality_reduced mismatch");
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
        WaterfallParams p;
        p.tvg.enabled = true; p.tvg.spreading = 30.0f; p.tvg.absorption = 0.5f;
        p.arc.enabled = true; p.arc.exponent = 1.5f;   p.arc.gain_cap_db = 12.0f;
        p.arn.enabled = true; p.arn.strength = 0.8f;

        auto hashOf = [&](const WaterfallParams& w) {
            return rastercache::makeMeta(dummy, nav, false, MapSonarQuality::High,
                                         "EPSG:4326", w).nav_hash;
        };
        const auto base = hashOf(p);

        CHECK(hashOf(p) == base, "makeMeta not deterministic for identical params");

        // Sub-quantum drift (what a float→double→JSON→float round-trip introduces)
        // must NOT change the fingerprint.
        WaterfallParams drift = p;
        drift.tvg.spreading += 1e-6f;
        drift.arn.strength  += 2e-6f;
        CHECK(hashOf(drift) == base,
              "makeMeta fingerprint shifted under sub-quantum float drift (would re-decode on reopen)");

        // A real change must change the fingerprint (so the mosaic rebuilds).
        WaterfallParams changed = p; changed.arn.strength = 0.6f;
        CHECK(hashOf(changed) != base, "makeMeta ignored a real param change");

        WaterfallParams toggled = p; toggled.destripe.enabled = true;
        CHECK(hashOf(toggled) != base, "makeMeta ignored a stage toggle");

        WaterfallParams pal = p; pal.beam_pattern.enabled = true;
        CHECK(hashOf(pal) != base, "makeMeta ignored a beam-pattern toggle");
    }

    fs::remove(path, ec);

    if (g_failures == 0) { std::printf("test_raster_cache: ALL PASS\n"); return 0; }
    std::printf("test_raster_cache: %d FAILURE(S)\n", g_failures);
    return 1;
}
