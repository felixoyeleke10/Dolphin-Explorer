#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssDialogs()
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

        // ---------------------------------------------------------------------
        // Import Dialog
        // ---------------------------------------------------------------------

        "ImportDialog { background: @bg; }"

        "#dlgHeader   { background: @bgEl; }"
        "#dlgTitle    { color: @textPrimary; font-family: @font; font-size: @fontLg; font-weight: 600; }"
        "#dlgSubtitle { color: @textMuted; font-family: @font; font-size: @fontSm; }"
        "#dlgDivider  { background: @border; }"
        "#dlgBody     { background: @bg; }"

        "#dlgSectionLabel {"
        "  color: @textMuted; font-family: @font; font-size: @fontXs;"
        "  font-weight: 600; letter-spacing: 0.2px;"
        "}"
        "#dlgSection {"
        "  background: @bgEl; border: 1px solid @border; border-radius: 8px;"
        "}"

        // -- Shared semantic labels (import wizard, metadata windows, etc.) -----
        "QLabel#dlgLabelMeta {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm;"
        "}"
        "QLabel#dlgLabelDanger  { color: @danger;  font-family: @font; font-size: @fontSm; }"
        "QLabel#dlgLabelCaution { color: @caution; font-family: @font; font-size: @fontSm; }"
        "QLabel#dlgLabelMono {"
        "  font-family: monospace; font-size: @fontSm; color: @textSoft;"
        "}"
        "QLabel#dlgLabelMonoSmall {"
        "  font-family: monospace; font-size: @fontXs; color: @textSubtle;"
        "}"
        // Per-file status badge in the import wizard — state drives colour.
        "QLabel#importFileStatus { color: @textMuted; font-family: @font; font-size: @fontSm; }"
        "QLabel#importFileStatus[state=\"ok\"]      { color: @success; }"
        "QLabel#importFileStatus[state=\"caution\"] { color: @caution; }"
        "QLabel#importFileStatus[state=\"error\"]   { color: @danger;  }"

        // ---------------------------------------------------------------------
        // Metadata windows (SBP + SSS)
        // ---------------------------------------------------------------------

        "QWidget#metaToolbar {"
        "  background: @bgEl; border-bottom: 1px solid @border;"
        "}"
        "QWidget#metaToolbar QToolButton {"
        "  background: transparent; border: none; border-radius: @radius2;"
        "  padding: @padSm; color: @textSecond;"
        "}"
        "QWidget#metaToolbar QToolButton:hover    { background: @overlayHov; }"
        "QWidget#metaToolbar QToolButton:pressed  { background: rgba(255,255,255,0.12); }"
        "QWidget#metaToolbar QToolButton::menu-indicator { image: none; }"

        "QFrame#metaSep { color: @border; }"

        "QLabel#metaLoadStatus { color: @textMuted; font-family: @font; font-size: @fontSm; }"

        "QLabel#metaSelStatus {"
        "  background: @bg; color: @textSubtle; padding: 0 6px; font-size: @fontSm;"
        "}"

        "QToolButton#metaUndockBtn {"
        "  background: transparent; border: none; color: @textSubtle; font-size: @fontXs;"
        "}"
        "QToolButton#metaUndockBtn:hover {"
        "  color: @textSoft; background: @overlayHov; border-radius: @radius1;"
        "}"

        "#dlgRadio            { color: @textSecond; }"
        "#dlgRadioLabel       { color: @textSecond; font-family: @font; font-size: @fontBase; }"
        "#dlgRadioLabelDim    { color: @textDisabled; font-family: @font; font-size: @fontBase; }"
        "#dlgProjectName      { color: @accent; font-family: @font; font-size: @fontBase; font-weight: 600; }"
        "#dlgNewProjForm      { background: transparent; }"
        "#dlgFieldLabel       { color: @textSubtle; font-family: @font; font-size: @fontSm; }"
        "#dlgFolderPath       { color: @textMuted; font-family: @font; font-size: @fontXs; }"

        // Hint/helper text inside dialogs — italic, muted
        "QLabel#dlgHint {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; font-style: italic;"
        "  padding: 2px 2px 4px 2px;"
        "}"

        "#dlgLineEdit {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 5px;"
        "  color: @textPrimary; font-family: @font; font-size: @fontBase;"
        "  padding: 3px 8px; selection-background-color: rgba(@accentRgb,0.4);"
        "}"
        "#dlgLineEdit:focus { border-color: @accent; }"

        "#dlgFileTable {"
        "  background: @bgPanel; border: 1px solid @border; border-radius: @radius3;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  gridline-color: @bg;"
        "  selection-background-color: rgba(@accentRgb,0.18);"
        "}"
        "#dlgTableHdr {"
        "  background: @bgEl; color: @textMuted; font-family: @font;"
        "  font-size: @fontXxs; font-weight: 700; letter-spacing: 0.5px;"
        "  border: none; border-bottom: 1px solid @border;"
        "}"
        "#dlgFileTable QHeaderView::section {"
        "  background: @bgEl; color: @textMuted; font-family: @font;"
        "  font-size: @fontXxs; font-weight: 700; letter-spacing: 0.5px;"
        "  padding: 4px 8px; border: none; border-right: 1px solid @border;"
        "}"

        "#dlgActionCombo {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  padding: 2px 20px 2px 6px; text-align: left;"
        "}"
        "#dlgActionCombo::drop-down { border: none; width: 20px; subcontrol-origin: padding; subcontrol-position: right center; }"
        "#dlgActionCombo QAbstractItemView {"
        "  background: @bgCard; border: 1px solid @borderMenu;"
        "  color: @textPrimary; selection-background-color: @accent;"
        "}"

        "#dlgFooter  { background: @bgEl; border-top: 1px solid @border; }"
        "#dlgSummary { color: @textMuted; font-family: @font; font-size: @fontSm; }"

        "#dlgProgressStatus {"
        "  color: @textSubtle; font-family: @font; font-size: @fontMd;"
        "}"

        "#dlgProgressBar {"
        "  border: none; border-radius: 2px;"
        "  background: @overlayHov; max-height: 4px; min-height: 4px;"
        "}"
        "#dlgProgressBar::chunk {"
        "  border-radius: 2px;"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 @accent, stop:1 @accentSoft);"
        "}"

        "#dlgBtnPrimary {"
        "  background: @accent; border: none; border-radius: @radius3;"
        "  color: @white; font-family: @font; font-size: @fontBase; font-weight: 600; padding: 0 20px;"
        "}"
        "#dlgBtnPrimary:hover    { background: @accentHover; }"
        "#dlgBtnPrimary:pressed  { background: @accentPress; }"
        "#dlgBtnPrimary:disabled { background: @bgCard; color: @textDisabled; }"

        "#dlgBtnSecondary {"
        "  background: @overlayEl; border: 1px solid @borderMenu; border-radius: @radius3;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase; padding: 0 16px;"
        "}"
        "#dlgBtnSecondary:hover    { background: @overlayHov; }"
        "#dlgBtnSecondary:pressed  { background: rgba(255,255,255,0.03); }"
        "#dlgBtnSecondary:disabled { background: rgba(255,255,255,0.03); border-color: @border; color: @textDisabled; }"

        "#dlgBtnBrowse {"
        "  background: @overlayEl; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @textSubtle; font-family: @font; font-size: @fontXs; padding: 2px 10px;"
        "}"
        "#dlgBtnBrowse:hover    { background: @overlayHov; }"
        "#dlgBtnBrowse:pressed  { background: rgba(255,255,255,0.03); }"
        "#dlgBtnBrowse:disabled { background: transparent; border-color: @border; color: @textDisabled; }"

        "#dlgBtnAdd {"
        "  background: transparent; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @accent; font-family: @font; font-size: @fontXs; font-weight: 600; padding: 2px 10px;"
        "}"
        "#dlgBtnAdd:hover    { background: rgba(@accentRgb,0.10); }"
        "#dlgBtnAdd:pressed  { background: rgba(@accentRgb,0.05); }"
        "#dlgBtnAdd:disabled { border-color: @border; color: @textDisabled; }"

        // ---------------------------------------------------------------------
        // Import panel action buttons
        // ---------------------------------------------------------------------

        "QPushButton#importActionBtn {"
        "  background: @overlayMut; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase;"
        "  padding: 6px 12px; text-align: left; min-height: 48px;"
        "}"
        "QPushButton#importActionBtn:hover    { background: @overlayHov; border-color: rgba(@accentRgb,0.4); }"
        "QPushButton#importActionBtn:pressed  { background: rgba(@accentRgb,0.15); }"
        "QPushButton#importActionBtn:disabled { background: rgba(255,255,255,0.02); border-color: @border; color: @textDisabled; }"

        // Sensor-type selector cards in ImportSetupDialog (QFrame used, not QPushButton)
        "QFrame#importActionBtn {"
        "  background: @overlayMut; border: 1px solid @borderMenu; border-radius: @radius2;"
        "}"
        "QFrame#importActionBtn:hover { background: @overlayHov; border-color: rgba(@accentRgb,0.4); }"
        "QFrame#importActionBtn[selected=\"true\"] {"
        "  background: rgba(@accentRgb,0.10); border-color: rgba(@accentRgb,0.45);"
        "}"

        "QPushButton#importPrimaryBtn {"
        "  background: rgba(@accentRgb,0.10); border: 1px solid rgba(@accentRgb,0.35); border-radius: @radius2;"
        "  color: @accentSoft; font-family: @font; font-size: @fontBase;"
        "  padding: 6px 12px; text-align: left; min-height: 48px;"
        "}"
        "QPushButton#importPrimaryBtn:hover    { background: rgba(@accentRgb,0.18); border-color: rgba(@accentRgb,0.5); }"
        "QPushButton#importPrimaryBtn:pressed  { background: rgba(@accentRgb,0.08); }"
        "QPushButton#importPrimaryBtn:disabled { background: rgba(@accentRgb,0.04); border-color: rgba(@accentRgb,0.15); color: @textDisabled; }"

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

        // ---------------------------------------------------------------------
        // Import / Correction Dialogs (shared patterns)
        // ---------------------------------------------------------------------

        "SidescanCorrectionDialog { background: @bg; }"
        "QDialog#geodesyWin { background: @bg; }"

        // QCheckBox#dlgCheckBox — legacy alias; global QCheckBox style in AppStyleBase now covers all checkboxes.

        // ---------------------------------------------------------------------
        // Execution Progress Dialog
        // ---------------------------------------------------------------------

        "ExecutionProgressDialog { background: @bgEl; }"
        "QWidget#epdHeader { background: @bgEl; border-bottom: 1px solid @border; }"
        "QWidget#epdListBody { background: @bgEl; }"
        "QWidget#epdFooter  { background: @bgPanel; border-top: 1px solid @border; }"

        "ExecutionProgressDialog QLabel#titleLabel {"
        "  color: @textPrimary; font-family: @font; font-size: 15px; font-weight: 600;"
        "  background: transparent;"
        "}"
        "ExecutionProgressDialog QLabel#subtitleLabel {"
        "  color: @textSubtle; font-family: @font; font-size: @fontBase; background: transparent;"
        "}"

        "QProgressBar#overallBar {"
        "  border: none; border-radius: @radius1;"
        "  background: rgba(255,255,255,0.10); max-height: 6px; min-height: 6px;"
        "}"
        "QProgressBar#overallBar::chunk {"
        "  border-radius: @radius1;"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 @accent, stop:1 @accentSoft);"
        "}"

        "QFrame#fileCard {"
        "  background: transparent; border: none; border-bottom: 1px solid @border;"
        "}"
        "QFrame#fileCard:hover { background: @overlayHov; }"
        "QFrame#fileCard > QWidget { background: transparent; }"
        "QLabel#rowStatusIcon { background: transparent; }"

        "QLabel#formatBadge {"
        "  background: rgba(@accentRgb,0.12); border: 1px solid rgba(@accentRgb,0.30);"
        "  border-radius: @radius3; color: @accentSoft;"
        "  font-family: @font; font-size: @fontSm; font-weight: 700;"
        "  min-width: @badgeSize; max-width: @badgeSize; min-height: @badgeSize; max-height: @badgeSize;"
        "}"
        "QLabel#formatBadge[state=\"done\"] {"
        "  background: rgba(@successRgb,0.12); border-color: rgba(@successRgb,0.30);"
        "  color: @success;"
        "}"
        "QLabel#formatBadge[state=\"failed\"] {"
        "  background: rgba(@dangerRgb,0.12); border-color: rgba(@dangerRgb,0.30);"
        "  color: @danger;"
        "}"

        "ExecutionProgressDialog QLabel#fileName {"
        "  color: @textPrimary; font-family: @font; font-size: @fontBase; font-weight: 600;"
        "  background: transparent;"
        "}"
        "ExecutionProgressDialog QLabel#fileMeta {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; background: transparent;"
        "}"
        "ExecutionProgressDialog QLabel#fileStatus {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; background: transparent;"
        "}"
        "ExecutionProgressDialog QLabel#fileResult {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; background: transparent;"
        "}"
        "ExecutionProgressDialog QLabel#fileResult[state=\"done\"]   { color: @success; }"
        "ExecutionProgressDialog QLabel#fileResult[state=\"failed\"] { color: @danger; }"

        "QProgressBar#fileBar {"
        "  border: none; border-radius: 2px;"
        "  background: @overlayHov; max-height: 5px; min-height: 5px;"
        "}"
        "QProgressBar#fileBar::chunk {"
        "  border-radius: 2px;"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 @accent, stop:1 @accentSoft);"
        "}"

        "ExecutionProgressDialog QLabel#elapsedLabel {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm; background: transparent;"
        "}"

        "QPushButton#bgBtn {"
        "  background: transparent; border: 1px solid @borderMenu; border-radius: @radius3;"
        "  color: @textSoft; font-family: @font; font-size: @fontBase;"
        "  padding: 5px 14px; min-width: 130px;"
        "}"
        "QPushButton#bgBtn:hover    { background: @overlayEl; color: @textPrimary; border-color: @borderMenu; }"
        "QPushButton#bgBtn:pressed  { background: rgba(255,255,255,0.03); }"
        "QPushButton#bgBtn:disabled { color: @textDisabled; border-color: @border; }"

        "QPushButton#closeBtn {"
        "  background: @accent; border: none; border-radius: @radius3;"
        "  color: @white; font-family: @font; font-size: @fontBase; font-weight: 600;"
        "  padding: 5px 20px; min-width: 80px;"
        "}"
        "QPushButton#closeBtn:hover    { background: @accentHover; }"
        "QPushButton#closeBtn:pressed  { background: @accentPress; }"
        "QPushButton#closeBtn:disabled { background: rgba(@accentRgb,0.30); color: rgba(255,255,255,0.35); }"

        // Command Palette Dialog
        "CommandPaletteDialog {"
        "  background: @bg;"
        "}"
        "#cpCard, #cpInputRow { background: transparent; }"
        "QLineEdit#cpInput {"
        "  background: transparent; border: none;"
        "  color: @textSecond; font-size: @fontMd; font-family: @font;"
        "  selection-background-color: rgba(@accentRgb,0.45);"
        "}"
        "QLineEdit#cpInput:focus { border: none; outline: none; }"
        "#cpSep { background: rgba(255,255,255,0.09); }"
        "#cpList { background: transparent; outline: 0; border: none; }"
        "#cpList::item { background: transparent; border: none; }"
        "#cpList::item:selected { background: transparent; border: none; }"

        // Note/hint labels inside dialogs (below form fields, muted italic)
        "QLabel#dlgNote {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; font-style: italic;"
        "  padding: 4px 12px;"
        "}"

        // CRS required warning label in ImportDialog
        "QLabel#dlgCrsRequired {"
        "  color: @caution; font-family: @font; font-size: @fontSm;"
        "}"

        // ---------------------------------------------------------------------
        // New Project Dialog
        // ---------------------------------------------------------------------

        "QLabel#newProjCrsLabel {"
        "  color: @textSubtle; font-family: @font; font-size: @fontSm; font-style: italic;"
        "}"
        "QFrame#newProjPreviewBox {"
        "  background: @bgPanel; border: 1px solid @border; border-radius: @radius2;"
        "}"
        "QLabel#newProjPreviewPath {"
        "  color: @textSoft; font-family: monospace; font-size: @fontSm;"
        "}"

        // ---------------------------------------------------------------------
        // Processing Modal Dialog (frameless, drop-shadow overlay)
        // ---------------------------------------------------------------------

        "QFrame#dlgProcessingFrame {"
        "  background-color: @bgPanel; border: 1px solid @border; border-radius: 10px;"
        "}"
        "QWidget#procHeader {"
        "  background: transparent; border-bottom: 1px solid @border;"
        "}"
        "QLabel#procSpinner {"
        "  color: @accent; font-size: 20px; background: transparent; border: none;"
        "}"
        "QLabel#procTitle {"
        "  color: @textSecond; font-family: @font; font-size: @fontBase; font-weight: 600;"
        "  letter-spacing: 1px; background: transparent; border: none;"
        "}"
        "QPushButton#procCloseBtn {"
        "  color: @textMuted; font-size: @fontBase; background: transparent;"
        "  border: none; border-radius: @radius2;"
        "}"
        "QPushButton#procCloseBtn:hover { color: @white; background: @dangerBright; }"
        "QProgressBar#procBar { background: @bgPanel; border: none; }"
        "QProgressBar#procBar::chunk {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 @accentPress, stop:1 @accent);"
        "}"
        "QLabel#procStatus {"
        "  color: @textPrimary; font-family: @font; font-size: @fontMd; font-weight: 600;"
        "  background: transparent;"
        "}"
        "QTextEdit#procLog {"
        "  background-color: @bg; color: @textSecond;"
        "  border: 1px solid @border; border-radius: @radius3; padding: 8px;"
        "}"
        "QTextEdit#procLog QScrollBar:vertical {"
        "  background: @bg; width: 5px; border: none; margin: 0;"
        "}"
        "QTextEdit#procLog QScrollBar::handle:vertical {"
        "  background: rgba(255,255,255,0.20); border-radius: 2px; min-height: 20px;"
        "}"
        "QTextEdit#procLog QScrollBar::handle:vertical:hover { background: @accent; }"
        "QTextEdit#procLog QScrollBar::add-line:vertical,"
        "QTextEdit#procLog QScrollBar::sub-line:vertical { height: 0; border: none; }"
        "QPushButton#procCancel {"
        "  color: @textMuted; font-family: @font; font-size: @fontBase; font-weight: 500;"
        "  background: transparent; border: 1px solid @border; border-radius: @radius2; padding: 0 20px;"
        "}"
        "QPushButton#procCancel:hover { background: @overlayHov; border-color: @textDisabled; color: @textPrimary; }"
        "QPushButton#procCancel:pressed  { background: @overlayMut; }"
        "QPushButton#procCancel:disabled { color: @textDisabled; border-color: @bgPanel; }"

    );
}

} // namespace dolphin::ui::detail
