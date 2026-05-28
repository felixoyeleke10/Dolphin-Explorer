#pragma once

// Central app identity constants.
// Change this file when rebranding — QSettings scopes, window titles,
// and about-page strings all pull from here.
namespace dolphin::ui::AppInfo {
    inline constexpr const char* kAppName     = "Dolphin Explorer";
    inline constexpr const char* kOrgName     = "Dolphin";
    inline constexpr const char* kSettingsApp = "DolphinExplorer";  // QSettings application key
    inline constexpr const char* kVersion     = "0.1.0";
}
