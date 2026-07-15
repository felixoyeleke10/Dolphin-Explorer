// Project.Layers.cpp — DataLayer CRUD and reorder methods.
#include "app/project/Project.h"
#include "app/project/Project_p.h"
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace dolphin::app {

DataLayer* Project::addLayer(const std::string& source_id, const std::string& label)
{
    auto layer = std::make_unique<DataLayer>(this);
    layer->id        = generateId("layer");
    layer->label     = label;
    layer->source_id = source_id;
    layer->state     = LayerState::Placeholder;
    layer->uses_project_graph = true;
    layer->artifact_index.source_id = source_id;
    DataLayer* ptr = layer.get();
    m_layers.push_back(std::move(layer));
    emit layerPending(ptr);
    emit modified();
    return ptr;
}

void Project::commitLayer(const std::string& id)
{
    auto* layer = findLayer(id);
    if (!layer || layer->state == LayerState::Ready) return;
    layer->state = LayerState::Ready;
    emit layerReady(layer);
}

void Project::markLayerIndexing(const std::string& id)
{
    auto* layer = findLayer(id);
    if (!layer || layer->state != LayerState::Placeholder) return;
    layer->state = LayerState::Indexing;
}

void Project::markLayerFailed(const std::string& id)
{
    auto* layer = findLayer(id);
    if (!layer || layer->state == LayerState::Ready) return;
    layer->state = LayerState::Failed;
}

DataLayer* Project::findLayer(const std::string& id)
{
    for (auto& l : m_layers) if (l && l->id == id) return l.get();
    return nullptr;
}

std::vector<DataLayer*> Project::findLayersBySource(const std::string& source_id) const
{
    std::vector<DataLayer*> result;
    for (auto& l : m_layers)
        if (l && l->source_id == source_id) result.push_back(l.get());
    return result;
}

void Project::removeLayer(const std::string& id)
{
    auto it = std::find_if(m_layers.begin(), m_layers.end(),
        [&](const auto& l) { return l->id == id; });
    if (it == m_layers.end()) return;

    const std::string source_id    = (*it)->source_id;
    const std::string store_path   = (*it)->artifact_store_path;
    m_layers.erase(it);

    const bool source_still_used = std::any_of(m_layers.begin(), m_layers.end(),
        [&](const auto& l){ return l->source_id == source_id; });
    if (!source_still_used) {
        m_sources.erase(std::remove_if(m_sources.begin(), m_sources.end(),
            [&](const ProjectSource& s){ return s.id == source_id; }),
            m_sources.end());
    }

    const bool store_still_used = !store_path.empty() && std::any_of(
        m_layers.begin(), m_layers.end(),
        [&](const auto& l){ return l->artifact_store_path == store_path; });
    // Remove this layer's derived map-raster sidecars (".draster"); if the store
    // is no longer referenced, remove all of its raster sidecars too. Parsed
    // stores remain durable workflow assets until the project itself is deleted.
    // Raster sidecars are derived
    // artifacts (rebuilt on next load); naming is "<store_file>.<layerId>.q<tier>
    // .draster" — see ui/features/map/sidescan/SidescanRasterCache.
    if (!store_path.empty()) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path store_p(store_path);
        const std::string store_file = store_p.filename().string();
        const std::string mine_prefix  = store_file + "." + id + ".";
        const std::string store_prefix = store_file + ".";
        for (const auto& entry : fs::directory_iterator(store_p.parent_path(), ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".draster") continue;
            const std::string fn = entry.path().filename().string();
            const bool mine        = fn.rfind(mine_prefix, 0) == 0;
            const bool store_gone  = !store_still_used && fn.rfind(store_prefix, 0) == 0;
            if (mine || store_gone) {
                std::error_code rec;
                fs::remove(entry.path(), rec);
            }
        }
    }

    emit layerRemoved(id);
    emit modified();
}

void Project::reorderLayers(const std::vector<std::string>& ordered_layer_ids)
{
    if (ordered_layer_ids.empty() || m_layers.size() < 2) return;

    std::vector<std::string> before_ids;
    before_ids.reserve(m_layers.size());
    for (const auto& layer : m_layers)
        before_ids.push_back(layer ? layer->id : std::string{});

    std::unordered_map<std::string, size_t> order_index;
    order_index.reserve(ordered_layer_ids.size());
    for (size_t i = 0; i < ordered_layer_ids.size(); ++i)
        order_index.emplace(ordered_layer_ids[i], i);

    std::stable_sort(m_layers.begin(), m_layers.end(),
        [&](const auto& a, const auto& b) {
            const auto ia = a ? order_index.find(a->id) : order_index.end();
            const auto ib = b ? order_index.find(b->id) : order_index.end();
            const size_t ra = ia == order_index.end() ? ordered_layer_ids.size() : ia->second;
            const size_t rb = ib == order_index.end() ? ordered_layer_ids.size() : ib->second;
            return ra < rb;
        });

    bool changed = false;
    for (size_t i = 0; i < m_layers.size(); ++i) {
        const std::string after_id = m_layers[i] ? m_layers[i]->id : std::string{};
        if (i >= before_ids.size() || before_ids[i] != after_id) { changed = true; break; }
    }
    if (!changed) return;

    emit layersReordered();
    emit modified();
}

void Project::purgeOrphanedCaches()
{
    namespace fs = std::filesystem;

    const QString data_root = detail::cacheRootForManifest(m_manifest_path);
    if (data_root.isEmpty()) return;

    const fs::path data_dir(data_root.toStdString());
    std::error_code ec;
    if (!fs::exists(data_dir, ec)) return;

    // Build the set of cache files currently referenced by layers in this project.
    std::unordered_set<std::string> referenced;
    for (const auto& layer : m_layers) {
        if (!layer || layer->artifact_store_path.empty()) continue;
        const std::string fmt = detail::normaliseFormat(layer->artifact_store_format);
        if (fmt != "dlpd" && fmt != "dpcache") continue;
        const QString norm = detail::normalisePath(layer->artifact_store_path);
        if (detail::pathHasPrefix(norm, data_root))
            referenced.insert(norm.toStdString());
    }

    // Parsed .dlpd / .dpcache stores are durable workflow assets (D-04), even
    // after a layer stops referencing them. Only derived map-raster sidecars are
    // eligible for orphan cleanup.
    // Delete orphaned map-raster sidecars (".draster") whose store is no longer
    // referenced. A sidecar "<store>.<layerId>.q<tier>.draster" belongs to a store
    // iff that store's normalized path + "." is a prefix of the sidecar's path.
    for (const auto& entry : fs::directory_iterator(data_dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension().string() != ".draster") continue;
        const std::string norm =
            detail::normalisePath(entry.path().string()).toStdString();
        bool referenced_store = false;
        for (const auto& ref : referenced) {
            if (norm.rfind(ref + ".", 0) == 0) { referenced_store = true; break; }
        }
        if (!referenced_store) {
            fs::remove(entry.path(), ec);
            if (ec) ec.clear();
        }
    }
}

} // namespace dolphin::app
