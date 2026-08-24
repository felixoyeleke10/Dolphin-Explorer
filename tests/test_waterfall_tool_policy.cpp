#include "ui/features/waterfall/interaction/WaterfallToolPolicy.h"

#include <cassert>
#include <iostream>

namespace tools = dolphin::ui::waterfalltools;

int main()
{
    tools::Selection state;
    state = tools::selectSeabed(state, tools::SeabedTool::Pen);
    assert(state.seabed == tools::SeabedTool::Pen);
    assert(state.contact == tools::ContactTool::None);

    state = tools::selectContact(state, tools::ContactTool::Pick);
    assert(state.seabed == tools::SeabedTool::None);
    assert(state.contact == tools::ContactTool::Pick);

    state = tools::selectFeature(state, tools::FeatureTool::Polygon);
    assert(state.contact == tools::ContactTool::None);
    assert(state.feature == tools::FeatureTool::Polygon);

    assert(tools::seabedToolFromIndex(99) == tools::SeabedTool::None);
    assert(tools::featureToolFromIndex(-1) == tools::FeatureTool::None);
    std::cout << "WaterfallToolPolicy checks passed\n";
    return 0;
}
