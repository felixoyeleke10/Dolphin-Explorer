#pragma once
#include "ui/shell/Theme.h"
#include <QColor>
#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  AppSettingsDialog — application-wide preferences.
//
//  Sidebar navigation with six pages:
//    General      — units, depth unit, coordinate/time format
//    Appearance   — theme, UI density, font size, tooltips
//    Performance  — worker threads, tile cache, auto-save, GPU acceleration
//    Data         — sound velocity, auto-stretch, default palette, default CRS
//    Export       — output directory, default export format
//    About        — version, build info, credits
//
//  All values persist to QSettings under the "app/" key group.
//  Call loadDefaults() to read current settings without opening the dialog.
// ─────────────────────────────────────────────────────────────────────────────

class AppSettingsDialog : public QDialog {
    Q_OBJECT
public:
    struct Settings {
        // ── General ───────────────────────────────────────────────────────
        int     units_system    = 0;    // 0=Metric, 1=Imperial
        int     depth_unit      = 0;    // 0=Meters, 1=Feet
        int     coord_format    = 0;    // 0=Decimal Degrees, 1=DMS, 2=UTM
        int     date_format     = 0;    // 0=ISO 8601 (UTC), 1=Local time

        // ── Appearance ────────────────────────────────────────────────────
        int     theme           = 0;    // 0=Dark, 1=Light (future)
        int     ui_density      = 1;    // 0=Compact, 1=Normal, 2=Comfortable
        int     font_size       = 1;    // 0=Small, 1=Medium, 2=Large
        bool    show_tooltips   = true;
        bool    show_grid       = true;

        // ── Performance ───────────────────────────────────────────────────
        int     worker_threads  = 4;    // 1–16
        int     tile_cache_mb   = 512;  // 64–4096
        int     autosave_min    = 5;    // 0=disabled, 1–60
        bool    gpu_accel       = true;

        // ── Data defaults ─────────────────────────────────────────────────
        double  sound_velocity  = 1500.0;   // m/s
        bool    auto_stretch    = true;
        int     default_palette = 0;        // 0=Thermal, 1=Greyscale, 2=Ocean, 3=Copper
        int     default_crs     = 0;        // 0=WGS 84, 1=NAD83, 2=ETRS89

        // ── Export ────────────────────────────────────────────────────────
        QString export_dir;
        int     export_format   = 0;        // 0=CSV, 1=GeoJSON, 2=KMZ, 3=Shapefile

        // ── Map ───────────────────────────────────────────────────────────
        QColor  map_bg_color    { Theme::kBg };  // 2D/3D viewport background
        int     map_grid_preset    = 0;            // index into kMapGridPresets
        int     grid_label_size    = 1;            // 0=Small/7pt, 1=Normal/8pt, 2=Large/10pt
        bool    grid_label_rotated = false;        // true = lat labels rotated 90°
        int     graticule_coord    = 0;            // 0=Auto, 1=Degrees, 2=Easting/Northing, 3=Both
    };

    // QSettings key constants
    static constexpr const char* kKeyUnits          = "app/unitsSystem";
    static constexpr const char* kKeyDepthUnit       = "app/depthUnit";
    static constexpr const char* kKeyCoordFormat     = "app/coordFormat";
    static constexpr const char* kKeyDateFormat      = "app/dateFormat";
    static constexpr const char* kKeyTheme           = "app/theme";
    static constexpr const char* kKeyUiDensity       = "app/uiDensity";
    static constexpr const char* kKeyFontSize        = "app/fontSize";
    static constexpr const char* kKeyShowTooltips    = "app/showTooltips";
    static constexpr const char* kKeyShowGrid        = "app/showGrid";
    static constexpr const char* kKeyWorkerThreads   = "app/workerThreads";
    static constexpr const char* kKeyTileCacheMb     = "app/tileCacheMb";
    static constexpr const char* kKeyAutosaveMin     = "app/autosaveMin";
    static constexpr const char* kKeyGpuAccel        = "app/gpuAccel";
    static constexpr const char* kKeySoundVelocity   = "app/soundVelocity";
    static constexpr const char* kKeyAutoStretch      = "app/autoStretch";
    static constexpr const char* kKeyDefaultPalette   = "app/defaultPalette";
    static constexpr const char* kKeyDefaultCrs       = "app/defaultCrs";
    static constexpr const char* kKeyExportDir        = "app/exportDir";
    static constexpr const char* kKeyExportFormat     = "app/exportFormat";
    static constexpr const char* kKeyMapBgColor       = "app/mapBgColor";
    static constexpr const char* kKeyMapGridPreset    = "app/mapGridPreset";
    static constexpr const char* kKeyGridLabelSize    = "app/gridLabelSize";
    static constexpr const char* kKeyGridLabelRotated = "app/gridLabelRotated";
    static constexpr const char* kKeyGratCoord        = "app/gratCoord";

