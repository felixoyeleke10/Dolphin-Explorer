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

// ── Lifecycle ─────────────────────────────────────────────────────────────────

Project::Project(QObject* parent) : QObject(parent) {}

void Project::setCrs(const std::string& epsg)
{
    const core::SpatialRef ref = geo::spatialRefFromId(epsg);
    m_display_spatial_ref = ref.empty() ? core::makeWgs84SpatialRef() : ref;
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

std::shared_ptr<Project> Project::open(const std::string& manifest_path)
{
    std::ifstream f(manifest_path);
    if (!f.is_open()) return nullptr;
    std::ostringstream ss;
    ss << f.rdbuf();
    auto p = std::make_shared<Project>();
    p->m_manifest_path = manifest_path;
    if (!p->fromJson(ss.str())) return nullptr;
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
    purgeOrphanedCaches();
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

// ── ID generation ─────────────────────────────────────────────────────────────

std::string Project::generateId(const std::string& prefix) const
{
    static std::atomic<uint64_t> s_counter{0};
    const auto now = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const uint64_t seq = s_counter.fetch_add(1, std::memory_order_relaxed);
    return prefix + "_" + std::to_string(now) + "_" + std::to_string(seq);
}

} // namespace dolphin::app
