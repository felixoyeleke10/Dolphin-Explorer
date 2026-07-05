// Project.cpp — lifecycle: create, open, save, saveAs, CRS, ID generation.
//
// CRUD methods split into compilation units:
//   Project.Sources.cpp      addSource / findSource / findSourceByPath
//   Project.Layers.cpp       addLayer / findLayer / removeLayer / reorderLayers
//   Project.Contacts.cpp     addContact / updateContact / removeContact
//   Project.Workers.cpp      addWorker / findWorker / removeWorker
//   Project.Serialization.cpp  toJson / fromJson

#include "app/project/Project.h"
#include "app/project/Project_p.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"

#include <QFileInfo>
#include <QSaveFile>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace dolphin::app {

namespace {

bool copyProjectOwnedCaches(const std::string& old_manifest,
                             const std::string& new_manifest,
                             std::vector<std::unique_ptr<DataLayer>>& layers)
{
    namespace fs = std::filesystem;
    const QString old_root = detail::cacheRootForManifest(old_manifest);
    const QString new_root = detail::cacheRootForManifest(new_manifest);
    if (old_root.isEmpty() || new_root.isEmpty()) return true;

    const fs::path new_root_path(new_root.toStdString());
    std::error_code ec;
    fs::create_directories(new_root_path, ec);
    if (ec) return false;

    std::map<std::string, std::string> remapped;
    for (auto& layer : layers) {
        if (!layer) continue;
        const std::string sfmt = detail::normaliseFormat(layer->artifact_store_format);
        if ((sfmt != "dlpd" && sfmt != "dpcache") || layer->artifact_store_path.empty())
            continue;

        const QString cur = detail::normalisePath(layer->artifact_store_path);
        if (!detail::pathHasPrefix(cur, old_root)) continue;

        const fs::path old_p(cur.toStdString());
        if (!fs::exists(old_p)) continue;

        auto remap_it = remapped.find(cur.toStdString());
        if (remap_it != remapped.end()) {
            layer->artifact_store_path = remap_it->second;
            continue;
        }

        const fs::path new_p = new_root_path / old_p.filename();
        if (old_p != new_p) {
            fs::create_directories(new_p.parent_path(), ec);
            if (ec) return false;
            fs::copy_file(old_p, new_p, fs::copy_options::overwrite_existing, ec);
            if (ec) return false;
        }
        const std::string relocated = detail::normalisePath(new_p.string()).toStdString();
        remapped.emplace(cur.toStdString(), relocated);
        layer->artifact_store_path = relocated;
    }
    return true;
}

} // namespace

// -- Lifecycle -----------------------------------------------------------------

Project::Project(QObject* parent) : QObject(parent) {}

void Project::setName(const std::string& name)
{
    if (name.empty() || name == m_name) return;
    m_name = name;
    emit modified();
}

void Project::setDrapingSurface(const std::string& path)
{
    if (path == m_draping_surface) return;
    m_draping_surface = path;
    emit modified();
}

void Project::setCrs(const std::string& epsg)
{
    const core::SpatialRef ref = geo::spatialRefFromId(epsg);
    m_display_spatial_ref = ref.empty() ? core::makeWgs84SpatialRef() : ref;
}

core::SpatialRef Project::workingCrs() const
{
    // The survey/working grid = the projected CRS the source data is in.  Pick
    // the most common projected source CRS across the project's sources; if no
    // source is projected (pure geographic data), fall back to the display ref.
    std::map<std::string, int> counts;
    core::SpatialRef best;
    int best_n = 0;
    for (const auto& src : m_sources) {
        const core::SpatialRef& sr = src.source_spatial_ref;
        if (sr.empty() || !core::spatialRefIsProjected(sr)) continue;
        const int n = ++counts[sr.id];
        if (n > best_n) { best_n = n; best = sr; }
    }
    return best.empty() ? m_display_spatial_ref : best;
}

bool Project::hasMixedProjectedSources() const
{
    std::string first;
    for (const auto& src : m_sources) {
        const core::SpatialRef& sr = src.source_spatial_ref;
        if (sr.empty() || !core::spatialRefIsProjected(sr)) continue;
        if (first.empty())       first = sr.id;
        else if (sr.id != first) return true;
    }
    return false;
}

std::shared_ptr<Project> Project::create(const std::string& name,
                                          const std::string& manifest_path)
{
    namespace fs = std::filesystem;
    auto p = std::make_shared<Project>();
    p->m_name          = name;
    p->m_manifest_path = manifest_path;

    const QString data_dir = detail::cacheRootForManifest(manifest_path);
    if (!data_dir.isEmpty()) {
        std::error_code ec;
        fs::create_directories(fs::path(data_dir.toStdString()), ec);
    }
    return p;
}

std::string Project::dataPath() const
{
    return detail::cacheRootForManifest(m_manifest_path).toStdString();
}

