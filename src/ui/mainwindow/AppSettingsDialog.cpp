// AppSettingsDialog.cpp — constructor, controls fill/read, apply, load defaults.
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <algorithm>
#include <cstddef>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace dolphin::ui {

static constexpr int kSettingsSidebarW = 196;
static constexpr int kMinW             = 740;
static constexpr int kMinH             = 540;

// -----------------------------------------------------------------------------
//  Constructor
// -----------------------------------------------------------------------------

AppSettingsDialog::AppSettingsDialog(const Settings& current, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setMinimumSize(kMinW, kMinH);
    setModal(true);

    auto* root    = makeCompactLayout<QVBoxLayout>(this);
    auto* content = new QWidget(this);
    auto* hl      = makeCompactLayout<QHBoxLayout>(content);

    auto* sidebar = new QWidget(content);
    sidebar->setObjectName("settingsSidebar");
    sidebar->setFixedWidth(kSettingsSidebarW);
    auto* sbl = new QVBoxLayout(sidebar);
    sbl->setContentsMargins(6, 8, 6, 8);
    sbl->setSpacing(2);

    m_nav_group = new QButtonGroup(sidebar);
    m_nav_group->setExclusive(true);
    static const char* kPageLabels[] = {
        "General", "Appearance", "Performance", "Data", "Export", "Map", "About"
    };
    static const char* kPageIcons[] = {
        ":/icons/settings.svg",
        ":/icons/panel_display.svg",
        ":/icons/analyze.svg",
        ":/icons/database.svg",
        ":/icons/export.svg",
        ":/icons/geodesy.svg",
        ":/icons/panel_info.svg",
    };
    for (int i = 0; i < 7; ++i) {
        auto* btn = new QToolButton(sidebar);
        btn->setText(tr(kPageLabels[i]));
        btn->setIcon(QIcon(QString::fromLatin1(kPageIcons[i])));
        btn->setIconSize(QSize(15, 15));
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setCheckable(true);
        btn->setObjectName("settingsNavBtn");
        btn->setFixedHeight(36);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_nav_group->addButton(btn, i);
        sbl->addWidget(btn);
    }
    sbl->addStretch();
    hl->addWidget(sidebar);

    m_stack = new QStackedWidget(content);
    m_stack->addWidget(buildGeneralPage());
    m_stack->addWidget(buildAppearancePage());
    m_stack->addWidget(buildPerformancePage());
    m_stack->addWidget(buildDataPage());
    m_stack->addWidget(buildExportPage());
    m_stack->addWidget(buildMapPage());
    m_stack->addWidget(buildAboutPage());
    hl->addWidget(m_stack, 1);

    connect(m_nav_group, &QButtonGroup::idClicked,
            m_stack, &QStackedWidget::setCurrentIndex);
    m_nav_group->button(0)->setChecked(true);

    root->addWidget(content, 1);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("dialogSeparator");
    root->addWidget(sep);

    auto* btn_box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel,
        this);
    btn_box->setContentsMargins(Theme::kSpacing5, Theme::kSpacing3, Theme::kSpacing5, Theme::kSpacing4);
    btn_box->button(QDialogButtonBox::Ok)->setObjectName("dlgBtnOk");

    connect(btn_box->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &AppSettingsDialog::onApply);
    connect(btn_box, &QDialogButtonBox::accepted, this, [this]() {
        onApply(); accept();
    });
    connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btn_box);

    fillControls(current);

    // Override the palette combo with the currently active SSS palette so the
    // dialog always shows what is actually rendered, not just the app-level default.
    // These can diverge when the user changes palette via the layer inspector.
    {
        QSettings qs;
        const QVariant v = qs.value(QStringLiteral("sss/paletteIdx"));
        if (v.isValid()) {
            const int idx = v.toInt();
            if (idx >= 0 && idx < m_palette_combo->count())
                m_palette_combo->setCurrentIndex(idx);
        }
    }
}

// -----------------------------------------------------------------------------
//  Controls fill / read-back
// -----------------------------------------------------------------------------

