// Project.Serialization.Read.cpp — Project::fromJson()
//
// Project::toJson() and shared JSON/path codec helpers live in
// Project.Serialization.cpp.

#include "app/project/Project.h"
#include "app/project/Project_p.h"
#include "geo/GeoUtils.h"
#include "io/cache/ParsedCache.h"
#include "util/Json.h"

#include <algorithm>

namespace dolphin::app {

namespace {

using detail::normalisePath;
using detail::normaliseFormat;
using detail::manifestDirectory;

// ── Decode helpers ────────────────────────────────────────────────────────────

static Modality modalityFromString(const std::string& s) {
    if (s == "sidescan")     return Modality::Sidescan;
    if (s == "subbottom")    return Modality::SubBottom;
    if (s == "magnetometer") return Modality::Magnetometer;
    if (s == "multibeam")    return Modality::Multibeam;
    if (s == "raster")       return Modality::Raster;
    if (s == "mixed")        return Modality::Mixed;
    return Modality::Unknown;
}

static core::Confidence confidenceFromString(const std::string& s) {
    if (s == "probable") return core::Confidence::Probable;
    if (s == "certain")  return core::Confidence::Certain;
    return core::Confidence::Possible;
}

static core::SpatialRef spatialRefFromJson(const util::JsonValue& node,
                                           const core::SpatialRef& fallback = {})
{
    if (!node.isObject())
        return fallback;

    core::SpatialRef ref;
    ref.id = node.get("id").asString();
    ref.kind = geo::spatialRefKindFromString(node.get("kind").asString());
    ref.exact = node.get("exact").asBool();

    if (ref.id.empty() && ref.kind == core::SpatialRefKind::Unknown)
        return fallback;
    if (ref.kind == core::SpatialRefKind::Unknown && !ref.id.empty())
        ref = geo::spatialRefFromId(ref.id);
    return ref;
}

// ── Path helpers ──────────────────────────────────────────────────────────────

static std::string resolveStoredPath(const std::string& path, const std::string& manifest_path)
{
    if (path.empty())
        return {};

    const QString qpath = QString::fromStdString(path);
    if (QDir::isAbsolutePath(qpath))
        return normalisePath(qpath).toStdString();

    const QString base_dir = manifestDirectory(manifest_path);
    if (base_dir.isEmpty())
        return QDir::cleanPath(qpath).toStdString();

    return normalisePath(QDir(base_dir).absoluteFilePath(qpath)).toStdString();
}

// ── Validation helpers ────────────────────────────────────────────────────────

static bool sourceFingerprintMatchesFile(const ProjectSource& src)
{
    if (src.path.empty())
        return false;

    QFileInfo info(QString::fromStdString(src.path));
    if (!info.exists())
        return false;

    const uint64_t current_size = static_cast<uint64_t>(info.size());
    if (src.size_bytes != current_size)
        return false;

    const int64_t current_modified = info.lastModified().toMSecsSinceEpoch();
    if (src.modified_utc_ms != 0 && src.modified_utc_ms != current_modified)
        return false;

    return true;
}

static const ProjectSource* findSourceById(const std::vector<ProjectSource>& sources,
                                           const std::string& source_id)
{
    auto it = std::find_if(sources.begin(), sources.end(),
        [&](const ProjectSource& src) { return src.id == source_id; });
    return (it != sources.end()) ? &(*it) : nullptr;
}

} // namespace

// ── fromJson ──────────────────────────────────────────────────────────────────

bool Project::fromJson(const std::string& json)
{
    util::JsonValue root = util::parseJson(json);
    if (!root.isObject()) return false;

    const int version = std::max(1, root.get("version").asInt());
    m_name = root.get("name").asString();
    m_display_spatial_ref = spatialRefFromJson(
        root.get("display_spatial_ref"),
        geo::spatialRefFromId(root.get("crs").asString()));
    if (m_name.empty()) return false;
    if (m_display_spatial_ref.empty())
        m_display_spatial_ref = core::makeWgs84SpatialRef();
    processing_graph = pipeline::NodeGraph{};
    if (root.has("project_graph"))
        processing_graph.fromJson(root.get("project_graph").dump());

    // Sources
    m_sources.clear();
    for (auto& js : root.get("sources").elements()) {
        ProjectSource s;
        s.id         = js.get("id").asString();
        s.format     = js.get("format").asString();
        s.path       = resolveStoredPath(js.get("path").asString(), m_manifest_path);
        s.source_spatial_ref = spatialRefFromJson(js.get("source_spatial_ref"));
        s.sha256     = js.get("sha256").asString();
        s.size_bytes = static_cast<uint64_t>(js.get("size").asDouble());
        s.modified_utc_ms =
            static_cast<int64_t>(js.get("modified_utc_ms").asDouble());
        m_sources.push_back(std::move(s));
    }

    // Layers — support both "layers" (v2 new) and "lines" (v2 old) keys
    m_layers.clear();
    auto& layers_node = root.has("layers") ? root.get("layers") : root.get("lines");
    for (auto& jl : layers_node.elements()) {
        auto layer = std::make_unique<DataLayer>(this);
        layer->id        = jl.get("id").asString();
        layer->label     = jl.get("label").asString();
        layer->source_id = jl.get("source_id").asString();
        layer->artifact_store_path =
            resolveStoredPath(jl.get("artifact_store_path").asString(), m_manifest_path);
        layer->artifact_store_format = jl.get("artifact_store_format").asString();
        layer->modality  = modalityFromString(jl.get("modality").asString());
        layer->source_spatial_ref = spatialRefFromJson(jl.get("source_spatial_ref"));
        layer->uses_project_graph = jl.has("uses_project_graph")
            ? jl.get("uses_project_graph").asBool()
            : false;
        if (layer->source_spatial_ref.empty()) {
            if (const auto* source = findSourceById(m_sources, layer->source_id))
                layer->source_spatial_ref = source->source_spatial_ref;
        }
        layer->index_built = jl.get("index_built").asBool();
        layer->visible     = !jl.get("visible").isNull() ? jl.get("visible").asBool() : true;
        layer->slant_range_corrected = jl.get("slant_range_corrected").asBool();
        layer->bottom_track_kind = static_cast<BottomTrackKind>(
            jl.get("bottom_track_kind").asInt());
        layer->sonar_name  = jl.get("sonar_name").asString();
        layer->survey_name = jl.get("survey_name").asString();
        layer->vessel_name = jl.get("vessel_name").asString();
        layer->start_time_utc = jl.get("start_time_utc").asDouble();
        layer->end_time_utc = jl.get("end_time_utc").asDouble();
        layer->frequency_hz     = jl.get("frequency_hz").asFloat();
        layer->low_frequency_hz = jl.get("low_frequency_hz").asFloat();

        auto& jindex = jl.get("artifact_index");
        if (jindex.isObject()) {
            layer->artifact_index.source_id = jindex.get("source_id").asString();
            for (auto& je : jindex.get("entries").elements()) {
                core::ArtifactIndexEntry entry;
                entry.artifact_id  = static_cast<uint64_t>(je.get("artifact_id").asDouble());
                entry.type         = static_cast<core::ArtifactType>(je.get("type").asInt());
                entry.timestamp_us = static_cast<int64_t>(je.get("timestamp_us").asDouble());
                entry.file_offset  = static_cast<uint64_t>(je.get("file_offset").asDouble());
                entry.subrecord_offset =
                    static_cast<uint32_t>(je.get("subrecord_offset").asDouble());
                entry.byte_length  = static_cast<uint32_t>(je.get("byte_length").asDouble());
                entry.lat          = je.get("lat").asDouble();
                entry.lon          = je.get("lon").asDouble();
                entry.spatial_ref_kind = geo::spatialRefKindFromString(
                    je.get("spatial_ref_kind").asString());
                entry.is_projected = je.get("is_projected").asBool();
                layer->artifact_index.entries.push_back(std::move(entry));
            }
        }

        // If entries were not saved (omitted when a .dlpd cache exists), rebuild
        // the index from the binary cache file.  Only reads record headers —
        // no sample data — so this is fast even for large surveys.
        if (layer->artifact_index.entries.empty() && !layer->artifact_store_path.empty()) {
            const std::string sfmt = normaliseFormat(layer->artifact_store_format);
            if ((sfmt == "dlpd" || sfmt == "dpcache")
                && io::parsedCacheIsValid(layer->artifact_store_path)) {
                io::ParsedCacheReader cache_reader;
                if (cache_reader.open(layer->artifact_store_path)) {
                    auto rebuilt = cache_reader.buildIndex();
                    if (!rebuilt.empty()) {
                        rebuilt.source_id = layer->source_id;
                        layer->artifact_index = std::move(rebuilt);
                    }
                    // Recover metadata fields that may be missing from old JSON manifests.
                    const io::FormatMeta cached_meta = cache_reader.metadata();
                    if (layer->sonar_name.empty() && !cached_meta.sonar_name.empty())
                        layer->sonar_name = cached_meta.sonar_name;
                    if (layer->survey_name.empty() && !cached_meta.survey_name.empty())
                        layer->survey_name = cached_meta.survey_name;
                    if (layer->vessel_name.empty() && !cached_meta.vessel_name.empty())
                        layer->vessel_name = cached_meta.vessel_name;
                }
            }
        }

        if (layer->source_spatial_ref.empty() && !layer->artifact_index.empty()) {
            const auto& first = layer->artifact_index.entries.front();
            if (first.spatial_ref_kind != core::SpatialRefKind::Unknown) {
                layer->source_spatial_ref.kind = first.spatial_ref_kind;
                layer->source_spatial_ref.exact = false;
            } else {
                layer->source_spatial_ref = geo::spatialRefFromLegacy(first.is_projected);
            }
        }

        // Manifests before v5 do not persist the parsed artifact store. Force a
        // rebuild so the layer gets a native cache instead of continuing to hit
        // the raw source on every load.
        if (version < 5 && !layer->artifact_index.empty()) {
            layer->artifact_index.entries.clear();
            layer->index_built = false;
            layer->artifact_store_path.clear();
            layer->artifact_store_format.clear();
        }

        if (version >= 5 && (layer->index_built || !layer->artifact_index.empty())) {
            const std::string store_format = normaliseFormat(layer->artifact_store_format);
            const auto* source = findSourceById(m_sources, layer->source_id);
            const bool missing_store = (store_format != "dlpd" && store_format != "dpcache")
                || layer->artifact_store_path.empty()
                || !QFileInfo(QString::fromStdString(layer->artifact_store_path)).exists();
            const bool stale_source = !source || !sourceFingerprintMatchesFile(*source);
            // Also reject the cached index if the .dlpd file has an outdated
            // format version — this forces a transparent background rebuild rather
            // than displaying incorrect data from a pre-fix cache.
            const bool stale_cache = !missing_store
                && !io::parsedCacheIsValid(layer->artifact_store_path);
            if (missing_store || stale_source || stale_cache) {
                layer->artifact_index.entries.clear();
                layer->index_built = false;
                layer->artifact_store_path.clear();
                layer->artifact_store_format.clear();
            }
        }

        if (layer->artifact_index.source_id.empty())
            layer->artifact_index.source_id = layer->source_id;
        if (!layer->artifact_index.empty())
            layer->index_built = true;
        if (layer->modality == Modality::Unknown && !layer->artifact_index.empty())
            layer->modality = inferModality(layer->artifact_index);

        // Restore node graph — requires NodeRegistry to be populated.
        // On failure, fromJson clears the graph to a consistent empty state.
        if (jl.has("graph"))
            layer->node_graph.fromJson(jl.get("graph").dump());

        // Tags and group membership
        for (const auto& jt : jl.get("tags").elements())
            layer->tags.push_back(jt.asString());
        layer->group_id = jl.get("group_id").asString();

        layer->state = layer->index_built ? LayerState::Ready : LayerState::Placeholder;

        // ── Migration: split legacy Mixed-modality layers ─────────────────────
        // Older project files may carry Modality::Mixed when a source contained
        // multiple artifact families.  Apply the same module routing that new
        // imports use: the first non-empty family in kModuleArtifactTypes order
        // retains the original DataLayer; additional families get new sibling
        // layers sharing the same cache.  Project re-saves on next user change.
        if (layer->modality == Modality::Mixed) {
            const std::string base = layer->label;
            bool first = true;
            std::vector<std::unique_ptr<DataLayer>> extras;
            for (auto fam_type : kModuleArtifactTypes) {
                auto filtered = filteredByType(layer->artifact_index, fam_type);
                if (filtered.empty()) continue;
                if (first) {
                    layer->label          = base + " " + moduleSuffix(fam_type);
                    layer->modality       = modalityForType(fam_type);
                    layer->artifact_index = std::move(filtered);
                    first = false;
                } else {
                    auto ex = std::make_unique<DataLayer>(this);
                    ex->id                    = generateId("layer");
                    ex->label                 = base + " " + moduleSuffix(fam_type);
                    ex->source_id             = layer->source_id;
                    ex->artifact_store_path   = layer->artifact_store_path;
                    ex->artifact_store_format = layer->artifact_store_format;
                    ex->modality              = modalityForType(fam_type);
                    ex->source_spatial_ref    = layer->source_spatial_ref;
                    ex->index_built           = layer->index_built;
                    ex->state                 = layer->state;
                    ex->visible               = layer->visible;
                    ex->sonar_name            = layer->sonar_name;
                    ex->survey_name           = layer->survey_name;
                    ex->vessel_name           = layer->vessel_name;
                    ex->start_time_utc        = layer->start_time_utc;
                    ex->end_time_utc          = layer->end_time_utc;
                    ex->frequency_hz          = layer->frequency_hz;
                    ex->artifact_index        = std::move(filtered);
                    extras.push_back(std::move(ex));
                }
            }
            m_layers.push_back(std::move(layer));
            for (auto& ex : extras)
                m_layers.push_back(std::move(ex));
        }
        else {
            m_layers.push_back(std::move(layer));
        }
    }

    for (auto& source : m_sources) {
        if (!source.source_spatial_ref.empty())
            continue;
        const auto it = std::find_if(m_layers.begin(), m_layers.end(),
            [&](const std::unique_ptr<DataLayer>& layer) {
                return layer && layer->source_id == source.id
                    && !layer->source_spatial_ref.empty();
            });
        if (it != m_layers.end() && *it)
            source.source_spatial_ref = (*it)->source_spatial_ref;
    }

    // Contacts
    m_contacts.clear();
    uint64_t max_id = 0;
    for (auto& jc : root.get("contacts").elements()) {
        core::Contact c;
        c.id             = static_cast<uint64_t>(jc.get("id").asDouble());
        c.label          = jc.get("label").asString();
        c.lat            = jc.get("lat").asDouble();
        c.lon            = jc.get("lon").asDouble();
        c.spatial_ref    = spatialRefFromJson(jc.get("spatial_ref"));
        if (c.spatial_ref.empty())
            c.spatial_ref = m_display_spatial_ref;
        c.depth_m        = jc.get("depth_m").asFloat();
        c.range_m        = jc.get("range_m").asFloat();        // new key
        if (c.range_m == 0.0f)
            c.range_m    = jc.get("length_m").asFloat();       // legacy key
        c.width_m        = jc.get("width_m").asFloat();
        c.height_m       = jc.get("height_m").asFloat();
        c.artifact_id    = static_cast<uint64_t>(jc.get("artifact_id").asDouble());
        c.sample_idx     = static_cast<uint32_t>(jc.get("sample_idx").asDouble());
        c.line_id        = jc.get("line_id").asString();
        c.classification = jc.get("classification").asString();
        c.confidence     = confidenceFromString(jc.get("confidence").asString());
        c.notes          = jc.get("notes").asString();
        c.created_at     = jc.get("created_at").asDouble();
        c.modified_at    = jc.get("modified_at").asDouble();
        for (const auto& jt : jc.get("tags").elements())
            c.tags.push_back(jt.asString());
        c.group_id = jc.get("group_id").asString();
        if (c.id > max_id) max_id = c.id;
        m_contacts.push_back(std::move(c));
    }
    m_next_contact_id = max_id + 1;

    // Layer groups
    m_layer_groups.clear();
    for (const auto& jg : root.get("layer_groups").elements()) {
        ItemGroup g;
        g.id       = jg.get("id").asString();
        g.name     = jg.get("name").asString();
        g.expanded = jg.has("expanded") ? jg.get("expanded").asBool() : true;
        if (!g.id.empty() && !g.name.empty())
            m_layer_groups.push_back(std::move(g));
    }

    // Contact groups
    m_contact_groups.clear();
    for (const auto& jg : root.get("contact_groups").elements()) {
        ItemGroup g;
        g.id       = jg.get("id").asString();
        g.name     = jg.get("name").asString();
        g.expanded = jg.has("expanded") ? jg.get("expanded").asBool() : true;
        if (!g.id.empty() && !g.name.empty())
            m_contact_groups.push_back(std::move(g));
    }

    return true;
}

} // namespace dolphin::app
