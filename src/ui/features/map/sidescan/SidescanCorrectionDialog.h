#pragma once
#include "ui/features/map/sidescan/SssGeorefParams.h"
#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QVBoxLayout;

namespace dolphin::ui {

// Correction dialog for sidescan sonar layers.
// Follows the ImportDialog header/body/footer visual structure.
// Mode::Navigation — position offsets and timing corrections.
// Mode::Heading    — full Navigation / Georeference parameter set.
class SidescanCorrectionDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode       { Navigation, Heading };
    enum class ApplyScope { SelectedLayer, AllLayers };

    SidescanCorrectionDialog(Mode           mode,
                             const QString& layer_name,
                             QWidget*       parent = nullptr);

    // Returns the complete georef parameter set from all heading-mode controls.
    SssGeorefParams headingParams() const;

signals:
    void applyRequested(ApplyScope scope);
    void previewRequested();
    void resetRequested();

private slots:
    void onApply();
    void onPreview();
    void onReset();
    void onLaybackToggled(bool enabled);
    void onUseFileLaybackToggled(bool useFile);

private:
    void buildLayerSection        (QVBoxLayout* body, const QString& layer_name);
    void buildNavigationSection   (QVBoxLayout* body);
    void buildNavSourceSection    (QVBoxLayout* body);
    void buildHeadingSourceSection(QVBoxLayout* body);
    void buildLaybackSection      (QVBoxLayout* body);
    void buildSmoothingSection    (QVBoxLayout* body);
    void buildManualOffsetSection (QVBoxLayout* body);
    void buildDebugSection        (QVBoxLayout* body);
    void buildScopeRow            (QVBoxLayout* body);

    Mode          m_mode;
    QPushButton*  m_btn_apply   = nullptr;
    QPushButton*  m_btn_preview = nullptr;
    QPushButton*  m_btn_reset   = nullptr;

    // Navigation mode controls
    QDoubleSpinBox* m_along_track = nullptr;
    QDoubleSpinBox* m_cross_track = nullptr;
    QDoubleSpinBox* m_time_delay  = nullptr;

    // Nav position source
    QComboBox*      m_nav_source  = nullptr;

    // Heading source
    QComboBox*      m_heading_source = nullptr;
    QDoubleSpinBox* m_hdg_offset     = nullptr;
    QCheckBox*      m_swap_channels  = nullptr;

    // Layback
    QCheckBox*      m_enable_layback      = nullptr;
    QCheckBox*      m_use_file_layback    = nullptr;
    QDoubleSpinBox* m_manual_layback_dist = nullptr;

    // Smoothing
    QComboBox*  m_smoothing_mode   = nullptr;
    QSpinBox*   m_smoothing_window = nullptr;

    // Manual offsets
    QDoubleSpinBox* m_x_offset = nullptr;
    QDoubleSpinBox* m_y_offset = nullptr;

    // Debug display
    QCheckBox*  m_debug_ping_lines = nullptr;

    // Scope / apply-to
    QComboBox*  m_apply_to    = nullptr;
    QComboBox*  m_scope_combo = nullptr;
};

} // namespace dolphin::ui
