// Project.Serialization.Read.cpp — Project::fromJson() orchestration and sources.

#include "app/project/Project.h"
#include "app/project/Project_p.h"
#include "geo/GeoUtils.h"
#include "util/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace dolphin::app {

namespace detail {

core::SpatialRef spatialRefFromJson(const util::JsonValue& node,
                                    const core::SpatialRef& fallback)
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

std::string resolveStoredPath(const std::string& path,
                              const std::string& manifest_path)
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

} // namespace detail

namespace {

using detail::resolveStoredPath;
using detail::spatialRefFromJson;

template <typename UInt>
UInt boundedUnsigned(double value)
{
    static_assert(std::is_unsigned_v<UInt>);
    if (!std::isfinite(value) || value <= 0.0)
        return 0;
    const double upper = static_cast<double>(std::numeric_limits<UInt>::max());
    if (value >= upper)
        return std::numeric_limits<UInt>::max();
    return static_cast<UInt>(value);
}

int64_t boundedNonNegativeInt64(double value)
{
    if (!std::isfinite(value) || value <= 0.0)
        return 0;
    const double upper = static_cast<double>(std::numeric_limits<int64_t>::max());
    if (value >= upper)
        return std::numeric_limits<int64_t>::max();
    return static_cast<int64_t>(value);
}

bool validateArrayWhenPresent(const util::JsonValue& root,
                              const char* field,
                              std::string& error)
{
    if (!root.has(field) || root.get(field).isArray())
        return true;
    error = "Project field '" + std::string(field) + "' must be an array.";
    return false;
}

} // namespace

bool Project::fromJson(const std::string& json)
{
    util::JsonValue root = util::parseJson(json);
    if (!root.isObject()) {
        m_load_error = "Invalid project JSON.";
        return false;
    }

    const int version = std::max(1, root.get("version").asInt());
    if (version > kSchemaVersion) {
        m_load_error = "This project was saved by a newer version of "
                       "Dolphin Explorer (manifest v" + std::to_string(version) +
                       "; this build reads up to v" +
                       std::to_string(kSchemaVersion) + ").";
        return false;
    }
    if (!root.has("name") || !root.get("name").isString()
            || root.get("name").asString().empty()) {
        m_load_error = "Project manifest is missing a non-empty name.";
        return false;
    }
    m_name = root.get("name").asString();

    if (!validateArrayWhenPresent(root, "sources", m_load_error)
            || (root.has("layers")
                ? !validateArrayWhenPresent(root, "layers", m_load_error)
                : !validateArrayWhenPresent(root, "lines", m_load_error))
            || !validateArrayWhenPresent(root, "contacts", m_load_error)
            || !validateArrayWhenPresent(root, "recycled_contacts", m_load_error)
            || !validateArrayWhenPresent(root, "features", m_load_error)
            || !validateArrayWhenPresent(root, "layer_groups", m_load_error)
            || !validateArrayWhenPresent(root, "contact_groups", m_load_error)) {
        return false;
    }

    m_display_spatial_ref = spatialRefFromJson(
        root.get("display_spatial_ref"),
        geo::spatialRefFromId(root.get("crs").asString()));
    if (m_display_spatial_ref.empty())
        m_display_spatial_ref = core::makeWgs84SpatialRef();
    processing_graph = pipeline::NodeGraph{};
    if (root.has("project_graph")
            && (!root.get("project_graph").isObject()
                || !processing_graph.fromJson(root.get("project_graph").dump()))) {
        m_load_error = "Project contains an unsupported or invalid project graph.";
        return false;
    }
    m_draping_surface = root.has("draping_surface")
        ? root.get("draping_surface").asString() : std::string{};

    m_sources.clear();
    std::unordered_set<std::string> source_ids;
    for (auto& js : root.get("sources").elements()) {
        if (!js.isObject()) {
            m_load_error = "Each project source must be an object.";
            return false;
        }
        ProjectSource source;
        source.id = js.get("id").asString();
        if (source.id.empty() || !source_ids.insert(source.id).second) {
            m_load_error = "Project contains an empty or duplicate source ID.";
            return false;
        }
        source.format = js.get("format").asString();
        source.path = resolveStoredPath(js.get("path").asString(), m_manifest_path);
        source.source_spatial_ref = spatialRefFromJson(
            js.get("source_spatial_ref"), {});
        source.sha256 = js.get("sha256").asString();
        source.size_bytes = boundedUnsigned<uint64_t>(js.get("size").asDouble());
        source.modified_utc_ms = boundedNonNegativeInt64(
            js.get("modified_utc_ms").asDouble());
        m_sources.push_back(std::move(source));
    }

    if (!restoreLayersFromJson(root, version))
        return false;
    return restoreEntitiesFromJson(root);
}

} // namespace dolphin::app