    struct MapBgPreset { const char* name; const char* hex; };
    static constexpr MapBgPreset kMapBgPresets[] = {
        { "Dark",       "#111113" },
        { "Deep Blue",  "#0a1a2e" },
        { "Slate",      "#1c2433" },
        { "Charcoal",   "#1a1a1a" },
        { "Night",      "#000000" },
    };

    struct MapGridPreset { const char* name; const char* line2d; const char* minor3d; const char* major3d; };
    static constexpr MapGridPreset kMapGridPresets[] = {
        { "Steel Blue (default)", "#2d2d2f", "#425878", "#7099C2" },
        { "Ocean Teal",           "#1e3840", "#1e6070", "#3aa0b8" },
        { "Warm Grey",            "#383830", "#585850", "#807868" },
        { "Bright",               "#4a4a55", "#607890", "#90b8d8" },
        { "Minimal",              "#222224", "#2e3a42", "#3e5060" },
    };

    explicit AppSettingsDialog(const Settings& current, QWidget* parent = nullptr);

    Settings currentSettings() const;
    static Settings loadDefaults();

signals:
    void applied(dolphin::ui::AppSettingsDialog::Settings s);

private slots:
    void onApply();

private:
    QWidget* buildGeneralPage    ();
    QWidget* buildAppearancePage ();
    QWidget* buildPerformancePage();
    QWidget* buildDataPage       ();
    QWidget* buildExportPage     ();
    QWidget* buildAboutPage      ();

    void fillControls(const Settings& s);

    QListWidget*    m_nav   = nullptr;
    QStackedWidget* m_stack = nullptr;

    // ── General ───────────────────────────────────────────────────────────
    QComboBox* m_units_combo        = nullptr;
    QComboBox* m_depth_unit_combo   = nullptr;
    QComboBox* m_coord_format_combo = nullptr;
    QComboBox* m_date_format_combo  = nullptr;

    // ── Appearance ────────────────────────────────────────────────────────
    QComboBox* m_theme_combo        = nullptr;
    QComboBox* m_density_combo      = nullptr;
    QComboBox* m_font_size_combo    = nullptr;
    QCheckBox* m_tooltips_check     = nullptr;
    QCheckBox* m_grid_check         = nullptr;

    // ── Performance ───────────────────────────────────────────────────────
    QSpinBox*  m_workers_spin       = nullptr;
    QSpinBox*  m_cache_spin         = nullptr;
    QSpinBox*  m_autosave_spin      = nullptr;
    QCheckBox* m_gpu_accel_check    = nullptr;

    // ── Data defaults ─────────────────────────────────────────────────────
    QDoubleSpinBox* m_sound_vel_spin = nullptr;
    QCheckBox*      m_auto_stretch   = nullptr;
    QComboBox*      m_palette_combo  = nullptr;
    QComboBox*      m_crs_combo      = nullptr;

    // ── Export ────────────────────────────────────────────────────────────
    QLineEdit*   m_export_dir_edit  = nullptr;
    QComboBox*   m_export_fmt_combo = nullptr;

    // ── Map ───────────────────────────────────────────────────────────────
    QComboBox*   m_map_bg_combo              = nullptr;
    QComboBox*   m_map_grid_combo            = nullptr;
    QComboBox*   m_grid_label_size_combo     = nullptr;
    QCheckBox*   m_grid_label_rotated_check  = nullptr;
    QComboBox*   m_grat_coord_combo          = nullptr;
};

} // namespace dolphin::ui
