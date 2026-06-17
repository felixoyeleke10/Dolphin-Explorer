// SidescanRasterCache.cpp — binary (de)serialization of a built map raster.
#include "ui/features/map/sidescan/SidescanRasterCache.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>
#include <QtGlobal>

namespace dolphin::ui::rastercache {

namespace {

constexpr quint32 kMagic   = 0x44525331; // "DRS1"
constexpr quint32 kVersion = 1;

constexpr unsigned long long kFnvOffset = 1469598103934665603ULL;
constexpr unsigned long long kFnvPrime  = 1099511628211ULL;

template <typename T>
unsigned long long fnvMix(unsigned long long h, const T& v) {
    const auto* p = reinterpret_cast<const unsigned char*>(&v);
    for (size_t i = 0; i < sizeof(T); ++i) { h ^= p[i]; h *= kFnvPrime; }
    return h;
}

// -- QPointF vector helpers ----------------------------------------------------
void writePoints(QDataStream& ds, const std::vector<QPointF>& pts) {
    ds << static_cast<quint64>(pts.size());
    for (const auto& p : pts) ds << p;
}
void readPoints(QDataStream& ds, std::vector<QPointF>& pts) {
    quint64 n = 0; ds >> n;
    pts.clear();
    pts.reserve(static_cast<size_t>(n));
    for (quint64 i = 0; i < n; ++i) { QPointF p; ds >> p; pts.push_back(p); }
}

// -- NavStats (persist the diagnostics-relevant subset) ------------------------
void writeNavStats(QDataStream& ds, const NavStats& s) {
    ds << static_cast<quint64>(s.total_pings)
       << static_cast<quint64>(s.invalid_nav)
       << static_cast<quint64>(s.repeated_fixes)
       << static_cast<quint64>(s.nav_spikes)
       << static_cast<quint64>(s.time_gaps)
       << s.avg_spacing_m << s.max_spacing_m
       << static_cast<quint64>(s.strips_built)
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
void readNavStats(QDataStream& ds, NavStats& s) {
    quint64 u; qint32 i; QString str;
    ds >> u; s.total_pings = u;
    ds >> u; s.invalid_nav = u;
    ds >> u; s.repeated_fixes = u;
    ds >> u; s.nav_spikes = u;
    ds >> u; s.time_gaps = u;
    ds >> s.avg_spacing_m >> s.max_spacing_m;
    ds >> u; s.strips_built = u;
    ds >> u; s.preview_pixels_written = u;
    ds >> u; s.preview_pixels_filled = u;
    ds >> u; s.coverage_ribbons_built = u;
    ds >> s.nav_lon_min >> s.nav_lon_max >> s.nav_lat_min >> s.nav_lat_max;
    ds >> s.strip_lon_min >> s.strip_lon_max >> s.strip_lat_min >> s.strip_lat_max;
    ds >> i; s.image_width = i;
    ds >> i; s.image_height = i;
    ds >> i; s.quality_used = static_cast<MapSonarQuality>(i);
    ds >> u; s.pings_available = u;
    ds >> s.memory_reduced;
    ds >> str; s.crs_label = str.toStdString();
    ds >> str; s.unsupported_crs_id = str.toStdString();
}

void writeMeta(QDataStream& ds, const Meta& m) {
    ds << static_cast<quint64>(m.src_size)
       << static_cast<qint64>(m.src_mtime)
       << static_cast<quint64>(m.nav_hash)
       << static_cast<qint32>(m.quality);
}
void readMeta(QDataStream& ds, Meta& m) {
    quint64 sz, nh; qint64 mt; qint32 q;
    ds >> sz >> mt >> nh >> q;
    m.src_size = sz; m.src_mtime = mt; m.nav_hash = nh; m.quality = q;
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
              bool                       slant_range_corrected,
              MapSonarQuality            quality,
              const std::string&         display_crs_id) {
    Meta m;
    const QFileInfo fi(QString::fromStdString(store_path));
    if (fi.exists()) {
        m.src_size  = static_cast<unsigned long long>(fi.size());
        m.src_mtime = fi.lastModified().toMSecsSinceEpoch();
    }
    unsigned long long h = kFnvOffset;
    h = fnvMix(h, nav.smooth_enabled);
    h = fnvMix(h, nav.smooth_window);
    h = fnvMix(h, nav.layback_enabled);
    h = fnvMix(h, nav.layback_m);
    h = fnvMix(h, nav.heading_offset_deg);
    h = fnvMix(h, nav.pitch_offset_deg);
    h = fnvMix(h, nav.roll_offset_deg);
    h = fnvMix(h, slant_range_corrected);
    for (char c : display_crs_id) h = fnvMix(h, c);
    m.nav_hash = h;
    m.quality  = static_cast<int>(quality);
    return m;
}

bool save(const std::string&  path,
          const Meta&         meta,
          const Summary&      summary,
          const LayerMapData& data) {
    QSaveFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::WriteOnly)) return false;

    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << kMagic << kVersion;
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
       << data.is_projected << data.show_nav_track << data.preview_reduced;

