// Project.Serialization.Read.cpp — Project::fromJson()
//
// Project::toJson() and shared JSON/path codec helpers live in
// Project.Serialization.cpp.

#include "app/project/Project.h"
#include "app/project/Project_p.h"
#include "app/layers/LayerUtils.h"
#include "geo/GeoUtils.h"
#include "io/cache/ParsedCache.h"
#include "util/Json.h"

#include <algorithm>
#include <cstdint>

namespace dolphin::app {

namespace {

using detail::normalisePath;
using detail::normaliseFormat;
using detail::manifestDirectory;

// -- Decode helpers ------------------------------------------------------------

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

static core::FeatureType featureTypeFromString(const std::string& s) {
    if (s == "polyline") return core::FeatureType::Polyline;
    return core::FeatureType::Polygon;
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

// -- Path helpers --------------------------------------------------------------

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

// -- Validation helpers --------------------------------------------------------

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

// -- fromJson ------------------------------------------------------------------

bool Project::fromJson(const std::string& json)
{
    util::JsonValue root = util::parseJson(json);
    if (!root.isObject()) return false;

    const int version = std::max(1, root.get("version").asInt());
    if (version > kSchemaVersion) {
        // Forward-compat guard: refuse cleanly instead of silently misparsing
        // fields this build does not understand.
        m_load_error = "This project was saved by a newer version of "
                       "Dolphin Explorer (manifest v" + std::to_string(version) +
                       "; this build reads up to v" +
                       std::to_string(kSchemaVersion) + ").";
        return false;
    }
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
    m_draping_surface = root.has("draping_surface")
        ? root.get("draping_surface").asString() : std::string{};

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
        layer->pipeline_applied = jl.has("pipeline_applied") ? jl.get("pipeline_applied").asBool() : false;
        layer->bottom_track_kind = static_cast<BottomTrackKind>(
            jl.get("bottom_track_kind").asInt());
        layer->qc_viewed_fraction = static_cast<float>(
            jl.get("qc_viewed_fraction").asDouble());
        layer->sss_palette = jl.has("sss_palette") ? jl.get("sss_palette").asInt() : -1;
        layer->sbp_palette = jl.has("sbp_palette") ? jl.get("sbp_palette").asInt() : -1;
        // v11 optional fields; pre-v11 manifests default to opaque / Blend.
        layer->map_opacity = jl.has("map_opacity")
            ? std::clamp(static_cast<float>(jl.get("map_opacity").asDouble()), 0.f, 1.f)
            : 1.0f;
        layer->map_blend_mode = jl.has("map_blend_mode")
            ? jl.get("map_blend_mode").asInt() : 0;
        layer->map_clip_polygons = jl.has("map_clip_polygons")
            && jl.get("map_clip_polygons").asBool();
        layer->map_show_beams = jl.has("map_show_beams")
            && jl.get("map_show_beams").asBool();

        if (jl.has("raster")) {
            const auto& jr = jl.get("raster");
            auto& r = layer->raster;
            r.valid    = true;
            r.is_depth = jr.get("is_depth").asBool();
            r.cols     = static_cast<std::uint32_t>(jr.get("cols").asInt());
            r.rows     = static_cast<std::uint32_t>(jr.get("rows").asInt());
            r.geo_transform[0] = jr.get("gt0").asDouble();
            r.geo_transform[1] = jr.get("gt1").asDouble();
            r.geo_transform[2] = jr.get("gt2").asDouble();
            r.geo_transform[3] = jr.get("gt3").asDouble();
            r.geo_transform[4] = jr.get("gt4").asDouble();
            r.geo_transform[5] = jr.get("gt5").asDouble();
            r.crs_wkt = jr.get("crs_wkt").asString();
            r.min_x = jr.get("min_x").asDouble();  r.min_y = jr.get("min_y").asDouble();
            r.max_x = jr.get("max_x").asDouble();  r.max_y = jr.get("max_y").asDouble();
        }

        if (jl.has("sss_display")) {
            const auto& jd = jl.get("sss_display");
            auto& p = layer->sss_display_state.params;
            p.gain              = static_cast<float>(jd.get("gain").asDouble());
            p.contrast          = static_cast<float>(jd.get("contrast").asDouble());
            p.threshold         = static_cast<float>(jd.get("threshold").asDouble());
            p.smoothing         = static_cast<float>(jd.get("smoothing").asDouble());
            p.display_channel   = static_cast<dolphin::ui::DisplayChannel>(jd.get("channel").asInt());
            p.slant_range_correction = jd.get("src").asBool();
            p.tvg.enabled    = jd.get("tvg_en").asBool();
            p.tvg.spreading  = static_cast<float>(jd.get("tvg_spread").asDouble());
            p.tvg.absorption = static_cast<float>(jd.get("tvg_absorb").asDouble());
            p.agc.enabled         = jd.get("agc_en").asBool();
            p.agc.mode            = static_cast<dolphin::app::AgcMode>(jd.get("agc_mode").asInt());
            p.agc.strength        = static_cast<float>(jd.get("agc_str").asDouble());
            p.agc.along_track_win = jd.get("agc_win").asInt();
            p.arc.enabled      = jd.get("arc_en").asBool();
            p.arc.exponent     = static_cast<float>(jd.get("arc_exp").asDouble());
            p.arc.gain_cap_db  = static_cast<float>(jd.get("arc_cap").asDouble());
            // Imaging chain — guarded for projects saved before these keys existed
            // (missing → params keep their disabled defaults).
            if (jd.has("arn_en")) {
                p.arn.enabled      = jd.get("arn_en").asBool();
                p.arn.strength     = static_cast<float>(jd.get("arn_str").asDouble());
                p.arn.gain_cap_db  = static_cast<float>(jd.get("arn_cap").asDouble());
                p.arn.column_smooth = jd.get("arn_smooth").asInt();
            }
            if (jd.has("ds_en")) {
                p.destripe.enabled     = jd.get("ds_en").asBool();
                p.destripe.window      = jd.get("ds_win").asInt();
                p.destripe.subdivision = jd.get("ds_sub").asInt();
                p.destripe.capping     = static_cast<float>(jd.get("ds_cap").asDouble());
            }
            if (jd.has("bpn_en")) {
                p.beam_pattern.enabled      = jd.get("bpn_en").asBool();
                p.beam_pattern.strength     = static_cast<float>(jd.get("bpn_str").asDouble());
                p.beam_pattern.smooth_radius = jd.get("bpn_rad").asInt();
            }
            if (jd.has("ml_en")) {
                p.ml_enhance.enabled    = jd.get("ml_en").asBool();
                p.ml_enhance.tile_pings = jd.get("ml_tp").asInt();
                p.ml_enhance.tile_samps = jd.get("ml_ts").asInt();
                p.ml_enhance.clip_limit = static_cast<float>(jd.get("ml_clip").asDouble());
            }
            layer->sss_display_state.customized = true;
        }

        if (jl.has("sbp_display")) {
            const auto& jd = jl.get("sbp_display");
            auto& d = layer->sbp_display_state;
            d.display_customized = jd.has("disp_ok") ? jd.get("disp_ok").asBool() : false;
            d.gain_customized    = jd.get("gain_ok").asBool();
            d.signal_customized  = jd.get("sig_ok").asBool();
            d.display.gain              = static_cast<float>(jd.get("disp_gain").asDouble());
            d.display.contrast          = static_cast<float>(jd.get("disp_contrast").asDouble());
            d.display.polarity_invert   = jd.get("invert").asBool();
            d.display.show_bottom_track = jd.get("bttrack").asBool();
            d.display.sound_speed_ms    = static_cast<float>(jd.get("sv_ms").asDouble());
            d.gain.static_gain_en = jd.get("gain_static_en").asBool();
            d.gain.static_gain_db = static_cast<float>(jd.get("gain_static_db").asDouble());
            d.gain.agc_en         = jd.get("gain_agc_en").asBool();
            d.gain.agc_window     = jd.get("gain_agc_win").asInt();
            d.gain.normalize_en   = jd.get("gain_norm_en").asBool();
            d.signal.envelope_en   = jd.get("sig_env_en").asBool();
            d.signal.dc_removal_en = jd.get("sig_dc_en").asBool();
            d.signal.bandpass_en   = jd.get("sig_bp_en").asBool();
            d.signal.bp_lo_hz      = static_cast<float>(jd.get("sig_bp_lo").asDouble());
            d.signal.bp_hi_hz      = static_cast<float>(jd.get("sig_bp_hi").asDouble());
        }

        if (jl.has("nav_state")) {
            const auto& jn = jl.get("nav_state");
            auto& nv = layer->nav_state;
            nv.smooth_enabled     = jn.get("smooth_en").asBool();
            nv.smooth_window      = jn.get("smooth_win").asInt();
            nv.layback_enabled    = jn.get("layback_en").asBool();
            nv.layback_m          = static_cast<float>(jn.get("layback_m").asDouble());
            nv.heading_offset_deg = static_cast<float>(jn.get("heading_off").asDouble());
            nv.pitch_offset_deg   = static_cast<float>(jn.get("pitch_off").asDouble());
            nv.roll_offset_deg    = static_cast<float>(jn.get("roll_off").asDouble());
            layer->nav_customized = true;
        }

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

        // When entries were not saved in the manifest (layers with a .dlpd store),
        // load them now via the binary footer fast path: two file seeks + N×56-byte
        // binary reads.  For typical surveys (≤10k pings) this is sub-millisecond
        // on SSD — all fields are preserved (including frequency_hz and ping_number
        // which JSON serialization omits).  Falls back to background rebuildCacheIndex
        // only if the footer is absent (pre-footer .dlpd) or corrupt.
        if (layer->artifact_index.entries.empty() && !layer->artifact_store_path.empty()) {
            const std::string sfmt = normaliseFormat(layer->artifact_store_format);
            const bool store_valid = (sfmt == "dlpd" || sfmt == "dpcache")
                && io::parsedCacheIsValid(layer->artifact_store_path);
            if (store_valid) {
                io::ParsedCacheReader cache_reader;
                if (cache_reader.open(layer->artifact_store_path)) {
                    // Read file-header metadata (sonar/survey/vessel names) before
                    // buildIndex() so we can back-fill missing JSON fields.
                    const io::FormatMeta hdr_meta = cache_reader.metadata();
                    if (layer->sonar_name.empty() && !hdr_meta.sonar_name.empty())
                        layer->sonar_name = hdr_meta.sonar_name;
                    if (layer->survey_name.empty() && !hdr_meta.survey_name.empty())
                        layer->survey_name = hdr_meta.survey_name;
                    if (layer->vessel_name.empty() && !hdr_meta.vessel_name.empty())
                        layer->vessel_name = hdr_meta.vessel_name;

                    // quickIndex() reads only the compact footer — two seeks plus
                    // N×56-byte reads, never falls back to a full file scan.
                    // Returns empty when no footer exists; the safety-net below
                    // leaves index_built=false so loadProject schedules a
                    // background rebuild (with progress) instead.
                    auto loaded = cache_reader.quickIndex();
                    if (!loaded.empty()) {
                        // Preserve the layer's source_id (buildIndex sets it to the
                        // file path, which is wrong here).
                        loaded.source_id = layer->artifact_index.source_id.empty()
                            ? layer->source_id
                            : layer->artifact_index.source_id;

                        // Re-apply modality filter: a shared .dlpd (mixed-modality
                        // source, e.g. XTF with SSS+SBP) holds all artifact types;
                        // this layer only owns one family.
                        if (layer->modality != Modality::Unknown
                                && layer->modality != Modality::Mixed) {
                            const core::ArtifactType wanted =
                                artifactTypeForModality(layer->modality);
                            bool has_siblings = false;
                            for (const auto& e : loaded.entries)
                                if (e.type != wanted) { has_siblings = true; break; }
                            if (has_siblings)
                                loaded = filteredByType(loaded, wanted);
                        }

                        layer->artifact_index = std::move(loaded);

                        // Back-fill pipeline_applied for old projects: non-zero
                        // correction_flags_seen means corrections are baked in.
                        // metadata() after buildIndex() returns the updated value.
                        if (!layer->pipeline_applied
                                && cache_reader.metadata().correction_flags_seen != 0)
                            layer->pipeline_applied = true;
                    }
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

        // Raster layers are not backed by a .dlpd ping cache — their store is the
        // GeoTIFF/image file itself, so skip the parsed-cache validation entirely.
        if (version >= 5 && layer->modality != Modality::Raster
                && (layer->index_built || !layer->artifact_index.empty())) {
            const std::string store_format = normaliseFormat(layer->artifact_store_format);
            const bool missing_store = (store_format != "dlpd" && store_format != "dpcache")
                || layer->artifact_store_path.empty()
                || !QFileInfo(QString::fromStdString(layer->artifact_store_path)).exists();
            // Reject the cached index only when the binary store is gone or has an
            // incompatible format version.  Source-file staleness (renamed, re-acquired
            // data, etc.) does not invalidate a perfectly readable .dlpd — the user
            // can trigger a re-import explicitly if they want fresh data.
            const bool stale_cache = !missing_store
                && !io::parsedCacheIsValid(layer->artifact_store_path);
            if (missing_store) {
                // File is gone — clear everything so the layer can be re-imported.
                layer->artifact_index.entries.clear();
                layer->index_built = false;
                layer->artifact_store_path.clear();
                layer->artifact_store_format.clear();
            } else if (stale_cache) {
                // File exists but header version is outside the accepted range.
                // Clear cached entries and let rebuildCacheIndex attempt a scan —
                // it will report a proper error via indexingFailed rather than
                // silently stranding the layer with no path to trigger rebuild.
                layer->artifact_index.entries.clear();
                layer->index_built = false;
            }
        }

        if (layer->artifact_index.source_id.empty())
            layer->artifact_index.source_id = layer->source_id;
        if (!layer->artifact_index.empty())
            layer->index_built = true;
        // If entries are still empty after the footer read (pre-footer .dlpd,
        // corrupt footer, or no store), fall back to background rebuildCacheIndex.
        if (layer->artifact_index.empty() && !layer->artifact_store_path.empty()
                && layer->modality != Modality::Raster)
            layer->index_built = false;
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

        // -- Migration: split legacy Mixed-modality layers ---------------------
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
                    ex->slant_range_corrected = layer->slant_range_corrected;
                    ex->pipeline_applied      = layer->pipeline_applied;
                    ex->bottom_track_kind     = layer->bottom_track_kind;
                    ex->qc_viewed_fraction    = layer->qc_viewed_fraction;
                    ex->group_id              = layer->group_id;
                    ex->tags                  = layer->tags;
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

    // Contacts (active + recycle bin share one parse).
    uint64_t max_id = 0;
    auto contactFromJson = [&](const util::JsonValue& jc) -> core::Contact {
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
        c.length_m       = jc.get("object_length_m").asFloat();
        c.shadow_m       = jc.get("shadow_m").asFloat();
        c.burial_depth_m = jc.get("burial_depth_m").asFloat();
        c.height_not_measurable = jc.get("height_not_measurable").asBool();
        c.symbol         = jc.get("symbol").asString();
        c.color_rgb      = static_cast<uint32_t>(jc.get("color_rgb").asDouble());
        c.use_for_report = jc.get("use_for_report").asBool();
        // Absent key (older projects) must default to visible.
        c.visible        = jc.get("visible").isNull() ? true
                         : jc.get("visible").asBool();
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
        return c;
    };

    m_contacts.clear();
    for (auto& jc : root.get("contacts").elements())
        m_contacts.push_back(contactFromJson(jc));

    m_recycled_contacts.clear();
    for (auto& jc : root.get("recycled_contacts").elements())
        m_recycled_contacts.push_back(contactFromJson(jc));

    m_next_contact_id = (max_id < UINT64_MAX) ? max_id + 1 : 1;

    // Features — SHAPE annotations (polylines/polygons).
    uint64_t max_feat_id = 0;
    m_features.clear();
    for (const auto& jf : root.get("features").elements()) {
        core::Feature f;
        f.id             = static_cast<uint64_t>(jf.get("id").asDouble());
        f.label          = jf.get("label").asString();
        f.type           = featureTypeFromString(jf.get("type").asString());
        for (const auto& jv : jf.get("vertices").elements()) {
            core::GeoVertex v;
            v.lat = jv.get("lat").asDouble();
            v.lon = jv.get("lon").asDouble();
            f.vertices.push_back(v);
        }
        f.spatial_ref    = spatialRefFromJson(jf.get("spatial_ref"));
        if (f.spatial_ref.empty())
            f.spatial_ref = m_display_spatial_ref;
        f.line_id        = jf.get("line_id").asString();
        f.classification = jf.get("classification").asString();
        f.notes          = jf.get("notes").asString();
        f.created_at     = jf.get("created_at").asDouble();
        f.modified_at    = jf.get("modified_at").asDouble();
        for (const auto& jt : jf.get("tags").elements())
            f.tags.push_back(jt.asString());
        f.group_id = jf.get("group_id").asString();
        f.visible  = jf.get("visible").isNull() ? true : jf.get("visible").asBool();
        if (f.id > max_feat_id) max_feat_id = f.id;
        m_features.push_back(std::move(f));
    }
    m_next_feature_id = (max_feat_id < UINT64_MAX) ? max_feat_id + 1 : 1;

    // Layer groups
    m_layer_groups.clear();
    for (const auto& jg : root.get("layer_groups").elements()) {
        ItemGroup g;
        g.id       = jg.get("id").asString();
        g.name     = jg.get("name").asString();
        g.expanded = jg.has("expanded") ? jg.get("expanded").asBool() : true;
        if (!g.id.empty())
            m_layer_groups.push_back(std::move(g));
    }

    // Contact groups
    m_contact_groups.clear();
    for (const auto& jg : root.get("contact_groups").elements()) {
        ItemGroup g;
        g.id       = jg.get("id").asString();
        g.name     = jg.get("name").asString();
        g.expanded = jg.has("expanded") ? jg.get("expanded").asBool() : true;
        if (!g.id.empty())
            m_contact_groups.push_back(std::move(g));
    }

    return true;
}

} // namespace dolphin::app