std::shared_ptr<Project> Project::open(const std::string& manifest_path,
                                       std::string* error)
{
    std::ifstream f(manifest_path);
    if (!f.is_open()) return nullptr;
    std::ostringstream ss;
    ss << f.rdbuf();
    auto p = std::make_shared<Project>();
    p->m_manifest_path = manifest_path;
    if (!p->fromJson(ss.str())) {
        if (error) *error = p->m_load_error;
        return nullptr;
    }
    p->purgeOrphanedCaches();
    return p;
}

bool Project::save()
{
    namespace fs = std::filesystem;
    if (m_manifest_path.empty()) return false;

    const fs::path mp(m_manifest_path);
    if (!mp.parent_path().empty()) {
        std::error_code ec;
        fs::create_directories(mp.parent_path(), ec);
        if (ec) return false;
    }

    const QByteArray data = QByteArray::fromStdString(toJson());
    QSaveFile file(QString::fromStdString(m_manifest_path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (file.write(data) != data.size()) { file.cancelWriting(); return false; }
    if (!file.commit()) return false;
    // NO purgeOrphanedCaches() here — save() runs after EVERY import
    // completion, and during a multi-file import a sibling's freshly written
    // .dlpd is not referenced by any layer until ITS completion commits the
    // store path. Purging at save deleted those in-flight caches (multi-line
    // SBP imports lost all but one line). Orphans are cleaned at open()
    // instead, when no imports can be in flight.
    return true;
}

bool Project::saveAs(const std::string& new_path)
{
    if (new_path.empty()) return false;

    const std::string old_path = m_manifest_path;
    std::vector<std::string> old_stores;
    old_stores.reserve(m_layers.size());
    for (const auto& l : m_layers)
        old_stores.push_back(l ? l->artifact_store_path : std::string{});

    if (!copyProjectOwnedCaches(old_path, new_path, m_layers)) {
        for (size_t i = 0; i < m_layers.size() && i < old_stores.size(); ++i)
            if (m_layers[i]) m_layers[i]->artifact_store_path = old_stores[i];
        return false;
    }

    m_manifest_path = new_path;
    const std::string new_stem =
        QFileInfo(QString::fromStdString(new_path)).baseName().toStdString();
    const std::string old_name = m_name;
    if (!new_stem.empty()) m_name = new_stem;

    if (save()) return true;

    m_manifest_path = old_path;
    m_name          = old_name;
    for (size_t i = 0; i < m_layers.size() && i < old_stores.size(); ++i)
        if (m_layers[i]) m_layers[i]->artifact_store_path = old_stores[i];
    return false;
}

bool Project::renameOnDisk(const std::string& new_path)
{
    namespace fs = std::filesystem;
    if (new_path.empty() || m_manifest_path.empty()) return false;

    const std::string old_path = m_manifest_path;
    std::error_code ec;
    if (fs::path(old_path) == fs::path(new_path)) return true;   // nothing to do
    if (fs::exists(fs::path(new_path), ec)) return false;        // never clobber

    // 1. Move the manifest file.
    fs::rename(fs::path(old_path), fs::path(new_path), ec);
    if (ec) return false;

    // 2. Move the project's cache folder (instant within a filesystem) and remap
    //    any layer artifact paths that lived under it.
    const QString old_root = detail::cacheRootForManifest(old_path);
    const QString new_root = detail::cacheRootForManifest(new_path);
    bool cache_moved = false;
    if (!old_root.isEmpty() && !new_root.isEmpty() && old_root != new_root) {
        const fs::path orp(old_root.toStdString());
        const fs::path nrp(new_root.toStdString());
        if (fs::exists(orp, ec)) {
            if (fs::exists(nrp, ec)) {                            // target cache exists → abort
                fs::rename(fs::path(new_path), fs::path(old_path), ec);   // roll back manifest
                return false;
            }
            fs::create_directories(nrp.parent_path(), ec);
            fs::rename(orp, nrp, ec);
            if (ec) {
                fs::rename(fs::path(new_path), fs::path(old_path), ec);   // roll back manifest
                return false;
            }
            cache_moved = true;
        }
    }

    if (cache_moved) {
        for (auto& l : m_layers) {
            if (!l || l->artifact_store_path.empty()) continue;
            const QString cur = detail::normalisePath(l->artifact_store_path);
            if (!detail::pathHasPrefix(cur, old_root)) continue;
            QString relocated = cur;
            relocated.replace(0, old_root.size(), new_root);     // whole dir moved → prefix swap
            l->artifact_store_path = detail::normalisePath(relocated).toStdString();
        }
    }

    m_manifest_path = new_path;   // display name is the caller's; not derived here

    if (save()) return true;

    // Best-effort rollback if the save failed after the moves.
    m_manifest_path = old_path;
    if (cache_moved)
        fs::rename(fs::path(new_root.toStdString()), fs::path(old_root.toStdString()), ec);
    fs::rename(fs::path(new_path), fs::path(old_path), ec);
    return false;
}

// -- ID generation -------------------------------------------------------------

std::string Project::generateId(const std::string& prefix) const
{
    static std::atomic<uint64_t> s_counter{0};
    const auto now = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const uint64_t seq = s_counter.fetch_add(1, std::memory_order_relaxed);
    return prefix + "_" + std::to_string(now) + "_" + std::to_string(seq);
}

} // namespace dolphin::app
