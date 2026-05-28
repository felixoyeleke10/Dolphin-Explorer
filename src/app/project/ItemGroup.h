#pragma once
#include <string>

namespace dolphin::app {

// A user-defined group that layers or contacts can be assigned to.
// Groups are project-scoped; layer groups and contact groups are stored separately.
struct ItemGroup {
    std::string id;
    std::string name;
    bool        expanded = true;
};

} // namespace dolphin::app
