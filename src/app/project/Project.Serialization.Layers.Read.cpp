// Project.Serialization.Layers.Read.cpp — layer reconstruction for Project::fromJson()
//
// Project::fromJson() orchestration and shared read codecs live in
// Project.Serialization.Read.cpp.

#include "app/project/Project.h"
#include "app/project/Project_p.h"
#include "app/contracts/ProcessingSettingsContract.h"
#include "app/layers/LayerUtils.h"
#include "geo/GeoUtils.h"
#include "io/cache/ParsedCache.h"
#include "util/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace dolphin::app {

namespace {

using detail::normaliseFormat;
using detail::resolveStoredPath;
using detail::spatialRefFromJson;

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

// -- Validation helpers --------------------------------------------------------

static const ProjectSource* findSourceById(const std::vector<ProjectSource>& sources,
                                           const std::string& source_id)
{
    auto it = std::find_if(sources.begin(), sources.end(),
        [&](const ProjectSource& src) { return src.id == source_id; });
    return (it != sources.end()) ? &(*it) : nullptr;
}

template <typename UInt>
static bool readExactUnsigned(const util::JsonValue& node,
                              UInt& result,
                              bool require_nonzero = false)
{
    static_assert(std::is_unsigned_v<UInt>);
    if (!node.isNumber())
        return false;

    const double value = node.asDouble();
    const double upper_exclusive = std::ldexp(
        1.0, std::numeric_limits<UInt>::digits);
    if (!std::isfinite(value) || value < 0.0
            || value >= upper_exclusive || std::trunc(value) != value
            || (require_nonzero && value == 0.0)) {
        return false;
    }

    result = static_cast<UInt>(value);
    return true;
}

static bool readExactInt64(const util::JsonValue& node, int64_t& result)
{
    if (!node.isNumber())
        return false;

    const double value = node.asDouble();
    const double upper_exclusive = std::ldexp(
        1.0, std::numeric_limits<int64_t>::digits);
    const double lower_inclusive = -upper_exclusive;
    if (!std::isfinite(value) || value < lower_inclusive
            || value >= upper_exclusive || std::trunc(value) != value) {
        return false;
    }

    result = static_cast<int64_t>(value);
    return true;
}

} // namespace

