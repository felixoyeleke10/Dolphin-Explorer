#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssDialogContacts()
{
    return QString(

        // ---------------------------------------------------------------------
        // Contact list table
        // ---------------------------------------------------------------------

        "QTableWidget#contactTable {"
        "  background: @bgPanel; border: none;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  gridline-color: @border;"
        "}"
        "QTableWidget#contactTable::item:selected {"
        "  background: rgba(@accentRgb,0.3);"
        "}"
        "QTableWidget#contactTable QHeaderView::section {"
        "  background: @bgEl; color: @textMuted;"
        "  font-family: @font; font-size: @fontXs; font-weight: 600;"
        "  padding: 4px 8px; border: none; border-bottom: 1px solid @border;"
        "}"

        // ---------------------------------------------------------------------
        // Contact Manager window (Explorer-style: command bar / nav / preview)
        // ---------------------------------------------------------------------

        "QMainWindow#contactManagerWindow { background: @bg; }"
        "QMainWindow#contactManagerWindow::separator { width: 0; height: 0; }"

        // Command bar
        "QToolBar#contactCmdBar {"
        "  background: @bgEl; border: none; border-bottom: 1px solid @border;"
        "  spacing: 2px; padding: 3px 6px;"
        "}"
        "QToolBar#contactCmdBar::separator {"
        "  background: @border; width: 1px; margin: 4px 6px;"
        "}"
        "QToolBar#contactCmdBar QToolButton {"
        "  background: transparent; border: none; border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase;"
        "  padding: 4px 9px; margin: 0;"
        "}"
        "QToolBar#contactCmdBar QToolButton:hover    { background: @overlayHov; }"
        "QToolBar#contactCmdBar QToolButton:pressed  { background: rgba(255,255,255,0.04); }"
        "QToolBar#contactCmdBar QToolButton:disabled { color: @textDisabled; }"
        "QToolBar#contactCmdBar QToolButton::menu-indicator { image: none; }"

        // Panels
        "QWidget#contactCentre  { background: @bg; }"
        "QTreeWidget#contactNavTree {"
        "  background: @bgPanel; border: none; border-right: 1px solid @border;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm; padding: 6px 4px;"
        "}"
        "QTreeWidget#contactNavTree::item { padding: 5px 6px; border-radius: @radius2; }"
        "QTreeWidget#contactNavTree::item:hover    { background: @overlayMut; }"
        "QTreeWidget#contactNavTree::item:selected { background: rgba(@accentRgb,0.22); color: @textPrimary; }"

        // Navigation bar (back/forward/up/refresh + folder-link bar + search)
        "QToolBar#contactNavBar {"
        "  background: @bgEl; border: none; border-bottom: 1px solid @border;"
        "  spacing: 2px; padding: 4px 6px;"
        "}"
        "QToolBar#contactNavBar::separator { background: @border; width: 1px; margin: 4px 5px; }"
        "QToolBar#contactNavBar QToolButton {"
        "  background: transparent; border: none; border-radius: @radius2;"
        "  color: @textSecond; font-size: 15px; padding: 3px 9px; margin: 0;"
        "}"
        "QToolBar#contactNavBar QToolButton:hover    { background: @overlayHov; }"
        "QToolBar#contactNavBar QToolButton:pressed  { background: rgba(255,255,255,0.04); }"
        "QToolBar#contactNavBar QToolButton:disabled { color: @textDisabled; }"

        // Folder-link (breadcrumb) bar — looks like Explorer's address field.
        "QLabel#contactBreadcrumb {"
        "  background: @bgPanel; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @textSubtle; font-family: @font; font-size: @fontBase;"
        "  padding: 4px 10px; margin: 0 4px;"
        "}"
        "QLabel#contactBreadcrumb a { color: @accentSoft; text-decoration: none; }"

        // Search box
        "QLineEdit#contactSearch {"
        "  background: @overlayMut; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm; padding: 3px 8px;"
        "}"
        "QLineEdit#contactSearch:focus { border-color: rgba(@accentRgb,0.55); background: @overlayHov; }"

        // Preview / details pane
        "QWidget#contactPreview { background: @bgPanel; border-left: 1px solid @border; }"
        "QLabel#contactPreviewTitle { color: @textPrimary; font-family: @font; font-size: 15px; font-weight: 700; }"
        "QLabel#contactPreviewEmpty { color: @textMuted; font-family: @font; font-size: @fontSm; }"
        "QFrame#contactPreviewSep   { color: @border; max-height: 1px; background: @border; border: none; }"
        "QLabel#contactPreviewImage { background: @bgEl; border: 1px solid @border; border-radius: 6px; }"
        "QLabel#contactKey  { color: @textMuted; font-family: @font; font-size: @fontXs; font-weight: 600; letter-spacing: 0.4px; }"
        "QLabel#contactVal  { color: @textSecond; font-family: @font; font-size: @fontSm; }"
        "QLabel#contactMeta { color: @textSubtle; font-family: @font; font-size: @fontXs; }"

        // Thumbnails (icon) view
        "QListWidget#contactThumbs {"
        "  background: @bg; border: none; color: @textSecond;"
        "  font-family: @font; font-size: @fontXs;"
        "}"
        "QListWidget#contactThumbs::item { padding: 4px; border-radius: @radius2; color: @textSecond; }"
        "QListWidget#contactThumbs::item:hover    { background: @overlayMut; }"
        "QListWidget#contactThumbs::item:selected { background: rgba(@accentRgb,0.22); color: @textPrimary; }"

        // Export Manager window (Contact-Manager-style 3-pane layout)
        "QMainWindow#exportManagerWindow { background: @bg; }"
        "QMainWindow#exportManagerWindow::separator { width: 0; height: 0; }"
        "QTreeWidget#exportNavTree {"
        "  background: @bgPanel; border: none; border-right: 1px solid @border;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm; padding: 6px 4px;"
        "}"
        "QTreeWidget#exportNavTree::item { padding: 6px 6px; border-radius: @radius2; }"
        "QTreeWidget#exportNavTree::item:hover    { background: @overlayMut; }"
        "QTreeWidget#exportNavTree::item:selected { background: rgba(@accentRgb,0.22); color: @textPrimary; }"
        "QWidget#exportCentre  { background: @bg; }"
        "QLabel#exportTitle    { color: @textPrimary; font-family: @font; font-size: 18px; font-weight: 700; }"
        "QLabel#exportDesc     { color: @textSubtle; font-family: @font; font-size: @fontSm; }"
        "QLabel#exportFieldLabel {"
        "  color: @textMuted; font-family: @font; font-size: @fontXs; font-weight: 600; letter-spacing: 0.5px;"
        "}"
        "QPushButton#exportFmtBtn {"
        "  background: @overlayMut; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase; padding: 5px 16px;"
        "}"
        "QPushButton#exportFmtBtn:hover   { background: @overlayHov; border-color: rgba(@accentRgb,0.4); }"
        "QPushButton#exportFmtBtn:checked { background: rgba(@accentRgb,0.18); border-color: rgba(@accentRgb,0.55); color: @white; }"
        "QWidget#exportPreview { background: @bgPanel; border-left: 1px solid @border; }"
        "QLabel#exportPreviewTitle  { color: @textPrimary; font-family: @font; font-size: @fontMd; font-weight: 700; }"
        "QLabel#exportPreviewDetail { color: @textSubtle; font-family: @font; font-size: @fontSm; }"
        "QMainWindow#exportManagerWindow QStatusBar { background: @bgEl; border-top: 1px solid @border; }"
        "QLabel#exportStatus { color: @textMuted; font-family: @font; font-size: @fontXs; padding: 0 6px; }"

        // Status bar + view-mode toggle buttons
        "QMainWindow#contactManagerWindow QStatusBar { background: @bgEl; border-top: 1px solid @border; }"
        "QMainWindow#contactManagerWindow QStatusBar::item { border: none; }"
        "QLabel#contactStatus { color: @textMuted; font-family: @font; font-size: @fontXs; padding: 0 6px; }"
        "QToolButton#contactViewBtn {"
        "  background: transparent; border: none; border-radius: @radius1;"
        "  color: @textMuted; font-size: 14px; padding: 1px 6px; margin: 0 1px;"
        "}"
        "QToolButton#contactViewBtn:hover   { background: @overlayHov; color: @textSecond; }"
        "QToolButton#contactViewBtn:checked { background: rgba(@accentRgb,0.22); color: @textPrimary; }"

        // Geodetic settings inline dialog labels
        "QLabel#geoDetectedLabel, QLabel#geoNoteLabel {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm;"
        "}"

        // ---------------------------------------------------------------------
        // Geodesy panel
        // ---------------------------------------------------------------------

        "QLabel#geoAutoDetect { color: @textSubtle; font-family: @font; font-size: @fontSm; }"
        "QLabel#geoLayerRow   { color: @textSecond; font-family: @font; font-size: @fontSm; }"
        "QLabel#geoLayerDot[state=\"ok\"]        { color: @success; }"
        "QLabel#geoLayerDot[state=\"confirmed\"] { color: @accentSoft; }"
        "QLabel#geoLayerDot[state=\"warning\"]   { color: @caution; }"

        "QWidget#panelBody { background: @bgPanel; }"

        // ---------------------------------------------------------------------
        // Contact Editor — "Edit contact details" (ce* namespace)
        // ---------------------------------------------------------------------

        "QDialog#contactEditor { background: @bg; }"

        "QLabel#ceFieldLabel { color: @textSubtle; font-family: @font; font-size: @fontSm; }"
        "QLabel#ceEcho      { color: @textMuted;  font-family: @font; font-size: @fontSm; }"
        "QLabel#ceFooter    { color: @textMuted;  font-family: @font; font-size: @fontSm; }"
        "QLabel#ceSection {"
        "  color: @textMuted; font-family: @font; font-size: @fontXs; font-weight: 600;"
        "  letter-spacing: 0.5px; padding-top: 4px;"
        "}"
        "QFrame#ceDivider { background: @border; max-height: 1px; min-height: 1px; border: none; }"

        // Text / numeric / combo fields — one shared card treatment.
        "QLineEdit#ceField, QDoubleSpinBox#ceSpin, QComboBox#ceCombo, QPlainTextEdit#ceText {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 5px;"
        "  color: @textPrimary; font-family: @font; font-size: @fontSm;"
        "  padding: 2px 6px; min-height: 20px;"
        "  selection-background-color: rgba(@accentRgb,0.4);"
        "}"
        "QLineEdit#ceField:focus, QDoubleSpinBox#ceSpin:focus,"
        "QComboBox#ceCombo:focus, QPlainTextEdit#ceText:focus {"
        "  border-color: rgba(@accentRgb,0.55);"
        "}"
        "QLineEdit#ceField:disabled, QDoubleSpinBox#ceSpin:disabled {"
        "  color: @textDisabled; background: rgba(255,255,255,0.02);"
        "}"

        // Keep the value clear of the stepper column.
        "QDoubleSpinBox#ceSpin { padding-right: 18px; }"
        "QDoubleSpinBox#ceSpin::up-button {"
        "  subcontrol-origin: border; subcontrol-position: top right;"
        "  width: 16px; border: none; background: transparent; margin: 1px 1px 0 0;"
        "}"
        "QDoubleSpinBox#ceSpin::down-button {"
        "  subcontrol-origin: border; subcontrol-position: bottom right;"
        "  width: 16px; border: none; background: transparent; margin: 0 1px 1px 0;"
        "}"
        "QDoubleSpinBox#ceSpin::up-button:hover, QDoubleSpinBox#ceSpin::down-button:hover {"
        "  background: @overlayHov; border-radius: 2px;"
        "}"
        "QDoubleSpinBox#ceSpin::up-arrow   { image: url(:/icons/spin_up.svg);   width: 7px; height: 5px; }"
        "QDoubleSpinBox#ceSpin::down-arrow { image: url(:/icons/spin_down.svg); width: 7px; height: 5px; }"

        "QComboBox#ceCombo::drop-down {"
        "  border: none; width: 18px; subcontrol-origin: padding; subcontrol-position: right center;"
        "}"
        "QComboBox#ceCombo::down-arrow { image: url(:/icons/spin_down.svg); width: 8px; height: 6px; }"
        "QComboBox#ceCombo QAbstractItemView {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 8px;"
        "  color: @textPrimary; selection-background-color: rgba(@accentRgb,0.3);"
        "  padding: 4px 0; font-size: @fontSm;"
        "}"

        "QListWidget#ceTags {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 5px;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm; padding: 2px;"
        "}"
        "QListWidget#ceTags::item { padding: 1px 6px; border-radius: @radius1; }"
        "QListWidget#ceTags::item:hover    { background: @overlayMut; }"
        "QListWidget#ceTags::item:selected { background: rgba(@accentRgb,0.22); color: @textPrimary; }"

        // Small square helper buttons (add tag / clear tags) + colour swatch.
        "QToolButton#ceMiniBtn {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 5px;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  min-width: 22px; min-height: 20px;"
        "}"
        "QToolButton#ceMiniBtn:hover   { border-color: rgba(255,255,255,0.25); color: @textPrimary; }"
        "QToolButton#ceMiniBtn:pressed { background: @overlayMut; }"
        "QPushButton#ceColorBtn {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 5px;"
        "  color: @textSecond; font-family: @font; font-size: @fontXs; padding: 1px 6px;"
        "}"
        "QPushButton#ceColorBtn:hover { border-color: rgba(255,255,255,0.25); }"

        // Image pane: framed viewer card.
        "QFrame#ceImageFrame {"
        "  background: #121214; border: 1px solid @border; border-radius: @radius3;"
        "}"

        // Command row: Prev/Next nav + danger delete; Close reuses #dlgBtnOk.
        "QToolButton#ceNavBtn {"
        "  background: @overlayEl; border: 1px solid @borderMenu; border-radius: @radius3;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase; padding: 4px 14px;"
        "}"
        "QToolButton#ceNavBtn:hover    { border-color: rgba(255,255,255,0.25); color: @textPrimary; }"
        "QToolButton#ceNavBtn:pressed  { background: @overlayMut; }"
        "QToolButton#ceNavBtn:disabled { color: @textDisabled; border-color: @border; }"
        "QLabel#ceNavTitle { color: @textSecond; font-family: @font; font-size: @fontSm; }"
        "QPushButton#ceDeleteBtn {"
        "  background: transparent; border: 1px solid rgba(@dangerRgb,0.45); border-radius: @radius3;"
        "  color: @dangerBright; font-family: @font; font-size: @fontBase; padding: 4px 16px;"
        "}"
        "QPushButton#ceDeleteBtn:hover   { background: rgba(@dangerRgb,0.14); border-color: rgba(@dangerRgb,0.70); }"
        "QPushButton#ceDeleteBtn:pressed { background: rgba(@dangerRgb,0.08); }"
        "QPushButton#ceExportBtn {"
        "  background: @overlayEl; border: 1px solid @borderMenu; border-radius: @radius3;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm; padding: 3px 12px;"
        "}"
        "QPushButton#ceExportBtn:hover   { border-color: rgba(255,255,255,0.25); color: @textPrimary; }"
        "QPushButton#ceExportBtn:pressed { background: @overlayMut; }"

    );
}

} // namespace dolphin::ui::detail
