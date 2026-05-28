#pragma once

#include <QString>

namespace dolphin::ui {

// AppStyle owns the global QSS layer for standard Qt widgets.
//
// Usage:
//   qApp->setStyleSheet(AppStyle::sheet()); // once at startup
//
// Custom-painted widgets should use Theme tokens so they stay visually aligned
// with the stylesheet rather than creating a separate palette.
struct AppStyle {
    static QString sheet();
};

} // namespace dolphin::ui
