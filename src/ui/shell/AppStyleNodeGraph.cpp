#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssNodeGraph()
{
    return QString(

        // ─────────────────────────────────────────────────────────────────────
        // Node Graph Window chrome
        // ─────────────────────────────────────────────────────────────────────

        "QFrame#nodeGraphTopBar { background: @bgEl; border-bottom: 1px solid @border; }"
        "QLabel#nodeLayerLabel  { color: @textMuted; font-family: @font; font-size: @fontSm; }"
        "QFrame#nodeDivider     { background: @border; border: none; }"

        // ─────────────────────────────────────────────────────────────────────
        // Node palette (left panel)
        // ─────────────────────────────────────────────────────────────────────

        "QWidget#nodeGraphPalette {"
        "  background: @bgPanel; border-right: 1px solid @border;"
        "}"
        "#nodePaletteList {"
        "  background: transparent; border: none;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "}"

        // ─────────────────────────────────────────────────────────────────────
        // Node Inspector Panel (right panel)
        // ─────────────────────────────────────────────────────────────────────

        "QWidget#nodeInspector {"
        "  background: @bgPanel; border-left: 1px solid @border;"
        "}"

        // ─────────────────────────────────────────────────────────────────────
        // Node Inspector Panel labels
        // ─────────────────────────────────────────────────────────────────────

        "QLabel#nodeInstanceId   { color: @textMuted;  font-family: @font; font-size: @fontXs; }"
        "QLabel#nodeHelperText,"
        "QLabel#nodeNoParams     { color: @textSubtle; font-family: @font; font-size: @fontBase; }"
        "QLabel#nodeParamLabel   { color: @textSecond; font-family: @font; font-size: @fontSm; }"
        "QLabel#nodeStatusDetail { color: @textMuted;  font-family: @font; font-size: @fontSm; }"

        // Worker tab bar (bottom of NodeGraph canvas)
        "QWidget#workerTabBar { background: @bgPanel; }"
        "QWidget#workerTabBar QTabBar { background: transparent; }"
        "QWidget#workerTabBar QTabBar::tab {"
        "  background: transparent; color: @textMuted; padding: 0 16px;"
        "  height: 31px; border: none; font-size: @fontSm; font-weight: 500;"
        "}"
        "QWidget#workerTabBar QTabBar::tab:selected { color: @textPrimary; border-bottom: 2px solid @accent; }"
        "QWidget#workerTabBar QTabBar::tab:hover:!selected { color: @textSubtle; }"

    );
}

} // namespace dolphin::ui::detail
