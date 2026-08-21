// SidescanRasterCache.cpp — binary (de)serialization of a built map raster.
#include "ui/features/map/sidescan/SidescanRasterCache.h"
#include "pipeline/SidescanEnhancementAlgorithms.h"
#include "pipeline/SidescanRadiometryAlgorithms.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>
#include <QtGlobal>

#include <cmath>
#include <limits>

namespace dolphin::ui::rastercache {

namespace {

constexpr quint32 kMagic = 0x44525331; // "DRS1"

// Defensive decode limits. Current shipped map tiers are at most 4096 px and
// 16k ping groups, so these retain ample forward headroom while preventing a
// corrupt sidecar from requesting unbounded memory.
constexpr quint64 kMaxCacheFileBytes       = 512ULL * 1024ULL * 1024ULL;
constexpr quint64 kMaxPointsPerVector      = 4ULL * 1024ULL * 1024ULL;
constexpr quint64 kMaxTotalPoints          = 8ULL * 1024ULL * 1024ULL;
constexpr quint64 kMaxCoverageCollections = 64;
constexpr quint64 kMaxRibbonsPerCoverage  = 1024ULL * 1024ULL;
constexpr quint64 kMaxTotalRibbons         = 2ULL * 1024ULL * 1024ULL;
constexpr quint64 kMaxBeamRays             = 4ULL * 1024ULL * 1024ULL;
constexpr qint32  kMaxRasterDimension      = 16384;
constexpr quint64 kMaxRasterPixels         = 64ULL * 1024ULL * 1024ULL;
constexpr quint32 kMaxDiagnosticStringBytes = 64U * 1024U;
constexpr quint64 kSerializedPointBytes     = 2ULL * sizeof(double);

constexpr unsigned long long kFnvOffset = 1469598103934665603ULL;
constexpr unsigned long long kFnvPrime  = 1099511628211ULL;

template <typename T>
unsigned long long fnvMix(unsigned long long h, const T& v) {
    const auto* p = reinterpret_cast<const unsigned char*>(&v);
    for (size_t i = 0; i < sizeof(T); ++i) { h ^= p[i]; h *= kFnvPrime; }
    return h;
}

// Tolerance-based mix for floating-point parameters. Quantizes to 1e-4 before
// hashing so the float→double→JSON→double→float round-trip on project save/reload
// can't shift the fingerprint and cause a spurious cache miss (which would force a
// full ping re-decode on every reopen). 1e-4 resolution is well below any UI step.
unsigned long long fnvMixF(unsigned long long h, double v) {
    return fnvMix(h, static_cast<long long>(std::llround(v * 10000.0)));
}

bool fitsSizeT(quint64 value) {
    return value <= static_cast<quint64>((std::numeric_limits<size_t>::max)());
}

bool checkedMultiply(quint64 a, quint64 b, quint64& product) {
    if (a != 0 && b > (std::numeric_limits<quint64>::max)() / a)
        return false;
    product = a * b;
    return true;
}

bool hasRemainingBytes(QDataStream& ds, quint64 required) {
    auto* device = ds.device();
    if (!device) return false;
    const qint64 available = device->bytesAvailable();
    return available >= 0 && required <= static_cast<quint64>(available);
}

bool isKnownQuality(qint32 value) {
    switch (static_cast<MapSonarQuality>(value)) {
    case MapSonarQuality::Off:
    case MapSonarQuality::CoverageOnly:
    case MapSonarQuality::Low:
    case MapSonarQuality::Medium:
    case MapSonarQuality::High:
        return true;
    }
    return false;
}

bool isKnownLayerKind(quint8 value) {
    return value <= static_cast<quint8>(LayerMapKind::Profile);
}

bool isKnownChannel(quint8 value) {
    return value == static_cast<quint8>(core::SidescanChannel::Port) ||
           value == static_cast<quint8>(core::SidescanChannel::Starboard);
}

bool validateRasterLayout(qint64 iw,
                          qint64 ih,
                          quint64 pixel_count,
                          quint64* raw_bytes = nullptr) {
    if (iw < 0 || ih < 0 || iw > kMaxRasterDimension || ih > kMaxRasterDimension)
        return false;

    if (iw == 0 || ih == 0) {
        if (iw != 0 || ih != 0 || pixel_count != 0) return false;
        if (raw_bytes) *raw_bytes = 0;
        return true;
    }

    quint64 expected_pixels = 0;
    if (!checkedMultiply(static_cast<quint64>(iw), static_cast<quint64>(ih),
                         expected_pixels) ||
        expected_pixels > kMaxRasterPixels ||
        pixel_count != expected_pixels ||
        !fitsSizeT(pixel_count))
        return false;

    quint64 bytes = 0;
    if (!checkedMultiply(pixel_count, sizeof(uint16_t), bytes) ||
        bytes > static_cast<quint64>((std::numeric_limits<int>::max)()))
        return false;
    if (raw_bytes) *raw_bytes = bytes;
    return true;
}

struct ReadBudget {
    quint64 points_remaining  = kMaxTotalPoints;
    quint64 ribbons_remaining = kMaxTotalRibbons;
};

// -- QPointF vector helpers ----------------------------------------------------
void writePoints(QDataStream& ds, const std::vector<QPointF>& pts) {
    ds << static_cast<quint64>(pts.size());
    for (const auto& p : pts) ds << p;
}
bool readPoints(QDataStream& ds,
                std::vector<QPointF>& pts,
                ReadBudget& budget) {
    quint64 n = 0;
    ds >> n;
    quint64 minimum_bytes = 0;
    if (ds.status() != QDataStream::Ok ||
        n > kMaxPointsPerVector ||
        n > budget.points_remaining ||
        !fitsSizeT(n) ||
        !checkedMultiply(n, kSerializedPointBytes, minimum_bytes) ||
        !hasRemainingBytes(ds, minimum_bytes))
        return false;

    std::vector<QPointF> decoded;
    decoded.reserve(static_cast<size_t>(n));
    for (quint64 i = 0; i < n; ++i) {
        QPointF point;
        ds >> point;
        if (ds.status() != QDataStream::Ok) return false;
        decoded.push_back(point);
    }
    budget.points_remaining -= n;
    pts = std::move(decoded);
    return true;
}

bool readSizeCounter(QDataStream& ds, size_t& out) {
    quint64 value = 0;
    ds >> value;
    if (ds.status() != QDataStream::Ok || !fitsSizeT(value)) return false;
    out = static_cast<size_t>(value);
    return true;
}

bool readBoundedString(QDataStream& ds, std::string& out) {
    auto* device = ds.device();
    if (!device || device->isSequential()) return false;

    const qint64 prefix_pos = device->pos();
    quint32 byte_count = 0;
    ds >> byte_count;
    if (ds.status() != QDataStream::Ok) return false;

    const bool is_null = byte_count == 0xFFFFFFFFU;
    if (!is_null &&
        ((byte_count & 1U) != 0U ||
         byte_count > kMaxDiagnosticStringBytes ||
         !hasRemainingBytes(ds, byte_count)))
        return false;

    // Rewind after validating QString's on-disk UTF-16 byte count, then let Qt
    // perform the endian-aware decode without exposing its allocator to an
    // untrusted length.
    if (!device->seek(prefix_pos)) return false;
    QString decoded;
    ds >> decoded;
    if (ds.status() != QDataStream::Ok) return false;
    out = decoded.toStdString();
    return true;
}

// -- NavStats (persist the diagnostics-relevant subset) ------------------------
void writeNavStats(QDataStream& ds, const NavStats& s) {
    ds << static_cast<quint64>(s.total_pings)
       << static_cast<quint64>(s.invalid_nav)
       << static_cast<quint64>(s.interpolated_nav)
       << static_cast<quint64>(s.repeated_fixes)
       << static_cast<quint64>(s.nav_spikes)
       << static_cast<quint64>(s.time_gaps)
       << s.avg_spacing_m << s.max_spacing_m
       << static_cast<quint64>(s.stitch_nav_rejects)
       << static_cast<quint64>(s.stitch_time_rejects)
       << static_cast<quint64>(s.stitch_ping_rejects)
       << static_cast<quint64>(s.stitch_heading_rejects)
       << static_cast<quint64>(s.skipped_no_position)
       << static_cast<quint64>(s.skipped_no_heading)
       << static_cast<quint64>(s.skipped_no_samples)
       << static_cast<quint64>(s.strips_built)
       << static_cast<quint64>(s.cells_attempted)
       << static_cast<quint64>(s.cells_rasterized)
       << static_cast<quint64>(s.preview_pixels_written)
       << static_cast<quint64>(s.preview_pixels_filled)
       << static_cast<quint64>(s.coverage_ribbons_built)
       << s.nav_lon_min << s.nav_lon_max << s.nav_lat_min << s.nav_lat_max
       << s.strip_lon_min << s.strip_lon_max << s.strip_lat_min << s.strip_lat_max
       << static_cast<qint32>(s.image_width) << static_cast<qint32>(s.image_height)
       << static_cast<qint32>(s.quality_used)
       << static_cast<quint64>(s.pings_available)
       << s.memory_reduced
       << QString::fromStdString(s.crs_label)
       << QString::fromStdString(s.unsupported_crs_id);
}
bool readNavStats(QDataStream& ds, NavStats& s) {
    qint32 i = 0;
    if (!readSizeCounter(ds, s.total_pings) ||
        !readSizeCounter(ds, s.invalid_nav) ||
        !readSizeCounter(ds, s.interpolated_nav) ||
        !readSizeCounter(ds, s.repeated_fixes) ||
        !readSizeCounter(ds, s.nav_spikes) ||
        !readSizeCounter(ds, s.time_gaps))
        return false;
    ds >> s.avg_spacing_m >> s.max_spacing_m;
    if (ds.status() != QDataStream::Ok ||
        !readSizeCounter(ds, s.stitch_nav_rejects) ||
        !readSizeCounter(ds, s.stitch_time_rejects) ||
        !readSizeCounter(ds, s.stitch_ping_rejects) ||
        !readSizeCounter(ds, s.stitch_heading_rejects) ||
        !readSizeCounter(ds, s.skipped_no_position) ||
        !readSizeCounter(ds, s.skipped_no_heading) ||
        !readSizeCounter(ds, s.skipped_no_samples) ||
        !readSizeCounter(ds, s.strips_built) ||
        !readSizeCounter(ds, s.cells_attempted) ||
        !readSizeCounter(ds, s.cells_rasterized) ||
        !readSizeCounter(ds, s.preview_pixels_written) ||
        !readSizeCounter(ds, s.preview_pixels_filled) ||
        !readSizeCounter(ds, s.coverage_ribbons_built))
        return false;
    ds >> s.nav_lon_min >> s.nav_lon_max >> s.nav_lat_min >> s.nav_lat_max;
    ds >> s.strip_lon_min >> s.strip_lon_max >> s.strip_lat_min >> s.strip_lat_max;
    ds >> i; s.image_width = i;
    ds >> i; s.image_height = i;
    ds >> i;
    if (ds.status() != QDataStream::Ok || !isKnownQuality(i) ||
        s.image_width < 0 || s.image_height < 0 ||
        s.image_width > kMaxRasterDimension ||
        s.image_height > kMaxRasterDimension)
        return false;
    s.quality_used = static_cast<MapSonarQuality>(i);
    if (!readSizeCounter(ds, s.pings_available)) return false;
    ds >> s.memory_reduced;
    if (ds.status() != QDataStream::Ok ||
        !readBoundedString(ds, s.crs_label) ||
        !readBoundedString(ds, s.unsupported_crs_id))
        return false;
    return ds.status() == QDataStream::Ok;
}

bool validatePointsForSave(const std::vector<QPointF>& points,
                           quint64& total_points) {
    const quint64 count = static_cast<quint64>(points.size());
    if (count > kMaxPointsPerVector || count > kMaxTotalPoints - total_points)
        return false;
    total_points += count;
    return true;
}

bool validateForSave(const LayerMapData& data) {
    quint64 total_points = 0;
    quint64 total_ribbons = 0;
    if (!isKnownLayerKind(static_cast<quint8>(data.kind)) ||
        !isKnownQuality(static_cast<qint32>(data.nav_stats.quality_used)) ||
        !validatePointsForSave(data.nav_track, total_points) ||
        data.coverage.size() > kMaxCoverageCollections)
        return false;

    for (const auto& coverage : data.coverage) {
        if (!isKnownChannel(static_cast<quint8>(coverage.channel)) ||
            coverage.ribbons.size() > kMaxRibbonsPerCoverage ||
            coverage.ribbons.size() > kMaxTotalRibbons - total_ribbons)
            return false;
        total_ribbons += static_cast<quint64>(coverage.ribbons.size());
        for (const auto& ribbon : coverage.ribbons)
            if (!validatePointsForSave(ribbon, total_points)) return false;
    }

    if (data.beam_rays.size() > kMaxBeamRays)
        return false;
    for (const auto& ray : data.beam_rays) {
        if (!isKnownChannel(static_cast<quint8>(ray.channel)) ||
            !std::isfinite(ray.origin.x()) || !std::isfinite(ray.origin.y()) ||
            !std::isfinite(ray.outer.x()) || !std::isfinite(ray.outer.y()))
            return false;
    }

    quint64 raw_bytes = 0;
    if (!validateRasterLayout(data.intensity_w, data.intensity_h,
                              static_cast<quint64>(data.intensity_cache.size()),
                              &raw_bytes))
        return false;

    const auto stringFits = [](const std::string& value) {
        const QString decoded = QString::fromStdString(value);
        return static_cast<quint64>(decoded.size()) * sizeof(char16_t) <=
               kMaxDiagnosticStringBytes;
    };
    return stringFits(data.nav_stats.crs_label) &&
           stringFits(data.nav_stats.unsupported_crs_id) &&
           data.nav_stats.image_width >= 0 &&
           data.nav_stats.image_height >= 0 &&
           data.nav_stats.image_width <= kMaxRasterDimension &&
           data.nav_stats.image_height <= kMaxRasterDimension;
}

void writeMeta(QDataStream& ds, const Meta& m) {
    ds << static_cast<quint64>(m.src_size)
       << static_cast<qint64>(m.src_mtime)
       << static_cast<quint64>(m.nav_hash)
       << static_cast<qint32>(m.quality);
}
bool readMeta(QDataStream& ds, Meta& m) {
    quint64 sz = 0, nh = 0;
    qint64 mt = 0;
    qint32 q = 0;
    ds >> sz >> mt >> nh >> q;
    if (ds.status() != QDataStream::Ok || !isKnownQuality(q)) return false;
    m.src_size = sz; m.src_mtime = mt; m.nav_hash = nh; m.quality = q;
    return true;
}

void writeHeader(QDataStream& ds) {
    ds << kMagic
       << static_cast<quint32>(kCacheFormatVersion)
       << static_cast<quint32>(kRasterAlgorithmRevision);
}

bool readCurrentHeader(QDataStream& ds) {
    quint32 magic = 0;
    quint32 format_version = 0;
    ds >> magic >> format_version;

    // Version 1 ended here and placed Meta next. Reject it before interpreting
    // any legacy bytes as the algorithm revision.
    if (ds.status() != QDataStream::Ok ||
        magic != kMagic ||
        format_version != static_cast<quint32>(kCacheFormatVersion))
        return false;

    quint32 algorithm_revision = 0;
    ds >> algorithm_revision;
    return ds.status() == QDataStream::Ok &&
           algorithm_revision == static_cast<quint32>(kRasterAlgorithmRevision);
}

bool summaryMatchesData(const Summary& summary, const LayerMapData& data) {
    if (!std::isfinite(summary.track_m) || summary.track_m < 0.0 ||
        summary.preview_port_count > summary.total_ssc_entries)
        return false;

    bool track_has_position = false;
    for (const QPointF& point : data.nav_track) {
        if (std::isfinite(point.x()) && std::isfinite(point.y())) {
            track_has_position = true;
            break;
        }
    }

    if (summary.has_sample_nav) {
        return std::isfinite(summary.sample_lat) &&
               std::isfinite(summary.sample_lon) &&
               summary.total_ssc_entries > 0;
    }

    // Early tier-prebuild caches wrote an all-zero Summary beside real map data.
    // Treat that combination as stale so the normal build regenerates an honest
    // status summary instead of reporting "no GPS" from a cache hit.
    if (summary.total_ssc_entries == 0 &&
        (track_has_position || data.nav_stats.total_pings > 0 || !data.coverage.empty() ||
         !data.intensity_cache.empty()))
        return false;
    return true;
}

void completeSummaryFromData(Summary& summary, const LayerMapData& data) {
    if (summary.has_sample_nav || summary.total_ssc_entries == 0) return;
    for (const QPointF& point : data.nav_track) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) continue;
        summary.has_sample_nav = true;
        summary.sample_lon = point.x();
        summary.sample_lat = point.y();
        summary.sample_alt = 0.0;
        summary.sample_is_proj = data.is_projected;
        return;
    }
}

} // namespace

