#include "io/ProbeDispatch.h"
#include "io/cache/ParsedCache.h"
#include "io/segy/SegyReader.h"
#include "io/xtf/XtfReader.h"
#include "io/jsf/JsfReader.h"
#include "core/SpatialRef.h"
#include <algorithm>
#include <cctype>

namespace dolphin::io {

namespace {

static std::string lowerExt(const std::string& path)
{
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

std::string fileFilterForArtifactType(core::ArtifactType type)
{
    // ONLY the chosen sensor's supported formats (+ DLPD cache, which can carry any
    // modality) — no "All Files" escape, so the open dialog can't pick another
    // sensor's files. Keep in sync with probeFile()'s extension switch.
    switch (type) {
        case core::ArtifactType::Sidescan:
            return "Sidescan Files (*.xtf *.jsf *.dlpd *.dpcache)";
        case core::ArtifactType::SubBottom:
            return "Sub-Bottom Files (*.segy *.sgy *.dlpd *.dpcache)";
        case core::ArtifactType::Magnetometer:
            return "Magnetometer Files (*.xtf *.dlpd *.dpcache)";
        case core::ArtifactType::Multibeam:
            return "Bathymetry Files (*.dlpd *.dpcache)";
        default:
            return std::string(kSupportedFileFilter);
    }
}

ProbeResult probeFile(const std::string& path)
{
    const std::string ext = lowerExt(path);

    if (ext == ".segy" || ext == ".sgy")
        return SegyReader{}.probe(path);
    if (ext == ".xtf")
        return XtfReader{}.probe(path);
    if (ext == ".jsf")
        return JsfReader{}.probe(path);

    if (ext == ".dlpd" || ext == ".dpcache") {
        ProbeResult r;
        r.path        = path;
        r.format_name = "DLPD";

        ParsedCacheReader reader;
        if (!reader.open(path)) {
            r.error_message = "Cannot open DLPD cache: " + path;
            return r;
        }

        // buildIndex populates modality counts (sidescan_ping_count etc.) by
        // scanning record headers. metadata() alone only returns the file header.
        reader.buildIndex({});
        const FormatMeta meta = reader.metadata();

        r.has_sidescan     = meta.sidescan_ping_count   > 0;
        r.has_subbottom    = meta.subbottom_trace_count > 0;
        r.has_magnetometer = meta.mag_sample_count      > 0;
        r.has_multibeam    = meta.multibeam_ping_count  > 0;

        if (!meta.coordinate_ref.empty()) {
            r.declared_crs   = meta.coordinate_ref;
            r.is_projected   = spatialRefIsProjected(meta.coordinate_ref);
            r.coord_valid    = true;
            r.needs_crs_review = false;
        }

        r.estimated_record_count = meta.artifact_count;

        // Populate channel table so the Channels tab shows what's inside.
        if (r.has_sidescan) {
            ProbeResult::ChannelInfo ch;
            ch.name          = "Sidescan";
            ch.modality      = "Sidescan";
            ch.frequency_khz = meta.frequency_hz / 1000.f;
            r.channels.push_back(ch);
            if (meta.low_frequency_hz > 0.f) {
                ProbeResult::ChannelInfo lo;
                lo.name          = "Sidescan (LF)";
                lo.modality      = "Sidescan";
                lo.frequency_khz = meta.low_frequency_hz / 1000.f;
                r.channels.push_back(lo);
            }
        }
        if (r.has_subbottom) {
            ProbeResult::ChannelInfo ch;
            ch.name     = "Sub-Bottom";
            ch.modality = "Sub-Bottom";
            r.channels.push_back(ch);
        }
        if (r.has_magnetometer) {
            ProbeResult::ChannelInfo ch;
            ch.name     = "Magnetometer";
            ch.modality = "Magnetometer";
            r.channels.push_back(ch);
        }
        if (r.has_multibeam) {
            ProbeResult::ChannelInfo ch;
            ch.name     = "Multibeam";
            ch.modality = "Multibeam";
            r.channels.push_back(ch);
        }

        r.success = r.has_sidescan || r.has_subbottom || r.has_magnetometer || r.has_multibeam;
        if (!r.success)
            r.error_message = "DLPD contains no recognised artifact types";
        return r;
    }

    ProbeResult r;
    r.path          = path;
    r.format_name   = "Unknown";
    r.error_message = "Unsupported file format (" + ext + ")";
    return r;
}

} // namespace dolphin::io
