// Project.Serialization.cpp — Project::toJson()
//
// Project::fromJson() lives in Project.Serialization.Read.cpp.
// All other Project methods live in Project.cpp.

#include "app/project/Project.h"
#include "app/project/Project_p.h"
#include "geo/GeoUtils.h"
#include "util/Json.h"

#include <cmath>

namespace dolphin::app {

namespace {

using detail::normalisePath;
using detail::normaliseFormat;
using detail::manifestDirectory;

// -- Encode helpers ------------------------------------------------------------

static std::string modalityToString(Modality m) {
    switch (m) {
    case Modality::Sidescan:     return "sidescan";
    case Modality::SubBottom:    return "subbottom";
    case Modality::Magnetometer: return "magnetometer";
    case Modality::Multibeam:    return "multibeam";
    case Modality::Raster:       return "raster";
    case Modality::Mixed:        return "mixed";
    default:                     return "unknown";
    }
}

static std::string confidenceToString(core::Confidence c) {
    switch (c) {
    case core::Confidence::Probable: return "probable";
    case core::Confidence::Certain:  return "certain";
    default:                         return "possible";
    }
}

static util::JsonValue spatialRefToJson(const core::SpatialRef& ref)
{
    util::JsonValue out = util::JsonValue::object();
    out["id"] = util::JsonValue(ref.id);
    out["kind"] = util::JsonValue(geo::spatialRefKindToString(ref.kind));
    out["exact"] = util::JsonValue(ref.exact);
    return out;
}

// -- Path helpers --------------------------------------------------------------

static std::string pathForManifest(const std::string& path, const std::string& manifest_path)
{
    if (path.empty())
        return {};

    const QString normalized = normalisePath(path);
    const QString base_dir = manifestDirectory(manifest_path);
    if (base_dir.isEmpty())
        return normalized.toStdString();

    const QString relative = QDir::cleanPath(QDir(base_dir).relativeFilePath(normalized));
    if (!relative.isEmpty()
        && !QDir::isAbsolutePath(relative)
        && !relative.startsWith("..")) {
        return relative.toStdString();
    }

    return normalized.toStdString();
}

} // namespace

// -- toJson --------------------------------------------------------------------

std::string Project::toJson() const
{
    util::JsonValue root = util::JsonValue::object();
    root["version"] = util::JsonValue(10);
    root["name"]    = util::JsonValue(m_name);
    root["crs"]     = util::JsonValue(m_display_spatial_ref.id);
    root["display_spatial_ref"] = spatialRefToJson(m_display_spatial_ref);
    root["project_graph"] = util::parseJson(processing_graph.toJson());

    // Sources
    util::JsonValue sources_arr = util::JsonValue::array();
    for (auto& s : m_sources) {
        util::JsonValue js = util::JsonValue::object();
        js["id"]     = util::JsonValue(s.id);
        js["format"] = util::JsonValue(s.format);
        js["path"]   = util::JsonValue(pathForManifest(s.path, m_manifest_path));
        js["source_spatial_ref"] = spatialRefToJson(s.source_spatial_ref);
        js["sha256"] = util::JsonValue(s.sha256);
        js["size"]   = util::JsonValue(static_cast<double>(s.size_bytes));
        js["modified_utc_ms"] = util::JsonValue(static_cast<double>(s.modified_utc_ms));
        sources_arr.push(std::move(js));
    }
    root["sources"] = std::move(sources_arr);

    // Layers — include the serialised node graph for each layer
    util::JsonValue layers_arr = util::JsonValue::array();
    for (auto& l : m_layers) {
        util::JsonValue jl = util::JsonValue::object();
        jl["id"]        = util::JsonValue(l->id);
        jl["label"]     = util::JsonValue(l->label);
        jl["source_id"] = util::JsonValue(l->source_id);
        jl["artifact_store_path"] = util::JsonValue(
            pathForManifest(l->artifact_store_path, m_manifest_path));
        jl["artifact_store_format"] = util::JsonValue(l->artifact_store_format);
        jl["modality"]  = util::JsonValue(modalityToString(l->modality));
        jl["source_spatial_ref"] = spatialRefToJson(l->source_spatial_ref);
        jl["uses_project_graph"] = util::JsonValue(l->uses_project_graph);
        jl["index_built"] = util::JsonValue(l->index_built);
        jl["visible"]     = util::JsonValue(l->visible);
        jl["slant_range_corrected"] = util::JsonValue(l->slant_range_corrected);
        jl["pipeline_applied"]      = util::JsonValue(l->pipeline_applied);
        jl["bottom_track_kind"] = util::JsonValue(static_cast<int>(l->bottom_track_kind));
        jl["qc_viewed_fraction"] = util::JsonValue(static_cast<double>(l->qc_viewed_fraction));
        jl["sss_palette"] = util::JsonValue(l->sss_palette);
        jl["sbp_palette"] = util::JsonValue(l->sbp_palette);
        jl["sonar_name"]  = util::JsonValue(l->sonar_name);
        jl["survey_name"] = util::JsonValue(l->survey_name);
        jl["vessel_name"] = util::JsonValue(l->vessel_name);
        jl["start_time_utc"] = util::JsonValue(l->start_time_utc);
        jl["end_time_utc"] = util::JsonValue(l->end_time_utc);
        jl["frequency_hz"]     = util::JsonValue(static_cast<double>(l->frequency_hz));
        jl["low_frequency_hz"] = util::JsonValue(static_cast<double>(l->low_frequency_hz));

        util::JsonValue jindex = util::JsonValue::object();
        jindex["source_id"] = util::JsonValue(l->artifact_index.source_id);

        // Only embed entries when there is no .dlpd cache to recover from.
        // For layers with a valid parsed store the entries are rebuilt at
        // project-open time from the compact binary file, keeping the .dlp
        // manifest small regardless of survey size.
        const bool has_dlpd = !l->artifact_store_path.empty()
            && (normaliseFormat(l->artifact_store_format) == "dlpd"
                || normaliseFormat(l->artifact_store_format) == "dpcache");
        if (!has_dlpd) {
            util::JsonValue entries = util::JsonValue::array();
            for (auto& e : l->artifact_index.entries) {
                util::JsonValue je = util::JsonValue::object();
                je["artifact_id"]  = util::JsonValue(static_cast<double>(e.artifact_id));
                je["type"]         = util::JsonValue(static_cast<int>(e.type));
                je["timestamp_us"] = util::JsonValue(static_cast<double>(e.timestamp_us));
                je["file_offset"]  = util::JsonValue(static_cast<double>(e.file_offset));
                je["subrecord_offset"] = util::JsonValue(static_cast<double>(e.subrecord_offset));
                je["byte_length"]  = util::JsonValue(static_cast<double>(e.byte_length));
                je["lat"]          = util::JsonValue(e.lat);
                je["lon"]          = util::JsonValue(e.lon);
                je["spatial_ref_kind"] = util::JsonValue(
                    geo::spatialRefKindToString(e.spatial_ref_kind));
                je["is_projected"] = util::JsonValue(e.is_projected);
                entries.push(std::move(je));
            }
            jindex["entries"] = std::move(entries);
        }
        jl["artifact_index"] = std::move(jindex);
        // Embed the graph JSON as a nested object (parse it back from the string)
        jl["graph"] = util::parseJson(l->node_graph.toJson());
        // Tags and group
        util::JsonValue ltags = util::JsonValue::array();
        for (const auto& t : l->tags)
            ltags.push(util::JsonValue(t));
        jl["tags"]     = std::move(ltags);
        jl["group_id"] = util::JsonValue(l->group_id);
        layers_arr.push(std::move(jl));
    }
    root["layers"] = std::move(layers_arr);

    // Contacts
    util::JsonValue contacts_arr = util::JsonValue::array();
    for (auto& c : m_contacts) {
        util::JsonValue jc = util::JsonValue::object();
        jc["id"]             = util::JsonValue(static_cast<double>(c.id));
        jc["label"]          = util::JsonValue(c.label);
        jc["lat"]            = util::JsonValue(c.lat);
        jc["lon"]            = util::JsonValue(c.lon);
        jc["spatial_ref"]    = spatialRefToJson(c.spatial_ref);
        jc["depth_m"]        = util::JsonValue(std::isfinite(c.depth_m)  ? static_cast<double>(c.depth_m)  : 0.0);
        jc["range_m"]        = util::JsonValue(std::isfinite(c.range_m)  ? static_cast<double>(c.range_m)  : 0.0);
        jc["width_m"]        = util::JsonValue(std::isfinite(c.width_m)  ? static_cast<double>(c.width_m)  : 0.0);
        jc["height_m"]       = util::JsonValue(std::isfinite(c.height_m) ? static_cast<double>(c.height_m) : 0.0);
        jc["artifact_id"]    = util::JsonValue(static_cast<double>(c.artifact_id));
        jc["sample_idx"]     = util::JsonValue(static_cast<double>(c.sample_idx));
        jc["line_id"]        = util::JsonValue(c.line_id);
        jc["classification"] = util::JsonValue(c.classification);
        jc["confidence"]     = util::JsonValue(confidenceToString(c.confidence));
        jc["notes"]          = util::JsonValue(c.notes);
        jc["created_at"]     = util::JsonValue(c.created_at);
        jc["modified_at"]    = util::JsonValue(c.modified_at);
        util::JsonValue ctags = util::JsonValue::array();
        for (const auto& t : c.tags)
            ctags.push(util::JsonValue(t));
        jc["tags"]     = std::move(ctags);
        jc["group_id"] = util::JsonValue(c.group_id);
        contacts_arr.push(std::move(jc));
    }
    root["contacts"] = std::move(contacts_arr);

    // Layer groups
    util::JsonValue layer_groups_arr = util::JsonValue::array();
    for (const auto& g : m_layer_groups) {
        util::JsonValue jg = util::JsonValue::object();
        jg["id"]       = util::JsonValue(g.id);
        jg["name"]     = util::JsonValue(g.name);
        jg["expanded"] = util::JsonValue(g.expanded);
        layer_groups_arr.push(std::move(jg));
    }
    root["layer_groups"] = std::move(layer_groups_arr);

    // Contact groups
    util::JsonValue contact_groups_arr = util::JsonValue::array();
    for (const auto& g : m_contact_groups) {
        util::JsonValue jg = util::JsonValue::object();
        jg["id"]       = util::JsonValue(g.id);
        jg["name"]     = util::JsonValue(g.name);
        jg["expanded"] = util::JsonValue(g.expanded);
        contact_groups_arr.push(std::move(jg));
    }
    root["contact_groups"] = std::move(contact_groups_arr);

    return root.dump();
}

} // namespace dolphin::app