std::string cachePath(const std::string& store_path,
                      const std::string& layer_id,
                      MapSonarQuality    quality) {
    return store_path + "." + layer_id + ".q"
         + std::to_string(static_cast<int>(quality)) + ".draster";
}

Meta makeMeta(const std::string&         store_path,
              const NavProcessingParams& nav,
              const SssGeorefParams&     georef,
              MapSonarQuality            quality,
              const std::string&         display_crs_id,
              const WaterfallParams&     sss) {
    Meta m;
    const QFileInfo fi(QString::fromStdString(store_path));
    if (fi.exists()) {
        m.src_size  = static_cast<unsigned long long>(fi.size());
        m.src_mtime = fi.lastModified().toMSecsSinceEpoch();
    }
    unsigned long long h = kFnvOffset;
    h = fnvMix (h, nav.smooth_enabled);
    h = fnvMix (h, nav.smooth_window);
    h = fnvMix (h, nav.layback_enabled);
    h = fnvMixF(h, nav.layback_m);
    h = fnvMixF(h, nav.heading_offset_deg);
    h = fnvMixF(h, nav.pitch_offset_deg);
    h = fnvMixF(h, nav.roll_offset_deg);

    // SSS georeferencing changes the world-space cells, coverage, and sometimes
    // which pings can be written. Every field participates in freshness so a
    // changed source/offset/smoothing policy can never reuse old geometry.
    h = fnvMix (h, static_cast<int>(georef.nav_source));
    h = fnvMix (h, static_cast<int>(georef.heading_source));
    h = fnvMix (h, georef.enable_layback);
    h = fnvMix (h, georef.use_file_layback);
    h = fnvMixF(h, georef.manual_layback_m);
    h = fnvMixF(h, georef.x_offset_m);
    h = fnvMixF(h, georef.y_offset_m);
    h = fnvMixF(h, georef.heading_offset_deg);
    h = fnvMix (h, georef.swap_port_starboard);
    h = fnvMix (h, static_cast<int>(georef.smoothing_mode));
    h = fnvMix (h, georef.smoothing_window);
    h = fnvMix (h, georef.debug_ping_lines_only);
    h = fnvMix (h, georef.slant_range_corrected);
    // show_nadir is display state. Each cache contains both footprints, so a
    // checkbox toggle must not invalidate the expensive amplitude raster.
    for (char c : display_crs_id) h = fnvMix(h, c);

    // Gain/imaging corrections — changing any re-rasterizes the mosaic. Floats are
    // quantized (fnvMixF) so a save/reload round-trip keeps the same fingerprint.
    h = fnvMix(h, pipeline::enhancement::kAlgorithmRevision);
    h = fnvMix(h, pipeline::radiometry::kAlgorithmRevision);
    h = fnvMix (h, sss.tvg.enabled);    h = fnvMixF(h, sss.tvg.spreading);   h = fnvMixF(h, sss.tvg.absorption);
    h = fnvMixF(h, sss.tvg.fallback_blanking_m);
    h = fnvMix (h, sss.arc.enabled);    h = fnvMixF(h, sss.arc.exponent);    h = fnvMixF(h, sss.arc.gain_cap_db);
    h = fnvMix (h, sss.agc.enabled);    h = fnvMix(h, static_cast<int>(sss.agc.mode));
    h = fnvMixF(h, sss.agc.strength);   h = fnvMix(h, sss.agc.along_track_win);
    h = fnvMix(h, static_cast<int>(sss.agc.smoothing_type));
    h = fnvMix(h, sss.agc.smoothing_win); h = fnvMix(h, sss.agc.edge_skip_samples);
    h = fnvMixF(h, sss.agc.noise_floor_pct); h = fnvMixF(h, sss.agc.gain_cap_db);
    h = fnvMixF(h, sss.agc.target_mean);
    h = fnvMix (h, sss.arn.enabled);    h = fnvMixF(h, sss.arn.strength);    h = fnvMixF(h, sss.arn.gain_cap_db);
    h = fnvMix (h, sss.arn.column_smooth);
    h = fnvMix (h, sss.destripe.enabled);     h = fnvMix (h, sss.destripe.window);
    h = fnvMix (h, sss.destripe.subdivision); h = fnvMixF(h, sss.destripe.capping);
    h = fnvMixF(h, sss.destripe.threshold_db);
    h = fnvMix (h, sss.beam_pattern.enabled); h = fnvMixF(h, sss.beam_pattern.strength);
    h = fnvMix (h, sss.beam_pattern.smooth_radius);
    h = fnvMixF(h, sss.beam_pattern.gain_cap_db);
    h = fnvMix (h, sss.ml_enhance.enabled);   h = fnvMix (h, sss.ml_enhance.tile_pings);
    h = fnvMix (h, sss.ml_enhance.tile_samps); h = fnvMixF(h, sss.ml_enhance.clip_limit);
    m.nav_hash = h;
    m.quality  = static_cast<int>(quality);
    return m;
}

