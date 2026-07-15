// Project.Serialization.Entities.cpp — contact, feature, and group reconstruction.

#include "app/project/Project.h"
#include "app/project/Project_p.h"
#include "util/Json.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace dolphin::app {

namespace {

using detail::spatialRefFromJson;

static core::Confidence confidenceFromString(const std::string& s)
{
    if (s == "probable") return core::Confidence::Probable;
    if (s == "certain")  return core::Confidence::Certain;
    return core::Confidence::Possible;
}

static core::FeatureType featureTypeFromString(const std::string& s)
{
    if (s == "polyline") return core::FeatureType::Polyline;
    return core::FeatureType::Polygon;
}

template <typename UInt>
static UInt boundedUnsigned(double value)
{
    static_assert(std::is_unsigned_v<UInt>);
    if (!std::isfinite(value) || value <= 0.0)
        return 0;
    const double upper = static_cast<double>(std::numeric_limits<UInt>::max());
    if (value >= upper)
        return std::numeric_limits<UInt>::max();
    return static_cast<UInt>(value);
}

static bool validObjectId(double value)
{
    // JsonValue stores numbers as doubles. Keep model identities within the
    // exactly representable integer range so save/open cannot silently merge
    // adjacent IDs above 2^53.
    constexpr double kMaxExactJsonInteger = 9007199254740991.0;
    return std::isfinite(value) && value >= 1.0
        && value <= kMaxExactJsonInteger
        && std::trunc(value) == value;
}

} // namespace

bool Project::restoreEntitiesFromJson(const util::JsonValue& root)
{
    // Contacts (active + recycle bin share one parse).
    uint64_t max_id = 0;
    bool contact_ids_valid = true;
    std::unordered_set<uint64_t> contact_ids;
    auto contactFromJson = [&](const util::JsonValue& jc) -> core::Contact {
        core::Contact c;
        const double id_value = jc.get("id").asDouble();
        if (!validObjectId(id_value)) {
            contact_ids_valid = false;
            return c;
        }
        c.id = static_cast<uint64_t>(id_value);
        if (!contact_ids.insert(c.id).second) {
            contact_ids_valid = false;
            return c;
        }
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
        c.color_rgb = boundedUnsigned<uint32_t>(jc.get("color_rgb").asDouble());
        c.use_for_report = jc.get("use_for_report").asBool();
        // Absent key (older projects) must default to visible.
        c.visible        = jc.get("visible").isNull() ? true
                         : jc.get("visible").asBool();
        c.artifact_id = boundedUnsigned<uint64_t>(jc.get("artifact_id").asDouble());
        c.sample_idx = boundedUnsigned<uint32_t>(jc.get("sample_idx").asDouble());
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
    for (auto& jc : root.get("contacts").elements()) {
        if (!jc.isObject()) {
            m_load_error = "Each project contact must be an object.";
            return false;
        }
        auto contact = contactFromJson(jc);
        if (!contact_ids_valid) {
            m_load_error = "Project contains an invalid or duplicate contact ID.";
            return false;
        }
        m_contacts.push_back(std::move(contact));
    }

    m_recycled_contacts.clear();
    for (auto& jc : root.get("recycled_contacts").elements()) {
        if (!jc.isObject()) {
            m_load_error = "Each recycled project contact must be an object.";
            return false;
        }
        auto contact = contactFromJson(jc);
        if (!contact_ids_valid) {
            m_load_error = "Project contains an invalid or duplicate contact ID.";
            return false;
        }
        m_recycled_contacts.push_back(std::move(contact));
    }

    m_next_contact_id = (max_id < UINT64_MAX) ? max_id + 1 : 1;

    // Features — SHAPE annotations (polylines/polygons).
    uint64_t max_feat_id = 0;
    std::unordered_set<uint64_t> feature_ids;
    m_features.clear();
    for (const auto& jf : root.get("features").elements()) {
        if (!jf.isObject()) {
            m_load_error = "Each project feature must be an object.";
            return false;
        }
        core::Feature f;
        const double id_value = jf.get("id").asDouble();
        if (!validObjectId(id_value)) {
            m_load_error = "Project contains an invalid feature ID.";
            return false;
        }
        f.id = static_cast<uint64_t>(id_value);
        if (!feature_ids.insert(f.id).second) {
            m_load_error = "Project contains a duplicate feature ID.";
            return false;
        }
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
    std::unordered_set<std::string> layer_group_ids;
    for (const auto& jg : root.get("layer_groups").elements()) {
        if (!jg.isObject()) {
            m_load_error = "Each project layer group must be an object.";
            return false;
        }
        ItemGroup g;
        g.id       = jg.get("id").asString();
        g.name     = jg.get("name").asString();
        g.expanded = jg.has("expanded") ? jg.get("expanded").asBool() : true;
        if (!g.id.empty()) {
            if (!layer_group_ids.insert(g.id).second) {
                m_load_error = "Project contains a duplicate layer-group ID.";
                return false;
            }
            m_layer_groups.push_back(std::move(g));
        }
    }

    // Contact groups
    m_contact_groups.clear();
    std::unordered_set<std::string> contact_group_ids;
    for (const auto& jg : root.get("contact_groups").elements()) {
        if (!jg.isObject()) {
            m_load_error = "Each project contact group must be an object.";
            return false;
        }
        ItemGroup g;
        g.id       = jg.get("id").asString();
        g.name     = jg.get("name").asString();
        g.expanded = jg.has("expanded") ? jg.get("expanded").asBool() : true;
        if (!g.id.empty()) {
            if (!contact_group_ids.insert(g.id).second) {
                m_load_error = "Project contains a duplicate contact-group ID.";
                return false;
            }
            m_contact_groups.push_back(std::move(g));
        }
    }

    return true;
}

} // namespace dolphin::app
