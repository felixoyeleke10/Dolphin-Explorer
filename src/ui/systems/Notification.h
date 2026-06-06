#pragma once
#include <string>

namespace dolphin::ui {

enum class NotificationSeverity {
    Info,
    Warning,
    Error,
};

// A structured message from any subsystem to the UI feed.
// Post via AppState::postNotification(); subscribe to AppState::notificationPosted().
struct Notification {
    NotificationSeverity severity = NotificationSeverity::Info;
    std::string          message;
    std::string          source;  // subsystem tag, e.g. "AutoSave", "Import"
};

} // namespace dolphin::ui