bool isFresh(const std::string& path, const Meta& expect) {
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly)) return false;
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    if (!readCurrentHeader(ds)) return false;
    Meta meta;
    return readMeta(ds, meta)
        && meta.src_size  == expect.src_size
        && meta.src_mtime == expect.src_mtime
        && meta.nav_hash  == expect.nav_hash
        && meta.quality   == expect.quality;
}

bool save(const std::string&  path,
          const Meta&         meta,
          const Summary&      summary,
          const LayerMapData& data) {
    if (!isKnownQuality(meta.quality) ||
        !validateForSave(data) || !summaryMatchesData(summary, data))
        return false;

    QSaveFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::WriteOnly)) return false;

    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    writeHeader(ds);
    writeMeta(ds, meta);

    // Summary
    ds << summary.has_sample_nav << summary.sample_lat << summary.sample_lon
       << summary.sample_alt << summary.sample_is_proj << summary.track_m
       << static_cast<quint64>(summary.total_ssc_entries)
       << static_cast<quint64>(summary.preview_port_count)
       << summary.quality_reduced;

    // LayerMapData
    ds << static_cast<quint8>(data.kind)
       << data.lon_min << data.lon_max << data.lat_min << data.lat_max
       << data.is_projected << data.preview_reduced;

    writePoints(ds, data.nav_track);

    ds << static_cast<quint64>(data.coverage.size());
    for (const auto& cov : data.coverage) {
        ds << static_cast<quint8>(cov.channel);
        ds << static_cast<quint64>(cov.ribbons.size());
        for (const auto& ribbon : cov.ribbons) writePoints(ds, ribbon);
    }

    ds << static_cast<quint64>(data.coverage_nadir_hidden.size());
    for (const auto& cov : data.coverage_nadir_hidden) {
        ds << static_cast<quint8>(cov.channel);
        ds << static_cast<quint64>(cov.ribbons.size());
        for (const auto& ribbon : cov.ribbons) writePoints(ds, ribbon);
    }


    ds << static_cast<quint64>(data.beam_rays.size());
    for (const auto& ray : data.beam_rays) {
        ds << static_cast<quint8>(ray.channel)
           << static_cast<qint64>(ray.timestamp_us)
           << static_cast<quint32>(ray.ping_number)
           << ray.origin << ray.outer;
    }

    // Intensity grid (the raster itself).
    ds << static_cast<qint32>(data.intensity_w)
       << static_cast<qint32>(data.intensity_h)
       << data.intensity_disp_low << data.intensity_disp_high;
    ds << static_cast<quint64>(data.intensity_cache.size());
    if (!data.intensity_cache.empty())
        ds.writeRawData(reinterpret_cast<const char*>(data.intensity_cache.data()),
                        static_cast<int>(data.intensity_cache.size() * sizeof(uint16_t)));

    writeNavStats(ds, data.nav_stats);

    if (ds.status() != QDataStream::Ok) return false;
    return f.commit();
}

