#include "pipeline/NodeRegistry.h"
#include "pipeline/nodes/input/SidescanInputNode.h"
#include "pipeline/nodes/correction/TvgNode.h"
#include "pipeline/nodes/correction/ArcNode.h"
#include "pipeline/nodes/correction/BandPassNode.h"
#include "pipeline/nodes/correction/BottomDetectNode.h"
#include "pipeline/nodes/correction/SlantRangeNode.h"
#include "pipeline/nodes/correction/GeoCorrectNode.h"
#include "pipeline/nodes/correction/NavSmoothNode.h"
#include "pipeline/nodes/filter/ClipRangeNode.h"
#include "pipeline/nodes/filter/SpeckleFilterNode.h"
#include "pipeline/nodes/filter/DecimatNode.h"
#include "pipeline/nodes/enhancement/GainNormalizeNode.h"
#include "pipeline/nodes/enhancement/ContrastEnhanceNode.h"
#include "pipeline/nodes/enhancement/HistogramEqNode.h"
#include "pipeline/nodes/enhancement/SidescanEnhancementNode.h"
#include "pipeline/nodes/analysis/ShadowDetectNode.h"
#include "pipeline/nodes/output/ExportGeoTiffNode.h"
#include "pipeline/nodes/output/ExportCsvNode.h"
#include "pipeline/nodes/merge/MergeNode.h"
#include "pipeline/nodes/input/SBPInputNode.h"
#include "pipeline/nodes/input/MBESInputNode.h"

namespace dolphin::pipeline {

NodeRegistry& NodeRegistry::instance()
{
    static NodeRegistry reg;
    return reg;
}

void NodeRegistry::registerType(const std::string& type_id,
                                 std::function<NodePtr()> factory)
{
    if (!m_factories.count(type_id))
        m_order.push_back(type_id);
    m_factories[type_id] = std::move(factory);
}

NodePtr NodeRegistry::create(const std::string& type_id) const
{
    auto it = m_factories.find(type_id);
    return it != m_factories.end() ? it->second() : nullptr;
}

bool NodeRegistry::isKnown(const std::string& type_id) const
{
    return m_factories.count(type_id) > 0;
}

std::vector<std::string> NodeRegistry::allTypeIds() const
{
    return m_order;
}

void registerBuiltinNodes()
{
    auto& reg = NodeRegistry::instance();

    // DataIn
    reg.registerType("sss_input",        []{ return std::make_shared<SidescanInputNode>(); });
    reg.registerType("sbp_input",        []{ return std::make_shared<SBPInputNode>(); });
    reg.registerType("mbes_input",       []{ return std::make_shared<MBESInputNode>(); });

    // Correction
    reg.registerType("tvg",              []{ return std::make_shared<TvgNode>(); });
    reg.registerType("arc",              []{ return std::make_shared<ArcNode>(); });
    reg.registerType("slant_range",      []{ return std::make_shared<SlantRangeNode>(); });
    reg.registerType("geocorrect",       []{ return std::make_shared<GeoCorrectNode>(); });
    reg.registerType("nav_smooth",       []{ return std::make_shared<NavSmoothNode>(); });
    reg.registerType("clip_range",       []{ return std::make_shared<ClipRangeNode>(); });

    // Filter
    reg.registerType("bpf",             []{ return std::make_shared<BandPassNode>(); });
    reg.registerType("speckle",         []{ return std::make_shared<SpeckleFilterNode>(); });
    reg.registerType("decimate",        []{ return std::make_shared<DecimatNode>(); });

    // Enhancement
    reg.registerType("gain_normalize",  []{ return std::make_shared<GainNormalizeNode>(); });
    reg.registerType("contrast_enhance",[]{ return std::make_shared<ContrastEnhanceNode>(); });
    reg.registerType("histogram_eq",    []{ return std::make_shared<HistogramEqNode>(); });
    reg.registerType("arn",             []{ return std::make_shared<SidescanEnhancementNode>(SidescanEnhancementKind::Arn); });
    reg.registerType("destripe",        []{ return std::make_shared<SidescanEnhancementNode>(SidescanEnhancementKind::Destripe); });
    reg.registerType("beam_pattern",    []{ return std::make_shared<SidescanEnhancementNode>(SidescanEnhancementKind::BeamPattern); });
    reg.registerType("adaptive_contrast",[]{ return std::make_shared<SidescanEnhancementNode>(SidescanEnhancementKind::AdaptiveContrast); });

    // Analysis
    reg.registerType("bottom_detect",   []{ return std::make_shared<BottomDetectNode>(); });
    reg.registerType("shadow_detect",   []{ return std::make_shared<ShadowDetectNode>(); });

    // Merge
    reg.registerType("merge",           []{ return std::make_shared<MergeNode>(); });
    reg.registerType("blend",           []{ return std::make_shared<BlendNode>(); });
    reg.registerType("multi_merge",     []{ return std::make_shared<MultiMergeNode>(); });

    // Output
    reg.registerType("export_geotiff",  []{ return std::make_shared<ExportGeoTiffNode>(); });
    reg.registerType("export_csv",      []{ return std::make_shared<ExportCsvNode>(); });
}

} // namespace dolphin::pipeline
