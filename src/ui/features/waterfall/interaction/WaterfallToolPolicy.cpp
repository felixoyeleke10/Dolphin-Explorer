#include "ui/features/waterfall/interaction/WaterfallToolPolicy.h"

namespace dolphin::ui::waterfalltools {

SeabedTool seabedToolFromIndex(int value)
{
    return value >= 1 && value <= 3 ? static_cast<SeabedTool>(value)
                                    : SeabedTool::None;
}

ContactTool contactToolFromIndex(int value)
{
    return value == 1 ? ContactTool::Pick : ContactTool::None;
}

FeatureTool featureToolFromIndex(int value)
{
    return value >= 1 && value <= 3 ? static_cast<FeatureTool>(value)
                                    : FeatureTool::None;
}

Selection selectSeabed(Selection current, SeabedTool tool)
{
    current.seabed = tool;
    if (tool != SeabedTool::None) {
        current.contact = ContactTool::None;
        current.feature = FeatureTool::None;
    }
    return current;
}

Selection selectContact(Selection current, ContactTool tool)
{
    current.contact = tool;
    if (tool != ContactTool::None) {
        current.seabed = SeabedTool::None;
        current.feature = FeatureTool::None;
    }
    return current;
}

Selection selectFeature(Selection current, FeatureTool tool)
{
    current.feature = tool;
    if (tool != FeatureTool::None) {
        current.seabed = SeabedTool::None;
        current.contact = ContactTool::None;
    }
    return current;
}

} // namespace dolphin::ui::waterfalltools