bool load(const std::string& path,
          const Meta&        expect,
          LayerMapData&      out,
          Summary&           summary) {
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly)) return false;
    if (f.size() < 0 || static_cast<quint64>(f.size()) > kMaxCacheFileBytes)
        return false;

    try {
        QDataStream ds(&f);
        ds.setVersion(QDataStream::Qt_6_0);

        if (!readCurrentHeader(ds)) return false;

        Meta meta;
        if (!readMeta(ds, meta)) return false;
        // Stale if the source store, nav params, or quality tier changed.
        if (meta.src_size  != expect.src_size  ||
            meta.src_mtime != expect.src_mtime ||
            meta.nav_hash  != expect.nav_hash  ||
            meta.quality   != expect.quality)
            return false;

        Summary decoded_summary;
        ds >> decoded_summary.has_sample_nav
           >> decoded_summary.sample_lat
           >> decoded_summary.sample_lon
           >> decoded_summary.sample_alt
           >> decoded_summary.sample_is_proj
           >> decoded_summary.track_m;
        quint64 value = 0;
        ds >> value; decoded_summary.total_ssc_entries = value;
        ds >> value; decoded_summary.preview_port_count = value;
        ds >> decoded_summary.quality_reduced;
        if (ds.status() != QDataStream::Ok) return false;

        LayerMapData decoded;
        quint8 kind = 0;
        ds >> kind;
        if (ds.status() != QDataStream::Ok || !isKnownLayerKind(kind)) return false;
        decoded.kind = static_cast<LayerMapKind>(kind);
        ds >> decoded.lon_min >> decoded.lon_max
           >> decoded.lat_min >> decoded.lat_max
           >> decoded.is_projected >> decoded.preview_reduced;
        if (ds.status() != QDataStream::Ok) return false;

        ReadBudget budget;
        if (!readPoints(ds, decoded.nav_track, budget)) return false;

        quint64 coverage_count = 0;
        ds >> coverage_count;
        quint64 minimum_coverage_bytes = 0;
        if (ds.status() != QDataStream::Ok ||
            coverage_count > kMaxCoverageCollections ||
            !fitsSizeT(coverage_count) ||
            !checkedMultiply(coverage_count, 1ULL + sizeof(quint64),
                             minimum_coverage_bytes) ||
            !hasRemainingBytes(ds, minimum_coverage_bytes))
            return false;
        decoded.coverage.reserve(static_cast<size_t>(coverage_count));

        for (quint64 c = 0; c < coverage_count; ++c) {
            SwathCoverage coverage;
            quint8 channel = 0;
            ds >> channel;
            if (ds.status() != QDataStream::Ok || !isKnownChannel(channel))
                return false;
            coverage.channel = static_cast<core::SidescanChannel>(channel);

            quint64 ribbon_count = 0;
            ds >> ribbon_count;
            quint64 minimum_ribbon_bytes = 0;
            if (ds.status() != QDataStream::Ok ||
                ribbon_count > kMaxRibbonsPerCoverage ||
                ribbon_count > budget.ribbons_remaining ||
                !fitsSizeT(ribbon_count) ||
                !checkedMultiply(ribbon_count, sizeof(quint64),
                                 minimum_ribbon_bytes) ||
                !hasRemainingBytes(ds, minimum_ribbon_bytes))
                return false;
            budget.ribbons_remaining -= ribbon_count;
            coverage.ribbons.reserve(static_cast<size_t>(ribbon_count));
            for (quint64 r = 0; r < ribbon_count; ++r) {
                std::vector<QPointF> ribbon;
                if (!readPoints(ds, ribbon, budget)) return false;
                coverage.ribbons.push_back(std::move(ribbon));
            }
            decoded.coverage.push_back(std::move(coverage));
        }

        quint64 hidden_coverage_count = 0;
        ds >> hidden_coverage_count;
        if (ds.status() != QDataStream::Ok ||
            hidden_coverage_count > kMaxCoverageCollections ||
            !fitsSizeT(hidden_coverage_count))
            return false;
        decoded.coverage_nadir_hidden.reserve(
            static_cast<size_t>(hidden_coverage_count));
        for (quint64 c = 0; c < hidden_coverage_count; ++c) {
            SwathCoverage coverage;
            quint8 channel = 0;
            ds >> channel;
            if (ds.status() != QDataStream::Ok || !isKnownChannel(channel))
                return false;
            coverage.channel = static_cast<core::SidescanChannel>(channel);
            quint64 ribbon_count = 0;
            ds >> ribbon_count;
            if (ds.status() != QDataStream::Ok ||
                ribbon_count > kMaxRibbonsPerCoverage ||
                ribbon_count > budget.ribbons_remaining ||
                !fitsSizeT(ribbon_count))
                return false;
            budget.ribbons_remaining -= ribbon_count;
            coverage.ribbons.reserve(static_cast<size_t>(ribbon_count));
            for (quint64 r = 0; r < ribbon_count; ++r) {
                std::vector<QPointF> ribbon;
                if (!readPoints(ds, ribbon, budget)) return false;
                coverage.ribbons.push_back(std::move(ribbon));
            }
            decoded.coverage_nadir_hidden.push_back(std::move(coverage));
        }

        quint64 beam_count = 0;
        ds >> beam_count;
        constexpr quint64 kSerializedBeamBytes =
            sizeof(quint8) + sizeof(qint64) + sizeof(quint32)
            + 2ULL * kSerializedPointBytes;
        quint64 minimum_beam_bytes = 0;
        if (ds.status() != QDataStream::Ok || beam_count > kMaxBeamRays ||
            !fitsSizeT(beam_count) ||
            !checkedMultiply(beam_count, kSerializedBeamBytes,
                             minimum_beam_bytes) ||
            !hasRemainingBytes(ds, minimum_beam_bytes))
            return false;
        decoded.beam_rays.reserve(static_cast<size_t>(beam_count));
        for (quint64 i = 0; i < beam_count; ++i) {
            quint8 channel = 0;
            qint64 timestamp = 0;
            quint32 ping_number = 0;
            QPointF origin;
            QPointF outer;
            ds >> channel >> timestamp >> ping_number >> origin >> outer;
            if (ds.status() != QDataStream::Ok || !isKnownChannel(channel) ||
                !std::isfinite(origin.x()) || !std::isfinite(origin.y()) ||
                !std::isfinite(outer.x()) || !std::isfinite(outer.y()))
                return false;
            decoded.beam_rays.push_back({
                static_cast<core::SidescanChannel>(channel),
                static_cast<int64_t>(timestamp),
                static_cast<uint32_t>(ping_number), origin, outer});
        }

        qint32 iw = 0, ih = 0;
        ds >> iw >> ih
           >> decoded.intensity_disp_low >> decoded.intensity_disp_high;
        quint64 pixel_count = 0;
        ds >> pixel_count;
        quint64 raw_bytes = 0;
        if (ds.status() != QDataStream::Ok ||
            !validateRasterLayout(iw, ih, pixel_count, &raw_bytes) ||
            !hasRemainingBytes(ds, raw_bytes))
            return false;
        decoded.intensity_w = iw;
        decoded.intensity_h = ih;
        if (pixel_count > 0) {
            decoded.intensity_cache.resize(static_cast<size_t>(pixel_count));
            const int bytes_to_read = static_cast<int>(raw_bytes);
            if (ds.readRawData(
                    reinterpret_cast<char*>(decoded.intensity_cache.data()),
                    bytes_to_read) != bytes_to_read)
                return false;
        }

        if (!readNavStats(ds, decoded.nav_stats) ||
            ds.status() != QDataStream::Ok || !f.atEnd() ||
            !summaryMatchesData(decoded_summary, decoded))
            return false;

        completeSummaryFromData(decoded_summary, decoded);
        out = std::move(decoded);
        summary = std::move(decoded_summary);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace dolphin::ui::rastercache
