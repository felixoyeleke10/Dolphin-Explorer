#pragma once
#include <QObject>
#include <cmath>
#include <string>
#include <vector>
#include "core/ArtifactIndex.h"
#include "core/SpatialRef.h"
#include "pipeline/NodeGraph.h"
#include "app/layers/LayerUtils.h"   // Modality enum, modalityLabel(), inferModality()
#include "app/display/SssDisplayState.h"
#include "app/display/SbpDisplayState.h"

namespace dolphin::app {

enum class LayerState {
    Placeholder,  // created in project; import in progress, no artifact data yet
    Indexing,     // background parse running
    Ready,        // artifact store built and index populated; safe for all consumers
    Failed        // indexing failed
};

// Summary of bottom-pick origin across all pings in a layer.
// Only meaningful for modalities that carry bottom tracking (SSS, SBP, MBES).
enum class BottomTrackKind : uint8_t {
    Unknown = 0,  // not yet computed (old project file or layer still indexing)
    None    = 1,  // no bottom picks present
    Auto    = 2,  // algorithm-detected picks only
    Manual  = 3,  // user-edited picks only
    Mixed   = 4,  // both algorithm and user-edited picks present
};

// A DataLayer represents one imported source file or derived product.
// It holds a seek table (ArtifactIndex), processing graph, and modality tag.
class DataLayer : public QObject {
    Q_OBJECT
public:
    explicit DataLayer(QObject* parent = nullptr);

    LayerState          state  = LayerState::Placeholder;
    std::string         id;
    std::string         label;
    std::string         source_id;             // -> ProjectSource::id
    std::string         artifact_store_path;
    std::string         artifact_store_format; // "xtf", "jsf", "dlpd" …
    Modality            modality = Modality::Unknown;
    core::SpatialRef    source_spatial_ref;
    core::ArtifactIndex artifact_index;
    pipeline::NodeGraph node_graph;
    bool                uses_project_graph    = false;
    bool                index_built           = false;
    bool                visible               = true;
    bool                slant_range_corrected = false;  // SRC; persisted in project JSON
    bool                pipeline_applied      = false;  // true after processing pipeline has been persisted
    BottomTrackKind     bottom_track_kind     = BottomTrackKind::Unknown;
    float               qc_viewed_fraction    = 0.f;   // fraction of pings the user has scrolled past [0,1]
    int                 sss_palette           = -1;    // per-layer palette override; -1 = use app default
    int                 sbp_palette           = -1;    // per-layer SBP palette override; -1 = use window default

    // Per-layer display state — written by MainWindow on paramsApplied, read on
    // layer open.  Not yet persisted in project JSON; reset to defaults on project reload.
    dolphin::ui::SssDisplayState sss_display_state;
    dolphin::ui::SbpDisplayState sbp_display_state;

    // Tagging and grouping
    std::vector<std::string> tags;
    std::string              group_id;  // empty = ungrouped; references ItemGroup::id

    // Survey metadata from file header
    std::string         sonar_name;
    std::string         survey_name;
    std::string         vessel_name;
    double              start_time_utc = 0.0;
    double              end_time_utc   = 0.0;
    float               frequency_hz     = 0.0f;
    float               low_frequency_hz = 0.0f;

    int artifactCount() const {
        return static_cast<int>(artifact_index.size());
    }

    int sidescanCount() const {
        return static_cast<int>(
            artifact_index.byType(core::ArtifactType::Sidescan).size());
    }

    int subBottomCount() const {
        return static_cast<int>(
            artifact_index.byType(core::ArtifactType::SubBottom).size());
    }

    // For pinned single-band layers (low_frequency_hz == 0, frequency_hz > 0),
    // returns the count of sidescan entries that belong to the pinned band.
    // For all other layers returns the full sidescanCount().
    int bandSidescanCount() const {
        if (low_frequency_hz != 0.f || frequency_hz <= 0.f)
            return sidescanCount();
        int count = 0;
        for (const auto& e : artifact_index.entries) {
            if (e.type != core::ArtifactType::Sidescan) continue;
            if (e.frequency_hz > 0.f && std::fabs(e.frequency_hz - frequency_hz) < 1.f)
                ++count;
        }
        return count;
    }

    // Total artifact count adjusted for the pinned band (non-sidescan entries
    // are not frequency-tagged and are always included in full).
    int bandArtifactCount() const {
        return artifactCount() - sidescanCount() + bandSidescanCount();
    }


signals:
    void indexReady();
    void processingComplete();
};

} // namespace dolphin::app
