// EpsgDatabase.cpp — curated database of survey-relevant EPSG coordinate systems.
#include "geo/EpsgDatabase.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>

namespace dolphin::geo {

namespace {

using Kind = core::SpatialRefKind;

static std::vector<EpsgEntry> buildDatabase()
{
    std::vector<EpsgEntry> db;
    db.reserve(300);

    // -- Geographic ----------------------------------------------------------
    db.push_back({4326, "WGS 84",  "WGS 84",  Kind::Geographic, true});
    db.push_back({4258, "ETRS89",  "ETRS89",  Kind::Geographic, false});
    db.push_back({4283, "GDA94",   "GDA94",   Kind::Geographic, false});
    db.push_back({7844, "GDA2020", "GDA2020", Kind::Geographic, false});
    db.push_back({4269, "NAD83",   "NAD83",   Kind::Geographic, false});
    db.push_back({4230, "ED50",    "ED50",    Kind::Geographic, false});

    // -- Common national grids -----------------------------------------------
    db.push_back({27700, "British National Grid",    "OSGB36 / British National Grid",       Kind::Projected, true});
    db.push_back({28992, "RD New (Netherlands)",     "Amersfoort / RD New",                  Kind::Projected, false});
    db.push_back({ 2056, "Swiss LV95",               "CH1903+ / LV95",                       Kind::Projected, false});
    db.push_back({ 3006, "SWEREF99 TM",              "SWEREF99 TM",                          Kind::Projected, false});
    db.push_back({ 3857, "Web Mercator",             "WGS 84 / Pseudo-Mercator",             Kind::Projected, false});

    // -- WGS 84 / UTM zones — all 120 ---------------------------------------
    // Zones 28–37 cover most of Europe, North Sea, and Atlantic — mark common.
    // Zones 10–20 cover the Americas — mark common.
    // Zones 50–58 cover Asia-Pacific / Australia — mark common.
    for (int z = 1; z <= 60; ++z) {
        const std::string zs = std::to_string(z);
        const bool commonN = (z >= 10 && z <= 20) || (z >= 28 && z <= 37) || (z >= 50 && z <= 58);
        db.push_back({
            32600 + z,
            "UTM Zone " + zs + "N",
            "WGS 84 / UTM Zone " + zs + "N",
            Kind::Projected, commonN
        });
        const bool commonS = (z >= 50 && z <= 58);
        db.push_back({
            32700 + z,
            "UTM Zone " + zs + "S",
            "WGS 84 / UTM Zone " + zs + "S",
            Kind::Projected, commonS
        });
    }

    // -- ETRS89 / UTM (European reference, zones 28–38) ---------------------
    for (int z = 28; z <= 38; ++z) {
        const std::string zs = std::to_string(z);
        db.push_back({
            25800 + z,
            "ETRS89 / UTM Zone " + zs + "N",
            "ETRS89 / UTM Zone " + zs + "N",
            Kind::Projected, true
        });
    }

    // -- NAD83 / UTM (North America, zones 1–23) ----------------------------
    for (int z = 1; z <= 23; ++z) {
        const std::string zs = std::to_string(z);
        db.push_back({
            26900 + z,
            "NAD83 / UTM Zone " + zs + "N",
            "NAD83 / UTM Zone " + zs + "N",
            Kind::Projected, (z >= 14 && z <= 20)
        });
    }

    // -- GDA94 / MGA (Australia, zones 49–56) -------------------------------
    for (int z = 49; z <= 56; ++z) {
        const std::string zs = std::to_string(z);
        db.push_back({
            28300 + z,
            "GDA94 / MGA Zone " + zs,
            "GDA94 / MGA Zone " + zs,
            Kind::Projected, true
        });
    }

    // -- GDA2020 / MGA (Australia, zones 49–56) -----------------------------
    for (int z = 49; z <= 56; ++z) {
        const std::string zs = std::to_string(z);
        db.push_back({
            7800 + z,
            "GDA2020 / MGA Zone " + zs,
            "GDA2020 / MGA Zone " + zs,
            Kind::Projected, true
        });
    }

    // -- ED50 / UTM (legacy European surveys, zones 28–38) ------------------
    for (int z = 28; z <= 38; ++z) {
        const std::string zs = std::to_string(z);
        const bool common = (z >= 29 && z <= 32);  // North Sea / NW Europe
        db.push_back({
            23000 + z,
            "ED50 / UTM Zone " + zs + "N",
            "ED50 / UTM Zone " + zs + "N",
            Kind::Projected, common
        });
    }

    // Sort: common entries first, then by full_name alphabetically.
    std::stable_sort(db.begin(), db.end(), [](const EpsgEntry& a, const EpsgEntry& b) {
        if (a.is_common != b.is_common) return a.is_common > b.is_common;
        return a.full_name < b.full_name;
    });

    return db;
}

} // namespace

const std::vector<EpsgEntry>& epsgDatabase()
{
    static const std::vector<EpsgEntry> db = buildDatabase();
    return db;
}

const EpsgEntry* epsgByCode(int code)
{
    if (code <= 0) return nullptr;
    for (const auto& e : epsgDatabase())
        if (e.code == code) return &e;
    return nullptr;
}

const EpsgEntry* epsgByRef(const core::SpatialRef& ref)
{
    if (ref.id.empty()) return nullptr;

    // Normalise to upper-case for comparison
    std::string norm;
    norm.reserve(ref.id.size());
    for (char c : ref.id)
        norm.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

    // "WGS84" alias
    if (norm == "WGS84") return epsgByCode(4326);

    // "EPSG:NNNN"
    if (norm.size() > 5 && norm.rfind("EPSG:", 0) == 0) {
        int code = 0;
        const char* first = norm.data() + 5;
        const char* last  = norm.data() + norm.size();
        if (std::from_chars(first, last, code).ec == std::errc{})
            return epsgByCode(code);
    }

    return nullptr;
}

std::string epsgDisplayName(const core::SpatialRef& ref)
{
    if (epsgIsUnconfirmed(ref))
        return "Projected \xe2\x80\x94 CRS not set";  // UTF-8 em dash

    if (ref.id.empty()) {
        switch (ref.kind) {
        case core::SpatialRefKind::Geographic: return "Geographic";
        case core::SpatialRefKind::Projected:  return "Projected";
        case core::SpatialRefKind::Local:      return "Local";
        default:                               return "\xe2\x80\x94";
        }
    }

    if (const EpsgEntry* e = epsgByRef(ref))
        return e->full_name + "  \xc2\xb7  EPSG:" + std::to_string(e->code);

    // Unknown code the user typed — show verbatim
    return ref.id;
}

bool epsgIsUnconfirmed(const core::SpatialRef& ref)
{
    if (ref.id.empty()) return false;
    if (ref.id.rfind("PROJECTED:", 0) == 0) return true;
    return false;
}

} // namespace dolphin::geo
