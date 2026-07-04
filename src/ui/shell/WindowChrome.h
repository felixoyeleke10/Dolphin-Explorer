#pragma once

class QWidget;

namespace dolphin::ui {

// Sync a top-level window's NATIVE frame (Windows DWM title bar) with the
// active Theme::mode() — dark frames in dark mode, native light in light mode.
// The main window is frameless (custom chrome), but every other top-level —
// viewers, dialogs, the contact editor/manager — uses the native frame.
// Call once per window (idempotent; safe to call on re-shows).
// No-op on non-Windows platforms.
void applyDarkTitleBar(QWidget* window);

} // namespace dolphin::ui
