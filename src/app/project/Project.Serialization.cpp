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

static std::string featureTypeToString(core::FeatureType t) {
    switch (t) {
    case core::FeatureType::Polyline: return "polyline";
    default:                          return "polygon";
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
    if (!m_draping_surface.empty())
        root["draping_surface"] = util::JsonValue(m_draping_surface);

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

        // Raster layers — the GeoTIFF/image source is the durable store, so persist
        // its metadata (kind, size, geo-transform, CRS, extent) for reopen.
        if (l->raster.valid) {
            const auto& r = l->raster;
            util::JsonValue jr = util::JsonValue::object();
            jr["is_depth"] = util::JsonValue(r.is_depth);
            jr["cols"]     = util::JsonValue(static_cast<int>(r.cols));
            jr["rows"]     = util::JsonValue(static_cast<int>(r.rows));
            jr["gt0"] = util::JsonValue(r.geo_transform[0]);
            jr["gt1"] = util::JsonValue(r.geo_transform[1]);
            jr["gt2"] = util::JsonValue(r.geo_transform[2]);
            jr["gt3"] = util::JsonValue(r.geo_transform[3]);
            jr["gt4"] = util::JsonValue(r.geo_transform[4]);
            jr["gt5"] = util::JsonValue(r.geo_transform[5]);
            jr["crs_wkt"] = util::JsonValue(r.crs_wkt);
            jr["min_x"] = util::JsonValue(r.min_x);
            jr["min_y"] = util::JsonValue(r.min_y);
            jr["max_x"] = util::JsonValue(r.max_x);
            jr["max_y"] = util::JsonValue(r.max_y);
            jl["raster"] = jr;
        }

        // Per-layer SSS display state — only written when the user has applied params.
        if (l->sss_display_state.customized) {
            const auto& p = l->sss_display_state.params;
            util::JsonValue jd = util::JsonValue::object();
            jd["gain"]       = util::JsonValue(static_cast<double>(p.gain));
            jd["contrast"]   = util::JsonValue(static_cast<double>(p.contrast));
            jd["threshold"]  = util::JsonValue(static_cast<double>(p.threshold));
            jd["smoothing"]  = util::JsonValue(static_cast<double>(p.smoothing));
            jd["channel"]    = util::JsonValue(static_cast<int>(p.display_channel));
            jd["src"]        = util::JsonValue(p.slant_range_correction);
            jd["tvg_en"]     = util::JsonValue(p.tvg.enabled);
            jd["tvg_spread"] = util::JsonValue(static_cast<double>(p.tvg.spreading));
            jd["tvg_absorb"] = util::JsonValue(static_cast<double>(p.tvg.absorption));
            jd["agc_en"]     = util::JsonValue(p.agc.enabled);
            jd["agc_mode"]   = util::JsonValue(static_cast<int>(p.agc.mode));
            jd["agc_str"]    = util::JsonValue(static_cast<double>(p.agc.strength));
            jd["agc_win"]    = util::JsonValue(p.agc.along_track_win);
            jd["arc_en"]     = util::JsonValue(p.arc.enabled);
            jd["arc_exp"]    = util::JsonValue(static_cast<double>(p.arc.exponent));
            jd["arc_cap"]    = util::JsonValue(static_cast<double>(p.arc.gain_cap_db));
            // Imaging chain (ARN / Destripe / Beam Pattern / ML Enhance) — must be
            // persisted: they feed the map raster-cache fingerprint, so losing them
            // on reopen invalidates the cached mosaic and forces a full rebuild.
            jd["arn_en"]     = util::JsonValue(p.arn.enabled);
            jd["arn_str"]    = util::JsonValue(static_cast<double>(p.arn.strength));
            jd["arn_cap"]    = util::JsonValue(static_cast<double>(p.arn.gain_cap_db));
            jd["arn_smooth"] = util::JsonValue(p.arn.column_smooth);
            jd["ds_en"]      = util::JsonValue(p.destripe.enabled);
            jd["ds_win"]     = util::JsonValue(p.destripe.window);
            jd["ds_sub"]     = util::JsonValue(p.destripe.subdivision);
            jd["ds_cap"]     = util::JsonValue(static_cast<double>(p.destripe.capping));
            jd["bpn_en"]     = util::JsonValue(p.beam_pattern.enabled);
            jd["bpn_str"]    = util::JsonValue(static_cast<double>(p.beam_pattern.strength));
            jd["bpn_rad"]    = util::JsonValue(p.beam_pattern.smooth_radius);
            jd["ml_en"]      = util::JsonValue(p.ml_enhance.enabled);
            jd["ml_tp"]      = util::JsonValue(p.ml_enhance.tile_pings);
            jd["ml_ts"]      = util::JsonValue(p.ml_enhance.tile_samps);
            jd["ml_clip"]    = util::JsonValue(static_cast<double>(p.ml_enhance.clip_limit));
            jl["sss_display"] = std::move(jd);
        }

        // Per-layer SBP display state — only written when any side has been applied.
        if (l->sbp_display_state.display_customized
                || l->sbp_display_state.gain_customized
                || l->sbp_display_state.signal_customized) {
            const auto& d = l->sbp_display_state;
            util::JsonValue jd = util::JsonValue::object();
            jd["disp_ok"]         = util::JsonValue(d.display_customized);
            jd["gain_ok"]         = util::JsonValue(d.gain_customized);
            jd["sig_ok"]          = util::JsonValue(d.signal_customized);
            jd["disp_gain"]       = util::JsonValue(static_cast<double>(d.display.gain));
            jd["disp_contrast"]   = util::JsonValue(static_cast<double>(d.display.contrast));
            jd["invert"]          = util::JsonValue(d.display.polarity_invert);
            jd["bttrack"]         = util::JsonValue(d.display.show_bottom_track);
            jd["sv_ms"]           = util::JsonValue(static_cast<double>(d.display.sound_speed_ms));
            jd["gain_static_en"]  = util::JsonValue(d.gain.static_gain_en);
            jd["gain_static_db"]  = util::JsonValue(static_cast<double>(d.gain.static_gain_db));
            jd["gain_agc_en"]     = util::JsonValue(d.gain.agc_en);
            jd["gain_agc_win"]    = util::JsonValue(d.gain.agc_window);
            jd["gain_norm_en"]    = util::JsonValue(d.gain.normalize_en);
            jd["sig_env_en"]      = util::JsonValue(d.signal.envelope_en);
            jd["sig_dc_en"]       = util::JsonValue(d.signal.dc_removal_en);
            jd["sig_bp_en"]       = util::JsonValue(d.signal.bandpass_en);
            jd["sig_bp_lo"]       = util::JsonValue(static_cast<double>(d.signal.bp_lo_hz));
            jd["sig_bp_hi"]       = util::JsonValue(static_cast<double>(d.signal.bp_hi_hz));
            jl["sbp_display"] = std::move(jd);
        }

        // Per-layer nav-correction state (model-owned; SSS + SBP) — only written
        // when the user has applied corrections, so untouched layers stay clean.
        if (l->nav_customized) {
            const auto& nv = l->nav_state;
            util::JsonValue jn = util::JsonValue::object();
            jn["smooth_en"]   = util::JsonValue(nv.smooth_enabled);
            jn["smooth_win"]  = util::JsonValue(nv.smooth_window);
            jn["layback_en"]  = util::JsonValue(nv.layback_enabled);
            jn["layback_m"]   = util::JsonValue(static_cast<double>(nv.layback_m));
            jn["heading_off"] = util::JsonValue(static_cast<double>(nv.heading_offset_deg));
            jn["pitch_off"]   = util::JsonValue(static_cast<double>(nv.pitch_offset_deg));
            jn["roll_off"]    = util::JsonValue(static_cast<double>(nv.roll_offset_deg));
            jl["nav_state"] = std::move(jn);
        }

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
        // For layers with a valid parsed store the entries are reloaded at
        // project-open time directly from the binary footer (O(N) binary read,
        // no full file scan), keeping the manifest small and all fields intact.
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

    // Contacts (active + recycle bin share one serialization shape).
    auto contactToJson = [](const core::Contact& c) -> util::JsonValue {
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
        // "object_length_m", not "length_m": the legacy "length_m" key held slant
        // range and still maps to range_m on read — reusing it would corrupt reads.
        jc["object_length_m"] = util::JsonValue(std::isfinite(c.length_m) ? static_cast<double>(c.length_m) : 0.0);
        jc["shadow_m"]       = util::JsonValue(std::isfinite(c.shadow_m) ? static_cast<double>(c.shadow_m) : 0.0);
        jc["burial_depth_m"] = util::JsonValue(std::isfinite(c.burial_depth_m) ? static_cast<double>(c.burial_depth_m) : 0.0);
        jc["height_not_measurable"] = util::JsonValue(c.height_not_measurable);
        jc["symbol"]         = util::JsonValue(c.symbol);
        jc["color_rgb"]      = util::JsonValue(static_cast<double>(c.color_rgb));
        jc["use_for_report"] = util::JsonValue(c.use_for_report);
        jc["visible"]        = util::JsonValue(c.visible);
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
        return jc;
    };

    util::JsonValue contacts_arr = util::JsonValue::array();
    for (const auto& c : m_contacts) contacts_arr.push(contactToJson(c));
    root["contacts"] = std::move(contacts_arr);

    util::JsonValue recycled_arr = util::JsonValue::array();
    for (const auto& c : m_recycled_contacts) recycled_arr.push(contactToJson(c));
    root["recycled_contacts"] = std::move(recycled_arr);

    // Features — SHAPE annotations (polylines/polygons).
    auto featureToJson = [](const core::Feature& f) -> util::JsonValue {
        util::JsonValue jf = util::JsonValue::object();
        jf["id"]             = util::JsonValue(static_cast<double>(f.id));
        jf["label"]          = util::JsonValue(f.label);
        jf["type"]           = util::JsonValue(featureTypeToString(f.type));
        util::JsonValue verts = util::JsonValue::array();
        for (const auto& v : f.vertices) {
            util::JsonValue jv = util::JsonValue::object();
            jv["lat"] = util::JsonValue(v.lat);
            jv["lon"] = util::JsonValue(v.lon);
            verts.push(std::move(jv));
        }
        jf["vertices"]       = std::move(verts);
        jf["spatial_ref"]    = spatialRefToJson(f.spatial_ref);
        jf["line_id"]        = util::JsonValue(f.line_id);
        jf["classification"] = util::JsonValue(f.classification);
        jf["notes"]          = util::JsonValue(f.notes);
        jf["created_at"]     = util::JsonValue(f.created_at);
        jf["modified_at"]    = util::JsonValue(f.modified_at);
        util::JsonValue ftags = util::JsonValue::array();
        for (const auto& t : f.tags)
            ftags.push(util::JsonValue(t));
        jf["tags"]     = std::move(ftags);
        jf["group_id"] = util::JsonValue(f.group_id);
        jf["visible"]  = util::JsonValue(f.visible);
        return jf;
    };
    util::JsonValue features_arr = util::JsonValue::array();
    for (const auto& f : m_features) features_arr.push(featureToJson(f));
    root["features"] = std::move(features_arr);

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
