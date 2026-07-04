#pragma once

#include <QString>
#include "ui/shell/Theme.h"

namespace dolphin::ui {

// AppStyle owns the global QSS layer for standard Qt widgets.
//
// Usage:
//   qApp->setStyleSheet(AppStyle::sheet()); // once at startup
//
// Custom-painted widgets should use Theme tokens so they stay visually aligned
// with the stylesheet rather than creating a separate palette.
struct AppStyle {
    static QString sheet();                 // QSS for the CURRENT Theme::mode()
    // Palette + stylesheet + native window frames in one step. Call at startup
    // (from the persisted setting) and whenever the user switches theme.
    static void    apply(Theme::Mode mode);
};

} // namespace dolphin::ui
