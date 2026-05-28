#pragma once
#include "ui/features/waterfall/WaterfallParams.h"
#include "ui/features/waterfall/WaterfallOverlayParams.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  WaterfallSettingsDialog — professional tabbed settings for WaterfallWindow.
//
//  Tabs:
//    Display      — channel (Both/Port/Stbd), amplitude bar visibility
//    Performance  — ping window size
//    Crosshair    — show, colour, style, width, opacity
//    Grid         — range grid show, colour, opacity, interval
//    Annotations  — scale bar visibility
//
//  Persists all values to QSettings.  Emits applied() on Apply / OK.
//  loadDefaults() rebuilds a Settings from QSettings without showing the dialog.
// ─────────────────────────────────────────────────────────────────────────────
class WaterfallSettingsDialog : public QDialog {
    Q_OBJECT
public:
    struct Settings {
        // ── Display ───────────────────────────────────────────────────────
        int            window_size     = 4000;
        DisplayChannel display_channel = DisplayChannel::Both;
        bool           show_amp_bar    = true;
        // ── Overlay ───────────────────────────────────────────────────────
        WfOverlayParams overlay;
    };

    // QSettings keys
    static constexpr const char* kKeyWindowSize      = "waterfall/windowSize";
    static constexpr const char* kKeyDisplayChannel  = "waterfall/displayChannel";
    static constexpr const char* kKeyShowAmpBar      = "waterfall/showAmpBar";
    static constexpr const char* kKeyXhairShow       = "waterfall/xhairShow";
    static constexpr const char* kKeyXhairColor      = "waterfall/xhairColor";
    static constexpr const char* kKeyXhairStyle      = "waterfall/xhairStyle";
    static constexpr const char* kKeyXhairWidth      = "waterfall/xhairWidth";
    static constexpr const char* kKeyXhairOpacity    = "waterfall/xhairOpacity";
    static constexpr const char* kKeyGridShow        = "waterfall/gridShow";
    static constexpr const char* kKeyGridColor       = "waterfall/gridColor";
    static constexpr const char* kKeyGridOpacity     = "waterfall/gridOpacity";
    static constexpr const char* kKeyGridInterval    = "waterfall/gridIntervalM";

    WaterfallSettingsDialog(const Settings& current, QWidget* parent = nullptr);

    Settings currentSettings() const;

    // Load persisted defaults — call this to initialise WaterfallWindow on startup.
    static Settings loadDefaults();

signals:
    void applied(dolphin::ui::WaterfallSettingsDialog::Settings s);

private slots:
    void onApply();

private:
    void buildDisplayTab    (QTabWidget* tabs);
    void buildPerformanceTab(QTabWidget* tabs);
    void buildCrosshairTab  (QTabWidget* tabs);
    void buildGridTab       (QTabWidget* tabs);

    static void updateColorButton(QPushButton* btn, const QColor& c);

    // ── Display ───────────────────────────────────────────────────────────
    QComboBox* m_channel_combo   = nullptr;
    QCheckBox* m_amp_bar_check   = nullptr;

    // ── Performance ───────────────────────────────────────────────────────
    QSpinBox*  m_window_size_spin = nullptr;

    // ── Crosshair ─────────────────────────────────────────────────────────
    QCheckBox*  m_xhair_show_check   = nullptr;
    QPushButton* m_xhair_color_btn   = nullptr;
    QComboBox*  m_xhair_style_combo  = nullptr;
    QSpinBox*   m_xhair_width_spin   = nullptr;
    QSpinBox*   m_xhair_opacity_spin = nullptr;
    QColor      m_xhair_color;

    // ── Grid ──────────────────────────────────────────────────────────────
    QCheckBox*      m_grid_show_check   = nullptr;
    QPushButton*    m_grid_color_btn    = nullptr;
    QSpinBox*       m_grid_opacity_spin = nullptr;
    QDoubleSpinBox* m_grid_interval_spin= nullptr;
    QColor          m_grid_color;
};

} // namespace dolphin::ui
