#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QTabWidget;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  SettingsDialog — application preferences stored via QSettings.
//
//  Tabs:
//    Display    — units, palette defaults
//    Processing — default TVG parameters
// -----------------------------------------------------------------------------
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // Key names used with QSettings — accessible so other code can read them.
    static constexpr const char* kKeyUnits          = "display/units";         // "m" or "ft"
    static constexpr const char* kKeyDefaultPalette = "display/palette";       // "Gray", "Hot", …
    static constexpr const char* kKeyTvgSpreading    = "processing/tvg_spread";       // float
    static constexpr const char* kKeyTvgAbsorption  = "processing/tvg_absorb";       // float
    static constexpr const char* kKeyTvgBlanking    = "processing/tvg_blank";        // float
    static constexpr const char* kKeyMapSonarQuality = "display/mapSonarQuality";    // int (MapSonarQuality)

private slots:
    void onAccepted();

private:
    QWidget* buildDisplayTab(QWidget* parent);
    QWidget* buildProcessingTab(QWidget* parent);

    QComboBox*      m_units_combo    = nullptr;
    QComboBox*      m_palette_combo  = nullptr;
    QDoubleSpinBox* m_tvg_spread     = nullptr;
    QDoubleSpinBox* m_tvg_absorb     = nullptr;
    QDoubleSpinBox* m_tvg_blank      = nullptr;
};

} // namespace dolphin::ui