void AppSettingsDialog::fillControls(const Settings& s)
{
    m_units_combo->setCurrentIndex(s.units_system);
    m_depth_unit_combo->setCurrentIndex(s.depth_unit);
    m_coord_format_combo->setCurrentIndex(s.coord_format);
    m_date_format_combo->setCurrentIndex(s.date_format);

    m_theme_combo->setCurrentIndex(s.theme);
    m_density_combo->setCurrentIndex(s.ui_density);
    m_font_size_combo->setCurrentIndex(s.font_size);
    m_tooltips_check->setChecked(s.show_tooltips);
    m_grid_check->setChecked(s.show_grid);

    m_workers_spin->setValue(s.worker_threads);
    m_cache_spin->setValue(s.tile_cache_mb);
    m_autosave_spin->setValue(s.autosave_min);
    m_gpu_accel_check->setChecked(s.gpu_accel);

    m_sound_vel_spin->setValue(s.sound_velocity);
    m_auto_stretch->setChecked(s.auto_stretch);
    m_palette_combo->setCurrentIndex(s.default_palette);
    m_crs_combo->setCurrentIndex(s.default_crs);

    m_export_dir_edit->setText(s.export_dir);
    m_export_fmt_combo->setCurrentIndex(s.export_format);

    {
        int idx = 0;
        const QString target = s.map_bg_color.isValid()
            ? s.map_bg_color.name() : QStringLiteral("#111113");
        for (int i = 0; i < int(std::size(kMapBgPresets)); ++i)
            if (QLatin1String(kMapBgPresets[i].hex) == target) { idx = i; break; }
        m_map_bg_combo->setCurrentIndex(idx);
    }
    m_map_grid_combo->setCurrentIndex(
        std::clamp(s.map_grid_preset, 0, int(std::size(kMapGridPresets)) - 1));
    m_grid_label_size_combo->setCurrentIndex(std::clamp(s.grid_label_size, 0, 2));
    m_grid_label_rotated_check->setChecked(s.grid_label_rotated);
    m_grat_coord_combo->setCurrentIndex(std::clamp(s.graticule_coord, 0, 3));
}

AppSettingsDialog::Settings AppSettingsDialog::currentSettings() const
{
    Settings s;
    s.units_system    = m_units_combo->currentIndex();
    s.depth_unit      = m_depth_unit_combo->currentIndex();
    s.coord_format    = m_coord_format_combo->currentIndex();
    s.date_format     = m_date_format_combo->currentIndex();

    s.theme           = m_theme_combo->currentIndex();
    s.ui_density      = m_density_combo->currentIndex();
    s.font_size        = m_font_size_combo->currentIndex();
    s.show_tooltips   = m_tooltips_check->isChecked();
    s.show_grid       = m_grid_check->isChecked();

    s.worker_threads  = m_workers_spin->value();
    s.tile_cache_mb   = m_cache_spin->value();
    s.autosave_min    = m_autosave_spin->value();
    s.gpu_accel       = m_gpu_accel_check->isChecked();

    s.sound_velocity  = m_sound_vel_spin->value();
    s.auto_stretch    = m_auto_stretch->isChecked();
    s.default_palette = m_palette_combo->currentIndex();
    s.default_crs     = m_crs_combo->currentIndex();

    s.export_dir      = m_export_dir_edit->text().trimmed();
    s.export_format   = m_export_fmt_combo->currentIndex();

    {
        const int i = m_map_bg_combo->currentIndex();
        s.map_bg_color = QColor(
            (i >= 0 && i < int(std::size(kMapBgPresets)))
                ? kMapBgPresets[i].hex : kMapBgPresets[0].hex);
    }
    s.map_grid_preset    = std::clamp(m_map_grid_combo->currentIndex(),
                                      0, int(std::size(kMapGridPresets)) - 1);
    s.grid_label_size    = std::clamp(m_grid_label_size_combo->currentIndex(), 0, 2);
    s.grid_label_rotated = m_grid_label_rotated_check->isChecked();
    s.graticule_coord    = std::clamp(m_grat_coord_combo->currentIndex(), 0, 3);
    return s;
}

// -----------------------------------------------------------------------------
//  Apply / persist
// -----------------------------------------------------------------------------

void AppSettingsDialog::onApply()
{
    const Settings s = currentSettings();
    QSettings qs;
    qs.setValue(kKeyUnits,          s.units_system);
    qs.setValue(kKeyDepthUnit,      s.depth_unit);
    qs.setValue(kKeyCoordFormat,    s.coord_format);
    qs.setValue(kKeyDateFormat,     s.date_format);
    qs.setValue(kKeyTheme,          s.theme);
    qs.setValue(kKeyUiDensity,      s.ui_density);
    qs.setValue(kKeyFontSize,       s.font_size);
    qs.setValue(kKeyShowTooltips,   s.show_tooltips);
    qs.setValue(kKeyShowGrid,       s.show_grid);
    qs.setValue(kKeyWorkerThreads,  s.worker_threads);
    qs.setValue(kKeyTileCacheMb,    s.tile_cache_mb);
    qs.setValue(kKeyAutosaveMin,    s.autosave_min);
    qs.setValue(kKeyGpuAccel,       s.gpu_accel);
    qs.setValue(kKeySoundVelocity,  s.sound_velocity);
    qs.setValue(kKeyAutoStretch,    s.auto_stretch);
    qs.setValue(kKeyDefaultPalette, s.default_palette);
    qs.setValue(kKeyDefaultCrs,     s.default_crs);
    qs.setValue(kKeyExportDir,      s.export_dir);
    qs.setValue(kKeyExportFormat,   s.export_format);
    qs.setValue(kKeyMapBgColor,        s.map_bg_color.name());
    qs.setValue(kKeyMapGridPreset,     s.map_grid_preset);
    qs.setValue(kKeyGridLabelSize,     s.grid_label_size);
    qs.setValue(kKeyGridLabelRotated,  s.grid_label_rotated);
    qs.setValue(kKeyGratCoord,         s.graticule_coord);

    // Mirror to legacy SettingsDialog keys so existing readers (SidescanViewController,
    // RightPanel.Display, LayerInspectorPage) pick up the change without restart.
    qs.setValue("display/units", s.units_system == 0 ? "m" : "ft");
    static constexpr const char* kPaletteNames[] = {
        "Thermal", "Gray", "Ocean", "Copper",
        "Inverted", "Viridis", "Plasma", "Midnight", "Sand", "Spectrum"
    };
    const char* palette_name = (s.default_palette >= 0 && s.default_palette < 10)
        ? kPaletteNames[s.default_palette] : "Gray";
    qs.setValue("display/palette", QLatin1String(palette_name));
    // Also write the SSS-specific key so the palette survives an app restart
    // even if the user never opened the layer inspector.
    qs.setValue(QStringLiteral("sss/paletteIdx"), s.default_palette);

    emit applied(s);
}

