#pragma once

namespace dolphin::ui::waterfalltools {

enum class SeabedTool { None = 0, Pen = 1, Box = 2, Eraser = 3 };
enum class ContactTool { None = 0, Pick = 1 };
enum class FeatureTool { None = 0, Polygon = 1, Line = 2, Pen = 3 };

struct Selection {
    SeabedTool seabed = SeabedTool::None;
    ContactTool contact = ContactTool::None;
    FeatureTool feature = FeatureTool::None;
};

SeabedTool seabedToolFromIndex(int value);
ContactTool contactToolFromIndex(int value);
FeatureTool featureToolFromIndex(int value);

Selection selectSeabed(Selection current, SeabedTool tool);
Selection selectContact(Selection current, ContactTool tool);
Selection selectFeature(Selection current, FeatureTool tool);

} // namespace dolphin::ui::waterfalltools