bool Project::restoreLayersFromJson(const util::JsonValue& root, int version)
{
    std::unordered_set<std::string> source_ids;
    for (const auto& source : m_sources)
        source_ids.insert(source.id);

    // Layers — support both "layers" (v2 new) and "lines" (v2 old) keys
    m_layers.clear();
    std::unordered_set<std::string> layer_ids;
    auto& layers_node = root.has("layers") ? root.get("layers") : root.get("lines");
    for (auto& jl : layers_node.elements()) {
        if (!jl.isObject()) {
            m_load_error = "Each project layer must be an object.";
            return false;
        }
        auto layer = std::make_unique<DataLayer>(this);
        layer->id        = jl.get("id").asString();
        layer->label     = jl.get("label").asString();
        layer->source_id = jl.get("source_id").asString();
        if (layer->id.empty() || !layer_ids.insert(layer->id).second) {
            m_load_error = "Project contains an empty or duplicate layer ID.";
            return false;
        }
        if (layer->source_id.empty() || !source_ids.count(layer->source_id)) {
            m_load_error = "Project layer references a missing source.";
            return false;
        }
        if (layer->label.empty()) layer->label = layer->id;
        layer->artifact_store_path =
            resolveStoredPath(jl.get("artifact_store_path").asString(), m_manifest_path);
        layer->artifact_store_format = jl.get("artifact_store_format").asString();
        layer->source_artifact_store_path = jl.has("source_artifact_store_path")
            ? resolveStoredPath(jl.get("source_artifact_store_path").asString(), m_manifest_path)
            : std::string{};
        layer->modality  = modalityFromString(jl.get("modality").asString());
        layer->source_spatial_ref = spatialRefFromJson(
            jl.get("source_spatial_ref"), {});
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
        layer->baked_correction_flags = jl.has("baked_correction_flags")
            ? static_cast<uint32_t>(std::max(0, jl.get("baked_correction_flags").asInt()))
            : 0u;
        layer->processing_origin = jl.has("processing_origin")
            ? static_cast<ProcessingOrigin>(std::clamp(
                jl.get("processing_origin").asInt(), 0, 3))
            : (layer->pipeline_applied ? ProcessingOrigin::LegacyUnknown
                                       : ProcessingOrigin::None);
        layer->applied_graph_json = jl.has("applied_graph_json")
            ? jl.get("applied_graph_json").asString() : std::string{};
        // Migrate older manifests that retained only the active sidecar path.
        // Accept a recovered baseline only after validating its cache footer.
        if (layer->source_artifact_store_path.empty()) {
            if (!layer->pipeline_applied) {
                layer->source_artifact_store_path = layer->artifact_store_path;
            } else {
                namespace fs = std::filesystem;
                const fs::path active(layer->artifact_store_path);
                const std::string suffix = "_" + layer->id;
                const std::string stem = active.stem().string();
                if (stem.size() > suffix.size()
                    && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    const fs::path candidate = active.parent_path()
                        / (stem.substr(0, stem.size() - suffix.size()) + ".dlpd");
                    if (io::parsedCacheIsValid(candidate.string()))
                        layer->source_artifact_store_path = candidate.string();
                }
            }
        }
        layer->bottom_track_kind = static_cast<BottomTrackKind>(std::clamp(
            jl.get("bottom_track_kind").asInt(),
            static_cast<int>(BottomTrackKind::Unknown),
            static_cast<int>(BottomTrackKind::Mixed)));
        layer->qc_viewed_fraction = std::clamp(
            jl.get("qc_viewed_fraction").asFloat(), 0.f, 1.f);
        layer->sss_palette = jl.has("sss_palette") ? jl.get("sss_palette").asInt() : -1;
        layer->sbp_palette = jl.has("sbp_palette") ? jl.get("sbp_palette").asInt() : -1;
        // v11 optional fields; pre-v11 manifests default to opaque / Blend.
        layer->map_opacity = jl.has("map_opacity")
            ? std::clamp(jl.get("map_opacity").asFloat(), 0.f, 1.f)
            : 1.0f;
        layer->map_blend_mode = jl.has("map_blend_mode")
            ? std::clamp(jl.get("map_blend_mode").asInt(), 0, 3) : 0;
        layer->map_clip_polygons = jl.has("map_clip_polygons")
            && jl.get("map_clip_polygons").asBool();
        layer->map_show_beams = jl.has("map_show_beams")
            && jl.get("map_show_beams").asBool();
        layer->map_beam_spacing = jl.has("map_beam_spacing")
            ? std::clamp(jl.get("map_beam_spacing").asInt(), 1, 50) : 10;

        if (jl.has("raster")) {
            const auto& jr = jl.get("raster");
            auto& r = layer->raster;
            r.valid    = true;
            r.is_depth = jr.get("is_depth").asBool();
            r.cols     = static_cast<std::uint32_t>(
                std::max(0, jr.get("cols").asInt()));
            r.rows     = static_cast<std::uint32_t>(
                std::max(0, jr.get("rows").asInt()));
            r.valid    = r.cols > 0 && r.rows > 0;
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
            p.gain              = jd.get("gain").asFloat();
            p.contrast          = jd.get("contrast").asFloat();
            p.threshold         = jd.get("threshold").asFloat();
            p.smoothing         = jd.get("smoothing").asFloat();
            p.display_channel = static_cast<dolphin::ui::DisplayChannel>(
                std::clamp(jd.get("channel").asInt(), 0, 2));
            p.slant_range_correction = jd.get("src").asBool();
            p.tvg.enabled    = jd.get("tvg_en").asBool();
            p.tvg.spreading  = jd.get("tvg_spread").asFloat();
            p.tvg.absorption = jd.get("tvg_absorb").asFloat();
            if (jd.has("tvg_fallback_blank"))
                p.tvg.fallback_blanking_m = jd.get("tvg_fallback_blank").asFloat();
            p.agc.enabled         = jd.get("agc_en").asBool();
            p.agc.mode = static_cast<dolphin::app::AgcMode>(
                std::clamp(jd.get("agc_mode").asInt(), 0, 1));
            p.agc.strength        = jd.get("agc_str").asFloat();
            p.agc.along_track_win = jd.get("agc_win").asInt();
            if (jd.has("agc_smooth_type"))
                p.agc.smoothing_type = static_cast<dolphin::app::AgcSmoothingType>(
                    std::clamp(jd.get("agc_smooth_type").asInt(), 0, 1));
            if (jd.has("agc_smooth_win"))
                p.agc.smoothing_win = jd.get("agc_smooth_win").asInt();
            if (jd.has("agc_edge_skip"))
                p.agc.edge_skip_samples = jd.get("agc_edge_skip").asInt();
            if (jd.has("agc_noise_floor"))
                p.agc.noise_floor_pct = jd.get("agc_noise_floor").asFloat();
            if (jd.has("agc_gain_cap"))
                p.agc.gain_cap_db = jd.get("agc_gain_cap").asFloat();
            if (jd.has("agc_target"))
                p.agc.target_mean = jd.get("agc_target").asFloat();
            p.arc.enabled      = jd.get("arc_en").asBool();
            p.arc.exponent     = jd.get("arc_exp").asFloat();
            p.arc.gain_cap_db  = jd.get("arc_cap").asFloat();
            // Imaging chain — guarded for projects saved before these keys existed
            // (missing → params keep their disabled defaults).
            if (jd.has("arn_en")) {
                p.arn.enabled      = jd.get("arn_en").asBool();
                p.arn.strength     = jd.get("arn_str").asFloat();
                p.arn.gain_cap_db  = jd.get("arn_cap").asFloat();
                p.arn.column_smooth = jd.get("arn_smooth").asInt();
            }
            if (jd.has("ds_en")) {
                p.destripe.enabled     = jd.get("ds_en").asBool();
                p.destripe.window      = jd.get("ds_win").asInt();
                p.destripe.subdivision = jd.get("ds_sub").asInt();
                p.destripe.capping     = jd.get("ds_cap").asFloat();
                if (jd.has("ds_thresh"))
                    p.destripe.threshold_db = jd.get("ds_thresh").asFloat();
            }
            if (jd.has("bpn_en")) {
                p.beam_pattern.enabled      = jd.get("bpn_en").asBool();
                p.beam_pattern.strength     = jd.get("bpn_str").asFloat();
                p.beam_pattern.smooth_radius = jd.get("bpn_rad").asInt();
                if (jd.has("bpn_cap"))
                    p.beam_pattern.gain_cap_db = jd.get("bpn_cap").asFloat();
            }
            if (jd.has("ml_en")) {
                p.ml_enhance.enabled    = jd.get("ml_en").asBool();
                p.ml_enhance.tile_pings = jd.get("ml_tp").asInt();
                p.ml_enhance.tile_samps = jd.get("ml_ts").asInt();
                p.ml_enhance.clip_limit = jd.get("ml_clip").asFloat();
            }
            layer->sss_display_state.customized = true;
            if (const auto error = contracts::validate(p); !error.empty()) {
                m_load_warnings.push_back("Layer '" + layer->label
                    + "': ignored invalid sidescan settings (" + error + ").");
                p = dolphin::ui::WaterfallParams{};
                layer->sss_display_state.customized = false;
            }
        }

        if (jl.has("sbp_display")) {
            const auto& jd = jl.get("sbp_display");
            auto& d = layer->sbp_display_state;
            d.display_customized = jd.has("disp_ok") ? jd.get("disp_ok").asBool() : false;
            d.gain_customized    = jd.get("gain_ok").asBool();
            d.signal_customized  = jd.get("sig_ok").asBool();
            d.display.gain              = jd.get("disp_gain").asFloat();
            d.display.contrast          = jd.get("disp_contrast").asFloat();
            d.display.polarity_invert   = jd.get("invert").asBool();
            d.display.show_bottom_track = jd.get("bttrack").asBool();
            d.display.sound_speed_ms    = jd.get("sv_ms").asFloat();
            d.gain.static_gain_en = jd.get("gain_static_en").asBool();
            d.gain.static_gain_db = jd.get("gain_static_db").asFloat();
            d.gain.agc_en         = jd.get("gain_agc_en").asBool();
            d.gain.agc_window     = jd.get("gain_agc_win").asInt();
            if (jd.has("gain_agc_cap"))
                d.gain.agc_gain_cap_db = jd.get("gain_agc_cap").asFloat();
            d.gain.normalize_en   = jd.get("gain_norm_en").asBool();
            d.signal.envelope_en   = jd.get("sig_env_en").asBool();
            d.signal.dc_removal_en = jd.get("sig_dc_en").asBool();
            d.signal.bandpass_en   = jd.get("sig_bp_en").asBool();
            d.signal.bp_lo_hz      = jd.get("sig_bp_lo").asFloat();
            d.signal.bp_hi_hz      = jd.get("sig_bp_hi").asFloat();
            if (const auto error = contracts::validate(d.display); !error.empty()) {
                m_load_warnings.push_back("Layer '" + layer->label
                    + "': ignored invalid sub-bottom display settings (" + error + ").");
                d.display = dolphin::ui::SubBottomDisplayParams{};
                d.display_customized = false;
            }
            if (const auto error = contracts::validate(d.gain, d.signal); !error.empty()) {
                m_load_warnings.push_back("Layer '" + layer->label
                    + "': ignored invalid sub-bottom processing settings (" + error + ").");
                d.gain = SbpGainParams{};
                d.signal = SbpSignalParams{};
                d.gain_customized = false;
                d.signal_customized = false;
            }
        }

        if (jl.has("nav_state")) {
            const auto& jn = jl.get("nav_state");
            auto& nv = layer->nav_state;
            nv.smooth_enabled     = jn.get("smooth_en").asBool();
            nv.smooth_window      = jn.get("smooth_win").asInt();
            nv.layback_enabled    = jn.get("layback_en").asBool();
            nv.layback_m          = jn.get("layback_m").asFloat();
            nv.heading_offset_deg = jn.get("heading_off").asFloat();
            nv.pitch_offset_deg   = jn.get("pitch_off").asFloat();
            nv.roll_offset_deg    = jn.get("roll_off").asFloat();
            layer->nav_customized = true;
            if (const auto error = contracts::validate(nv); !error.empty()) {
                m_load_warnings.push_back("Layer '" + layer->label
                    + "': ignored invalid navigation settings (" + error + ").");
                nv = dolphin::ui::NavProcessingParams{};
                layer->nav_customized = false;
            }
        }

        layer->sonar_name  = jl.get("sonar_name").asString();
        layer->survey_name = jl.get("survey_name").asString();
        layer->vessel_name = jl.get("vessel_name").asString();
        layer->start_time_utc = jl.get("start_time_utc").asDouble();
        layer->end_time_utc = jl.get("end_time_utc").asDouble();
        layer->frequency_hz     = jl.get("frequency_hz").asFloat();
        layer->low_frequency_hz = jl.get("low_frequency_hz").asFloat();

        bool rejected_persisted_index_entries = false;
        bool restored_index_from_store = false;
        auto& jindex = jl.get("artifact_index");
        if (jindex.isObject()) {
            const std::string persisted_index_source_id =
                jindex.get("source_id").asString();
            // Reader-built indexes historically stored a file path here (usually
            // the .dlpd path), while project layers use a logical ProjectSource
            // ID.  Both encodings were written by released manifests.  The
            // layer's validated source_id is the authoritative relationship; a
            // mismatch makes persisted offsets untrusted, but must not strand an
            // otherwise valid project.  Discard them below and restore only from
            // the layer's validated durable store.
            if (!persisted_index_source_id.empty()
                    && persisted_index_source_id != layer->source_id)
                rejected_persisted_index_entries = true;
            layer->artifact_index.source_id = layer->source_id;

            const auto& entries_node = jindex.get("entries");
            if (jindex.has("entries") && !entries_node.isArray()) {
                rejected_persisted_index_entries = true;
            } else {
                for (const auto& je : entries_node.elements()) {
                    uint32_t type_value = 0;
                    uint64_t artifact_id = 0;
                    int64_t timestamp_us = 0;
                    uint64_t file_offset = 0;
                    uint32_t subrecord_offset = 0;
                    uint32_t byte_length = 0;
                    if (!je.isObject()
                            || !readExactUnsigned(je.get("type"), type_value)
                            || type_value < static_cast<uint32_t>(core::ArtifactType::Sidescan)
                            || type_value > static_cast<uint32_t>(core::ArtifactType::Raster)
                            || !readExactUnsigned(je.get("artifact_id"), artifact_id)
                            || !readExactInt64(je.get("timestamp_us"), timestamp_us)
                            || !readExactUnsigned(je.get("file_offset"), file_offset)
                            || !readExactUnsigned(je.get("subrecord_offset"), subrecord_offset)
                            || !readExactUnsigned(je.get("byte_length"), byte_length, true)) {
                        rejected_persisted_index_entries = true;
                        continue;
                    }

                    core::ArtifactIndexEntry entry;
                    entry.artifact_id  = artifact_id;
                    entry.type         = static_cast<core::ArtifactType>(type_value);
                    entry.timestamp_us = timestamp_us;
                    entry.file_offset  = file_offset;
                    entry.subrecord_offset = subrecord_offset;
                    entry.byte_length  = byte_length;
                    entry.lat          = je.get("lat").asDouble();
                    entry.lon          = je.get("lon").asDouble();
                    entry.spatial_ref_kind = geo::spatialRefKindFromString(
                        je.get("spatial_ref_kind").asString());
                    entry.is_projected = je.get("is_projected").asBool();
                    layer->artifact_index.entries.push_back(std::move(entry));
                }
            }
        } else if (jl.has("artifact_index")) {
            rejected_persisted_index_entries = true;
        }

        // A partially accepted seek table is unsafe: either restore the complete
        // index from its durable store below or leave the layer pending a rebuild.
        if (rejected_persisted_index_entries) {
            layer->artifact_index.entries.clear();
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
                        // Parsed-cache readers identify their input by file path;
                        // project layers identify it by logical ProjectSource ID.
                        loaded.source_id = layer->source_id;

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
                        restored_index_from_store = !layer->artifact_index.empty();

                        // Back-fill pipeline_applied for old projects: non-zero
                        // correction_flags_seen means corrections are baked in.
                        // metadata() after buildIndex() returns the updated value.
                        layer->baked_correction_flags |=
                            cache_reader.metadata().correction_flags_seen;
                        if (!layer->pipeline_applied
                                && layer->baked_correction_flags != 0)
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
            // Reject the cached index only when the binary store is gone or lacks a
            // compatible, readable non-empty footer/index. Source-file staleness
            // (renamed, re-acquired data, etc.) does not invalidate a readable .dlpd —
            // the user can trigger a re-import explicitly if they want fresh data.
            const bool stale_cache = !missing_store
                && !io::parsedCacheIsValid(layer->artifact_store_path);
            if (missing_store) {
                // File is gone — clear everything so the layer can be re-imported.
                layer->artifact_index.entries.clear();
                layer->index_built = false;
                layer->artifact_store_path.clear();
                layer->artifact_store_format.clear();
            } else if (stale_cache) {
                // File exists but its header/footer/index is incompatible, missing,
                // or corrupt. Clear cached entries and let rebuildCacheIndex attempt
                // a scan. A legacy footerless file can be repaired; a truncated file
                // reports a proper indexing error instead of reusing stale offsets.
                layer->artifact_index.entries.clear();
                layer->index_built = false;
            }
        }

        // Maintain one application-layer invariant even when a raw/cache reader
        // supplied a path-form locator in an older manifest.
        layer->artifact_index.source_id = layer->source_id;
        if (!layer->artifact_index.empty())
            layer->index_built = true;
        if (rejected_persisted_index_entries && !restored_index_from_store)
            layer->index_built = false;
        // If entries are still empty after the footer read (pre-footer .dlpd,
        // corrupt footer, or no store), fall back to background rebuildCacheIndex.
        if (layer->artifact_index.empty() && !layer->artifact_store_path.empty()
                && layer->modality != Modality::Raster)
            layer->index_built = false;
        if (layer->modality == Modality::Unknown && !layer->artifact_index.empty())
            layer->modality = inferModality(layer->artifact_index);

        // Restore node graph — requires NodeRegistry to be populated.
        // On failure, fromJson clears the graph to a consistent empty state.
        if (jl.has("graph")
                && (!jl.get("graph").isObject()
                    || !layer->node_graph.fromJson(jl.get("graph").dump()))) {
            m_load_error = "Project layer contains an unsupported or invalid processing graph.";
            return false;
        }
        if (layer->processing_origin == ProcessingOrigin::LegacyUnknown) {
            const auto& graph = layer->uses_project_graph
                ? processing_graph : layer->node_graph;
            const bool has_processing_node = std::any_of(
                graph.nodes().cbegin(), graph.nodes().cend(), [](const auto& node) {
                    if (!node) return false;
                    const auto category = node->schema().category;
                    return category != "Input" && category != "Output"
                        && category != "Merge";
                });
            if (has_processing_node)
                layer->processing_origin = ProcessingOrigin::NodeGraph;
        }

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
                    ex->source_artifact_store_path = layer->source_artifact_store_path;
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
                    ex->baked_correction_flags = layer->baked_correction_flags;
                    ex->processing_origin      = layer->processing_origin;
                    ex->applied_graph_json     = layer->applied_graph_json;
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

    return true;
}

} // namespace dolphin::app