AppSettingsDialog::Settings AppSettingsDialog::loadDefaults()
{
    QSettings qs;
    Settings s;
    s.units_system    = qs.value(kKeyUnits,          s.units_system).toInt();
    s.depth_unit      = qs.value(kKeyDepthUnit,       s.depth_unit).toInt();
    s.coord_format    = qs.value(kKeyCoordFormat,     s.coord_format).toInt();
    s.date_format     = qs.value(kKeyDateFormat,      s.date_format).toInt();
    s.theme           = qs.value(kKeyTheme,           s.theme).toInt();
    s.ui_density      = qs.value(kKeyUiDensity,       s.ui_density).toInt();
    s.font_size       = qs.value(kKeyFontSize,        s.font_size).toInt();
    s.show_tooltips   = qs.value(kKeyShowTooltips,    s.show_tooltips).toBool();
    s.show_grid       = qs.value(kKeyShowGrid,        s.show_grid).toBool();
    s.worker_threads  = qs.value(kKeyWorkerThreads,   s.worker_threads).toInt();
    s.tile_cache_mb   = qs.value(kKeyTileCacheMb,     s.tile_cache_mb).toInt();
    s.autosave_min    = qs.value(kKeyAutosaveMin,     s.autosave_min).toInt();
    s.gpu_accel       = qs.value(kKeyGpuAccel,        s.gpu_accel).toBool();
    s.sound_velocity  = qs.value(kKeySoundVelocity,   s.sound_velocity).toDouble();
    s.auto_stretch    = qs.value(kKeyAutoStretch,     s.auto_stretch).toBool();
    s.default_palette = qs.value(kKeyDefaultPalette,  s.default_palette).toInt();
    s.default_crs     = qs.value(kKeyDefaultCrs,      s.default_crs).toInt();
    s.export_dir      = qs.value(kKeyExportDir,       s.export_dir).toString();
    s.export_format   = qs.value(kKeyExportFormat,    s.export_format).toInt();

    const QString bg_hex = qs.value(kKeyMapBgColor, s.map_bg_color.name()).toString();
    if (QColor::isValidColorName(bg_hex))
        s.map_bg_color = QColor(bg_hex);
    s.map_grid_preset    = qs.value(kKeyMapGridPreset,     s.map_grid_preset).toInt();
    s.grid_label_size    = qs.value(kKeyGridLabelSize,     s.grid_label_size).toInt();
    s.grid_label_rotated = qs.value(kKeyGridLabelRotated,  s.grid_label_rotated).toBool();
    s.graticule_coord    = qs.value(kKeyGratCoord,          s.graticule_coord).toInt();

    // One-time migration from legacy SettingsDialog keys (first run after upgrade).
    if (!qs.contains(kKeyUnits) && qs.contains("display/units"))
        s.units_system = qs.value("display/units").toString() == "ft" ? 1 : 0;
    if (!qs.contains(kKeyDefaultPalette) && qs.contains("display/palette")) {
        const QString n = qs.value("display/palette").toString();
        if      (n == "Hot"    || n == "Thermal")  s.default_palette = 0;
        else if (n == "Ocean")                      s.default_palette = 2;
        else if (n == "Copper")                     s.default_palette = 3;
        else if (n == "Inverted")                   s.default_palette = 4;
        else if (n == "Viridis")                    s.default_palette = 5;
        else if (n == "Plasma")                     s.default_palette = 6;
        else if (n == "Midnight")                   s.default_palette = 7;
        else if (n == "Sand")                       s.default_palette = 8;
        else if (n == "Spectrum" || n == "Turbo")   s.default_palette = 9;
        else                                        s.default_palette = 1;
    }

    return s;
}

} // namespace dolphin::ui