    writePoints(ds, data.nav_track);

    ds << static_cast<quint64>(data.coverage.size());
    for (const auto& cov : data.coverage) {
        ds << static_cast<quint8>(cov.channel);
        ds << static_cast<quint64>(cov.ribbons.size());
        for (const auto& ribbon : cov.ribbons) writePoints(ds, ribbon);
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

    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0, version = 0;
    ds >> magic >> version;
    if (magic != kMagic || version != kVersion) return false;

    Meta meta;
    readMeta(ds, meta);
    // Stale if the source store, nav params, or quality tier changed.
    if (meta.src_size  != expect.src_size  ||
        meta.src_mtime != expect.src_mtime ||
        meta.nav_hash  != expect.nav_hash  ||
        meta.quality   != expect.quality)
        return false;

    LayerMapData d;

    ds >> summary.has_sample_nav >> summary.sample_lat >> summary.sample_lon
       >> summary.sample_alt >> summary.sample_is_proj >> summary.track_m;
    quint64 u = 0;
    ds >> u; summary.total_ssc_entries = u;
    ds >> u; summary.preview_port_count = u;
    ds >> summary.quality_reduced;

    quint8 kind = 0;
    ds >> kind; d.kind = static_cast<LayerMapKind>(kind);
    ds >> d.lon_min >> d.lon_max >> d.lat_min >> d.lat_max
       >> d.is_projected >> d.show_nav_track >> d.preview_reduced;

    readPoints(ds, d.nav_track);

    quint64 cov_n = 0; ds >> cov_n;
    d.coverage.clear();
    d.coverage.reserve(static_cast<size_t>(cov_n));
    for (quint64 c = 0; c < cov_n; ++c) {
        SwathCoverage cov;
        quint8 ch = 0; ds >> ch;
        cov.channel = static_cast<core::SidescanChannel>(ch);
        quint64 rib_n = 0; ds >> rib_n;
        cov.ribbons.reserve(static_cast<size_t>(rib_n));
        for (quint64 r = 0; r < rib_n; ++r) {
            std::vector<QPointF> ribbon;
            readPoints(ds, ribbon);
            cov.ribbons.push_back(std::move(ribbon));
        }
        d.coverage.push_back(std::move(cov));
    }

    qint32 iw = 0, ih = 0;
    ds >> iw >> ih >> d.intensity_disp_low >> d.intensity_disp_high;
    d.intensity_w = iw; d.intensity_h = ih;
    quint64 px_n = 0; ds >> px_n;
    if (px_n > 0) {
        d.intensity_cache.resize(static_cast<size_t>(px_n));
        const int want = static_cast<int>(px_n * sizeof(uint16_t));
        if (ds.readRawData(reinterpret_cast<char*>(d.intensity_cache.data()), want) != want)
            return false;
    }

    readNavStats(ds, d.nav_stats);

    if (ds.status() != QDataStream::Ok) return false;

    out = std::move(d);
    return true;
}

} // namespace dolphin::ui::rastercache
