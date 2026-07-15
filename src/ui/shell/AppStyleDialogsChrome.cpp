#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssDialogChrome()
{
    return QString(

        // ---------------------------------------------------------------------
        // App Settings Dialog — sidebar nav + page chrome
        // ---------------------------------------------------------------------

        "AppSettingsDialog { background: @bg; }"

        "QWidget#settingsSidebar {"
        "  background: @bgEl; border-right: 1px solid @border;"
        "}"
        // Idle is flat/transparent (no accent tint) — only hover/selected react,
        // matching the app's nav pattern (panelTab, sidebar lists). Accent fill is
        // reserved for the active page.
        "QToolButton#settingsNavBtn {"
        "  background: transparent; border: 1px solid transparent;"
        "  border-radius: 6px; color: @textSecond;"
        "  font-family: @font; font-size: @fontBase;"
        "  text-align: left; padding: 8px 10px 8px 12px;"
        "}"
        "QToolButton#settingsNavBtn:hover   { background: @overlayEl; color: @textPrimary; }"
        "QToolButton#settingsNavBtn:checked { background: rgba(@accentRgb,0.18); border-color: rgba(@accentRgb,0.40); color: @white; }"
        "QToolButton#settingsNavBtn:pressed { background: @overlayMut; }"
        "QToolButton#settingsNavBtn::menu-indicator { image: none; }"

        "QWidget#settingsPage { background: @bg; }"
        "QScrollArea#settingsPageScroll { background: @bg; border: none; }"
        "QScrollArea#settingsPageScroll > QWidget > QWidget { background: @bg; }"

        "QLabel#settingsPageTitle {"
        "  color: @textPrimary; font-family: @font; font-size: 18px; font-weight: 600;"
        "}"
        "QLabel#settingsSectionTitle {"
        "  color: @textMuted; font-family: @font; font-size: @fontXs; font-weight: 600;"
        "  letter-spacing: 0.2px; padding-top: 2px;"
        "}"
        "QFrame#settingsDivider {"
        "  color: @border; max-height: 1px; min-height: 1px;"
        "}"
        "QLabel#fieldHint {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; font-style: italic;"
        "  padding: 1px 0 2px 0;"
        "}"
        "QLabel#aboutCopy {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; line-height: 1.5;"
        "}"

        // ---------------------------------------------------------------------
        // QDialogButtonBox — themed button bar shared by all QDialog subclasses.
        // Secondary role (Cancel / Close / Discard).
        // ---------------------------------------------------------------------

        "QDialogButtonBox QPushButton {"
        "  background: @overlayEl; border: 1px solid @borderMenu;"
        "  border-radius: @radius3; color: @textSecond;"
        "  font-family: @font; font-size: @fontBase;"
        "  padding: 5px 16px; min-width: 72px;"
        "}"
        "QDialogButtonBox QPushButton:hover   { background: rgba(255,255,255,0.09); }"
        "QDialogButtonBox QPushButton:pressed { background: rgba(255,255,255,0.03); }"
        "QDialogButtonBox QPushButton:disabled {"
        "  background: rgba(255,255,255,0.03); border-color: @border; color: @textDisabled;"
        "}"
        // Primary / accept button — identified by object name set in each dialog.
        "QPushButton#dlgBtnOk, QPushButton#dlgBtnAccept {"
        "  background: @accent; border: none; border-radius: @radius3;"
        "  color: @white; font-family: @font; font-size: @fontBase; font-weight: 600;"
        "  padding: 5px 20px; min-width: 80px;"
        "}"
        "QPushButton#dlgBtnOk:hover,   QPushButton#dlgBtnAccept:hover   { background: @accentHover; }"
        "QPushButton#dlgBtnOk:pressed, QPushButton#dlgBtnAccept:pressed { background: @accentPress; }"
        "QPushButton#dlgBtnOk:disabled, QPushButton#dlgBtnAccept:disabled {"
        "  background: rgba(@accentRgb,0.30); color: rgba(255,255,255,0.35);"
        "}"

        // ---------------------------------------------------------------------
        // Dialog tab widget  (QTabWidget#dlgTabs — import wizard and any future
        // dialog that needs an in-body tab strip)
        // ---------------------------------------------------------------------

        "QTabWidget#dlgTabs::pane {"
        "  border: none; border-top: 1px solid @border; background: transparent;"
        "}"
        "QTabWidget#dlgTabs > QTabBar { background: transparent; }"
        "QTabWidget#dlgTabs > QTabBar::tab {"
        "  background: transparent; border: none;"
        "  border-bottom: 2px solid transparent;"
        "  color: @textMuted; font-family: @font; font-size: @fontSm;"
        "  padding: 6px 16px 5px 16px; margin-right: 2px;"
        "}"
        "QTabWidget#dlgTabs > QTabBar::tab:selected {"
        "  color: @textPrimary; border-bottom: 2px solid @accent;"
        "}"
        "QTabWidget#dlgTabs > QTabBar::tab:hover:!selected {"
        "  color: @textSecond; border-bottom: 2px solid @border;"
        "}"
        "QTabWidget#dlgTabs > QTabBar::tab:disabled {"
        "  color: @textDisabled;"
        "}"

    );
}

} // namespace dolphin::ui::detail
