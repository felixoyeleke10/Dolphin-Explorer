#include "ui/shell/WindowChrome.h"
#include "ui/shell/Theme.h"

#include <QWidget>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

namespace dolphin::ui {

void applyDarkTitleBar(QWidget* window)
{
#ifdef Q_OS_WIN
    if (!window || !window->isWindow()) return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return;

    // Follows the active theme: dark frames in dark mode, native light in light.
    BOOL dark = (Theme::mode() == Theme::Mode::Dark) ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 20H1+); 19 is the pre-20H1 value.
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
#else
    Q_UNUSED(window);
#endif
}

} // namespace dolphin::ui
