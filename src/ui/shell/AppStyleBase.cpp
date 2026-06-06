#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssBase()
{
    return QString(

        // -- Scrollbars (global) -----------------------------------------------
        "QScrollBar:vertical {"
        "  background: transparent; width: 8px; margin: 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(255,255,255,0.18);"
        "  border-radius: @radius2; min-height: 30px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.28); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal {"
        "  background: transparent; height: 8px; margin: 0;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: rgba(255,255,255,0.18);"
        "  border-radius: @radius2; min-width: 30px;"
        "}"
        "QScrollBar::handle:horizontal:hover { background: rgba(255,255,255,0.28); }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"

        // -- QLineEdit (global base) -------------------------------------------
        "QLineEdit {"
        "  background: @overlayEl;"
        "  border: 1px solid @overlayHov;"
        "  border-radius: 7px; color: @textSecond; font-size: @fontMd;"
        "  font-family: @font;"
        "  padding: 5px 10px;"
        "  selection-background-color: rgba(@accentRgb,0.4);"
        "}"
        "QLineEdit:focus {"
        "  border-color: rgba(@accentRgb,0.6);"
        "  background: @overlayHov;"
        "}"

        // -- Tree / List views (global base) ----------------------------------
        "QTreeWidget, QListWidget {"
        "  background: transparent; border: none;"
        "  color: @textSecond; font-size: @fontBase; font-family: @font; outline: none;"
        "}"
        "QTreeWidget::item, QListWidget::item {"
        "  padding: 4px 8px; border-radius: @radius3;"
        "}"
        "QTreeWidget::item:hover, QListWidget::item:hover {"
        "  background: @overlayMut;"
        "}"
        "QTreeWidget::item:selected, QListWidget::item:selected {"
        "  background: rgba(@accentRgb,0.25); color: @textPrimary;"
        "}"

        // -- QDoubleSpinBox / QSpinBox (global base) --------------------------
        "QDoubleSpinBox, QSpinBox {"
        "  background: @overlayMut; border: 1px solid rgba(255,255,255,0.09);"
        "  border-radius: 5px; color: @textSecond; font-family: @font; font-size: @fontSm;"
        "  padding: @padXs @padSm; selection-background-color: rgba(@accentRgb,0.35);"
        "}"
        "QDoubleSpinBox:hover, QSpinBox:hover {"
        "  border-color: rgba(255,255,255,0.18); background: rgba(255,255,255,0.07);"
        "}"
        "QDoubleSpinBox:focus, QSpinBox:focus {"
        "  border-color: rgba(@accentRgb,0.55); background: @overlayHov;"
        "}"
        "QDoubleSpinBox:disabled, QSpinBox:disabled {"
        "  background: rgba(255,255,255,0.02); border-color: @overlayMut;"
        "  color: @textDisabled;"
        "}"
        "QDoubleSpinBox::up-button, QSpinBox::up-button,"
        "QDoubleSpinBox::down-button, QSpinBox::down-button {"
        "  border: none; width: 14px;"
        "  background: transparent;"
        "}"
        "QDoubleSpinBox::up-button:hover, QSpinBox::up-button:hover,"
        "QDoubleSpinBox::down-button:hover, QSpinBox::down-button:hover {"
        "  background: @overlayHov;"
        "}"

        // -- QComboBox (global base) -------------------------------------------
        // text-align:left prevents Windows from centring combo text by default.
        "QComboBox {"
        "  background: @bgCard; border: 1px solid @borderMenu; border-radius: 5px;"
        "  color: @textSecond; font-family: @font; font-size: @fontBase;"
        "  padding: 3px 24px 3px 8px; text-align: left;"
        "}"
        "QComboBox:hover { border-color: rgba(255,255,255,0.18); background: @bgHover; }"
        "QComboBox:focus { border-color: rgba(@accentRgb,0.6); background: @bgHover; }"
        "QComboBox::drop-down {"
        "  border: none; width: 20px;"
        "  subcontrol-origin: padding; subcontrol-position: right center;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background: @bgCard; border: 1px solid @borderMenu;"
        "  color: @textPrimary; selection-background-color: rgba(@accentRgb,0.3);"
        "  outline: none; padding: 2px 0; text-align: left;"
        "}"

        // -- QCheckBox (global base) ------------------------------------------
        "QCheckBox {"
        "  color: @textSecond; font-family: @font; font-size: @fontBase; spacing: 6px;"
        "}"
        "QCheckBox::indicator {"
        "  width: 14px; height: 14px; border-radius: @radius1;"
        "  border: 1px solid rgba(255,255,255,0.15); background: @overlayMut;"
        "}"
        "QCheckBox::indicator:hover   { border-color: rgba(255,255,255,0.30); }"
        "QCheckBox::indicator:checked {"
        "  background: @accent; border-color: @accent;"
        "  image: url(:/icons/check_white.svg);"
        "}"
        "QCheckBox::indicator:disabled { background: rgba(255,255,255,0.03); border-color: rgba(255,255,255,0.07); }"
        "QCheckBox:disabled { color: @textDisabled; }"

        // -- QRadioButton (global base) ---------------------------------------
        "QRadioButton {"
        "  color: @textSecond; font-family: @font; font-size: @fontBase; spacing: 6px;"
        "}"
        "QRadioButton::indicator {"
        "  width: 14px; height: 14px; border-radius: 7px;"
        "  border: 1px solid rgba(255,255,255,0.15); background: @overlayMut;"
        "}"
        "QRadioButton::indicator:hover   { border-color: rgba(255,255,255,0.30); }"
        "QRadioButton::indicator:checked { background: @accent; border-color: @accent; }"
        "QRadioButton::indicator:disabled { background: rgba(255,255,255,0.03); border-color: rgba(255,255,255,0.07); }"
        "QRadioButton:disabled { color: @textDisabled; }"

        // -- QGroupBox ---------------------------------------------------------
        "QGroupBox {"
        "  border: 1px solid @border;"
        "  border-radius: @radius3; margin-top: 10px; padding-top: 6px;"
        "  color: @textMuted; font-size: @fontXs; font-weight: 600;"
        "  font-family: @font;"
        "  letter-spacing: 0.5px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; subcontrol-position: top left;"
        "  left: 8px; padding: 0 4px; color: @textMuted;"
        "}"

        // -- QSplitter ---------------------------------------------------------
        "QSplitter::handle            { background: @border; }"
        "QSplitter::handle:vertical   { height: 1px; }"
        "QSplitter::handle:horizontal { width: 1px; }"

        // -- QProgressBar ------------------------------------------------------
        "QProgressBar { background: @border; border: none; border-radius: 2px; }"
        "QProgressBar::chunk { background: @accent; border-radius: 2px; }"

    );
}

} // namespace dolphin::ui::detail
