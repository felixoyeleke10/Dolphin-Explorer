#pragma once
#include "ui/features/subbottom/panels/SubBottomDisplayPanel.h"
#include "ui/features/subbottom/SubBottomViewStyle.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  SubBottomSettingsDialog — professional tabbed settings for SubBottomWindow.
//
//  Tabs:
//    Display      — palette, gain, bottom track show/colour/thickness
//    Acquisition  — sound speed
//    View Scale   — px/trace, px/sample
//    Crosshair    — show, colour, style, width, opacity
//    Grid         — depth-time grid show, colour, opacity, interval
//
//  Persists all values to QSettings.  Emits applied() on Apply / OK.
// -----------------------------------------------------------------------------
class SubBottomSettingsDialog : public QDialog {
    Q_OBJECT
public:
    struct ViewScale {
        int   px_per_trace  = 2;
        float px_per_sample = 0.5f;
    };

    SubBottomSettingsDialog(const SubBottomDisplayParams& params,
                            const ViewScale&              scale,
                            const SubBottomViewStyle&     style,
                            QWidget* parent = nullptr);

    SubBottomDisplayParams displayParams() const;
    ViewScale              viewScale()     const;
    SubBottomViewStyle     viewStyle()     const;

signals:
    void applied(dolphin::ui::SubBottomDisplayParams params,
                 int px_per_trace, float px_per_sample,
                 dolphin::ui::SubBottomViewStyle style);

private slots:
    void onApply();

private:
    void buildDisplayTab   (QTabWidget* tabs);
    void buildAcquisitionTab(QTabWidget* tabs);
    void buildViewScaleTab (QTabWidget* tabs);
    void buildCrosshairTab (QTabWidget* tabs);
    void buildGridTab      (QTabWidget* tabs);

    static void updateColorButton(QPushButton* btn, const QColor& c);

    // -- Display -----------------------------------------------------------
    QComboBox*      m_palette_combo     = nullptr;
    QDoubleSpinBox* m_gain_spin         = nullptr;
    QDoubleSpinBox* m_contrast_spin     = nullptr;
    QCheckBox*      m_polarity_check    = nullptr;
    QCheckBox*      m_bt_check          = nullptr;
    QPushButton*    m_bt_color_btn      = nullptr;
    QSpinBox*       m_bt_thickness_spin = nullptr;
    QColor          m_bt_color;

    // -- Acquisition -------------------------------------------------------
    QDoubleSpinBox* m_speed_spin        = nullptr;

    // -- View Scale --------------------------------------------------------
    QSpinBox*       m_trace_w_spin      = nullptr;
    QDoubleSpinBox* m_depth_s_spin      = nullptr;

    // -- Crosshair ---------------------------------------------------------
    QCheckBox*      m_xhair_show_check  = nullptr;
    QPushButton*    m_xhair_color_btn   = nullptr;
    QComboBox*      m_xhair_style_combo = nullptr;
    QSpinBox*       m_xhair_width_spin  = nullptr;
    QSpinBox*       m_xhair_opacity_spin= nullptr;
    QColor          m_xhair_color;

    // -- Grid --------------------------------------------------------------
    QCheckBox*      m_grid_show_check   = nullptr;
    QPushButton*    m_grid_color_btn    = nullptr;
    QSpinBox*       m_grid_opacity_spin = nullptr;
    QDoubleSpinBox* m_grid_interval_spin= nullptr;
    QColor          m_grid_color;
};

} // namespace dolphin::ui
