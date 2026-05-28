#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssDialogs()
{
    return QString(

        // ─────────────────────────────────────────────────────────────────────
        // App Settings Dialog — sidebar nav + page chrome
        // ─────────────────────────────────────────────────────────────────────

        "AppSettingsDialog { background: @bg; }"

        "QWidget#settingsSidebar {"
        "  background: @bgEl; border-right: 1px solid @border;"
        "}"
        "QListWidget#settingsNav {"
        "  background: transparent; border: none; outline: none;"
        "  font-family: @font; font-size: @fontBase; padding: 4px 0;"
        "}"
        "QListWidget#settingsNav::item {"
        "  color: @textSubtle; padding: 8px 16px 8px 18px;"
        "  border-left: 2px solid transparent;"
        "}"
        "QListWidget#settingsNav::item:selected {"
        "  background: rgba(@accentRgb,0.10); color: @textPrimary;"
        "  border-left: 2px solid @accent;"
        "}"
        "QListWidget#settingsNav::item:hover:!selected {"
        "  background: rgba(255,255,255,0.04); color: @textSecond;"
        "}"

        "QWidget#settingsPage { background: @bg; }"
        "QScrollArea#settingsPageScroll { background: @bg; border: none; }"
        "QScrollArea#settingsPageScroll > QWidget > QWidget { background: @bg; }"

        "QLabel#settingsPageTitle {"
        "  color: @textPrimary; font-family: @font; font-size: 18px; font-weight: 600;"
        "}"
        "QLabel#settingsSectionTitle {"
        "  color: @textSecond; font-family: @font; font-size: @fontSm; font-weight: 600;"
        "  padding-top: 2px;"
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

        // ─────────────────────────────────────────────────────────────────────
        // QDialogButtonBox — themed button bar shared by all QDialog subclasses.
        // Secondary role (Cancel / Close / Discard).
        // ─────────────────────────────────────────────────────────────────────

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

        // ─────────────────────────────────────────────────────────────────────
        // Dialog tab widget  (QTabWidget#dlgTabs — import wizard and any future
        // dialog that needs an in-body tab strip)
        // ─────────────────────────────────────────────────────────────────────

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

        // ─────────────────────────────────────────────────────────────────────
        // Import Dialog
        // ─────────────────────────────────────────────────────────────────────

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

        // ── Shared semantic labels (import wizard, metadata windows, etc.) ─────
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

        // ─────────────────────────────────────────────────────────────────────
        // Metadata windows (SBP + SSS)
        // ─────────────────────────────────────────────────────────────────────

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

        // ─────────────────────────────────────────────────────────────────────
        // Import panel action buttons
        // ─────────────────────────────────────────────────────────────────────

        "QPushButton#importActionBtn {"
        "  background: @overlayMut; border: 1px solid @borderMenu; border-radius: @radius2;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase;"
        "  padding: 6px 12px; text-align: left; min-height: 48px;"
        "}"
        "QPushButton#importActionBtn:hover    { background: @overlayHov; border-color: rgba(@accentRgb,0.4); }"
        "QPushButton#importActionBtn:pressed  { background: rgba(@accentRgb,0.15); }"
        "QPushButton#importActionBtn:disabled { background: rgba(255,255,255,0.02); border-color: @border; color: @textDisabled; }"

        "QPushButton#importPrimaryBtn {"
        "  background: rgba(@accentRgb,0.10); border: 1px solid rgba(@accentRgb,0.35); border-radius: @radius2;"
        "  color: @accentSoft; font-family: @font; font-size: @fontBase;"
        "  padding: 6px 12px; text-align: left; min-height: 48px;"
        "}"
        "QPushButton#importPrimaryBtn:hover    { background: rgba(@accentRgb,0.18); border-color: rgba(@accentRgb,0.5); }"
        "QPushButton#importPrimaryBtn:pressed  { background: rgba(@accentRgb,0.08); }"
        "QPushButton#importPrimaryBtn:disabled { background: rgba(@accentRgb,0.04); border-color: rgba(@accentRgb,0.15); color: @textDisabled; }"

        // ─────────────────────────────────────────────────────────────────────
        // Contact list table
        // ─────────────────────────────────────────────────────────────────────

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

        // Geodetic settings inline dialog labels
        "QLabel#geoDetectedLabel, QLabel#geoNoteLabel {"
        "  color: @textMuted; font-family: @font; font-size: @fontSm;"
        "}"

        // ─────────────────────────────────────────────────────────────────────
        // Geodesy panel
        // ─────────────────────────────────────────────────────────────────────

        "QLabel#geoAutoDetect { color: @textSubtle; font-family: @font; font-size: @fontSm; }"
        "QLabel#geoLayerRow   { color: @textSecond; font-family: @font; font-size: @fontSm; }"
        "QLabel#geoLayerDot[state=\"ok\"]        { color: @success; }"
        "QLabel#geoLayerDot[state=\"confirmed\"] { color: @accentSoft; }"
        "QLabel#geoLayerDot[state=\"warning\"]   { color: @caution; }"

        "QWidget#panelBody { background: @bgPanel; }"

        // ─────────────────────────────────────────────────────────────────────
        // Import / Correction Dialogs (shared patterns)
        // ─────────────────────────────────────────────────────────────────────

        "SidescanCorrectionDialog { background: @bg; }"
        "QDialog#geodesyWin { background: @bg; }"

        // QCheckBox#dlgCheckBox — legacy alias; global QCheckBox style in AppStyleBase now covers all checkboxes.

        // ─────────────────────────────────────────────────────────────────────
        // Execution Progress Dialog
        // ─────────────────────────────────────────────────────────────────────

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
        "  background: @bgCard; border: 1px solid @border; border-radius: 8px;"
        "}"
        "QFrame#fileCard > QWidget { background: transparent; }"

        "QLabel#formatBadge {"
        "  background: rgba(@accentRgb,0.12); border: 1px solid rgba(@accentRgb,0.30);"
        "  border-radius: @radius3; color: @accentSoft;"
        "  font-family: @font; font-size: @fontSm; font-weight: 700;"
        "  min-width: 42px; max-width: 42px; min-height: 42px; max-height: 42px;"
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

        // Terrain status label overlaid on the 3D map viewport
        "QLabel#map3DTerrainLabel {"
        "  background: rgba(0,0,0,140); color: @success; padding: @padXs @padMd;"
        "}"

    );
}

} // namespace dolphin::ui::detail
