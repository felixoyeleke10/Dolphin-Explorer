// AppSettingsDialog.Pages.cpp — page builders and layout helpers.
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <cstddef>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSysInfo>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

namespace dolphin::ui {

namespace {

QScrollArea* wrapInScroll(QWidget* page)
{
    auto* scroll = new QScrollArea;
    scroll->setObjectName("settingsPageScroll");
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

void addSection(QVBoxLayout* vl, const QString& title, QWidget* parent)
{
    if (vl->count() > 0)
        vl->addSpacing(20);

    auto* lbl = new QLabel(title.toUpper(), parent);
    lbl->setObjectName("settingsSectionTitle");
    vl->addWidget(lbl);

    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setObjectName("settingsDivider");
    vl->addWidget(line);
    vl->addSpacing(6);
}

QFormLayout* makeForm()
{
    auto* fl = new QFormLayout;
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fl->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    fl->setHorizontalSpacing(16);
    fl->setVerticalSpacing(9);
    return fl;
}

void addHint(QFormLayout* fl, const QString& text, QWidget* parent)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setObjectName("fieldHint");
    lbl->setWordWrap(true);
    fl->addRow(QString(), lbl);
}

} // namespace

// -----------------------------------------------------------------------------
//  Page builders
// -----------------------------------------------------------------------------

QWidget* AppSettingsDialog::buildGeneralPage()
{
    auto* page = new QWidget;
    page->setObjectName("settingsPage");
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(28, Theme::kSpacing7, 28, Theme::kSpacing7);
    vl->setSpacing(0);

    auto* title = new QLabel(tr("General"), page);
    title->setObjectName("settingsPageTitle");
    vl->addWidget(title);
    vl->addSpacing(18);

    addSection(vl, tr("Measurement"), page);
    auto* mfl = makeForm();

    m_units_combo = new QComboBox(page);
    m_units_combo->addItems({ tr("Metric  (m, km)"), tr("Imperial  (ft, nmi)") });
    mfl->addRow(tr("System:"), m_units_combo);

    m_depth_unit_combo = new QComboBox(page);
    m_depth_unit_combo->addItems({ tr("Meters (m)"), tr("Feet (ft)") });
    mfl->addRow(tr("Depth unit:"), m_depth_unit_combo);

    vl->addLayout(mfl);

    addSection(vl, tr("Coordinates & Time"), page);
    auto* cfl = makeForm();

    m_coord_format_combo = new QComboBox(page);
    m_coord_format_combo->addItems({
        tr("Decimal Degrees  (DD.ddddd\xC2\xB0)"),
        tr("Degrees, Minutes, Seconds  (DMS)"),
        tr("UTM  (automatic zone)"),
    });
    cfl->addRow(tr("Format:"), m_coord_format_combo);

    m_date_format_combo = new QComboBox(page);
    m_date_format_combo->addItems({
        tr("ISO 8601  (UTC)"),
        tr("Local time"),
    });
    cfl->addRow(tr("Timestamps:"), m_date_format_combo);

    vl->addLayout(cfl);
    vl->addStretch();

    return wrapInScroll(page);
}

QWidget* AppSettingsDialog::buildAppearancePage()
{
    auto* page = new QWidget;
    page->setObjectName("settingsPage");
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(28, Theme::kSpacing7, 28, Theme::kSpacing7);
    vl->setSpacing(0);

    auto* title = new QLabel(tr("Appearance"), page);
    title->setObjectName("settingsPageTitle");
    vl->addWidget(title);
    vl->addSpacing(18);

    addSection(vl, tr("Interface"), page);
    auto* fl = makeForm();

    m_theme_combo = new QComboBox(page);
    m_theme_combo->addItem(tr("Dark  (default)"));
    m_theme_combo->addItem(tr("Light"));
    m_theme_combo->setToolTip(
        tr("Interface theme. Applies immediately.\n"
           "Sonar imagery and the map canvas stay dark in both themes."));
    fl->addRow(tr("Theme:"), m_theme_combo);

    m_density_combo = new QComboBox(page);
    m_density_combo->addItems({
        tr("Compact"),
        tr("Normal  (default)"),
        tr("Comfortable"),
    });
    fl->addRow(tr("UI density:"), m_density_combo);

    m_font_size_combo = new QComboBox(page);
    m_font_size_combo->addItems({ tr("Small"), tr("Medium  (default)"), tr("Large") });
    fl->addRow(tr("Font size:"), m_font_size_combo);

    vl->addLayout(fl);

    addSection(vl, tr("Behaviour"), page);
    auto* bfl = makeForm();

    m_tooltips_check = new QCheckBox(tr("Show tooltips on hover"), page);
    bfl->addRow(QString(), m_tooltips_check);

    m_grid_check = new QCheckBox(tr("Show coordinate grid on map"), page);
    bfl->addRow(QString(), m_grid_check);

    vl->addLayout(bfl);

    auto* restart_hint = new QLabel(
        tr("Theme applies immediately. Density and font-size changes take effect on next launch."), page);
    restart_hint->setObjectName("fieldHint");
    restart_hint->setWordWrap(true);
    vl->addSpacing(10);
    vl->addWidget(restart_hint);

    vl->addStretch();

    return wrapInScroll(page);
}

QWidget* AppSettingsDialog::buildPerformancePage()
{
    auto* page = new QWidget;
    page->setObjectName("settingsPage");
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(28, Theme::kSpacing7, 28, Theme::kSpacing7);
    vl->setSpacing(0);

    auto* title = new QLabel(tr("Performance"), page);
    title->setObjectName("settingsPageTitle");
    vl->addWidget(title);
    vl->addSpacing(18);

    addSection(vl, tr("Processing"), page);
    auto* pfl = makeForm();

    m_workers_spin = new QSpinBox(page);
    m_workers_spin->setRange(1, 16);
    m_workers_spin->setSuffix(tr("  threads"));
    m_workers_spin->setToolTip(tr("Parallel worker threads for pipeline jobs.\n"
                                  "Recommended: equal to the number of physical CPU cores."));
    pfl->addRow(tr("Worker threads:"), m_workers_spin);
    addHint(pfl, tr("Detected: %1 logical cores").arg(QThread::idealThreadCount()), page);

    m_gpu_accel_check = new QCheckBox(tr("Enable GPU acceleration"), page);
    m_gpu_accel_check->setToolTip(tr("Uses the GPU for waterfall rendering and texture uploads.\n"
                                     "Disable if you experience graphics glitches."));
    pfl->addRow(QString(), m_gpu_accel_check);

    vl->addLayout(pfl);

    addSection(vl, tr("Cache"), page);
    auto* cfl = makeForm();

    m_cache_spin = new QSpinBox(page);
    m_cache_spin->setRange(64, 4096);
    m_cache_spin->setSingleStep(64);
    m_cache_spin->setSuffix(tr("  MB"));
    m_cache_spin->setToolTip(tr("Maximum RAM used for map tile and sonar tile caching."));
    cfl->addRow(tr("Tile cache:"), m_cache_spin);
    addHint(cfl, tr("Higher values improve panning performance on large surveys."), page);

    vl->addLayout(cfl);

    addSection(vl, tr("Auto-save"), page);
    auto* afl = makeForm();

    m_autosave_spin = new QSpinBox(page);
    m_autosave_spin->setRange(0, 60);
    m_autosave_spin->setSpecialValueText(tr("Disabled"));
    m_autosave_spin->setSuffix(tr("  min"));
    afl->addRow(tr("Save interval:"), m_autosave_spin);

    vl->addLayout(afl);
    vl->addStretch();

    return wrapInScroll(page);
}

QWidget* AppSettingsDialog::buildDataPage()
{
    auto* page = new QWidget;
    page->setObjectName("settingsPage");
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(28, Theme::kSpacing7, 28, Theme::kSpacing7);
    vl->setSpacing(0);

    auto* title = new QLabel(tr("Data"), page);
    title->setObjectName("settingsPageTitle");
    vl->addWidget(title);
    vl->addSpacing(18);

    addSection(vl, tr("Acoustics"), page);
    auto* afl = makeForm();

    m_sound_vel_spin = new QDoubleSpinBox(page);
    m_sound_vel_spin->setRange(1400.0, 1700.0);
    m_sound_vel_spin->setDecimals(1);
    m_sound_vel_spin->setSingleStep(5.0);
    m_sound_vel_spin->setSuffix(tr("  m/s"));
    m_sound_vel_spin->setToolTip(tr("Default water sound velocity used when the sonar file\n"
                                    "does not embed its own value. Typical seawater: 1500 m/s."));
    afl->addRow(tr("Sound velocity:"), m_sound_vel_spin);
    addHint(afl, tr("Freshwater ≈ 1480 m/s · Seawater ≈ 1500–1520 m/s"), page);

    vl->addLayout(afl);

    addSection(vl, tr("Display Defaults"), page);
    auto* dfl = makeForm();

    m_palette_combo = new QComboBox(page);
    m_palette_combo->addItems({
        tr("Thermal"), tr("Greyscale"), tr("Ocean"), tr("Copper"),
        tr("Inverted"), tr("Viridis"), tr("Plasma"), tr("Midnight"), tr("Sand"), tr("Spectrum")
    });
    dfl->addRow(tr("Default palette:"), m_palette_combo);

    m_auto_stretch = new QCheckBox(tr("Auto-stretch amplitude on load"), page);
    m_auto_stretch->setToolTip(tr("Maps the 1st–99th percentile of amplitude values\n"
                                   "to the full colour range when a layer is loaded."));
    dfl->addRow(QString(), m_auto_stretch);

    vl->addLayout(dfl);

    addSection(vl, tr("Coordinate Reference"), page);
    auto* cfl = makeForm();

    m_crs_combo = new QComboBox(page);
    m_crs_combo->addItems({
        tr("WGS 84  (EPSG:4326)  — default"),
        tr("NAD83   (EPSG:4269)"),
        tr("ETRS89  (EPSG:4258)"),
    });
    m_crs_combo->setToolTip(tr("Default geographic CRS assumed for newly imported data\n"
                                "when no CRS is embedded in the source file."));
    cfl->addRow(tr("Default CRS:"), m_crs_combo);
    addHint(cfl, tr("This can be overridden per-file in the Import dialog."), page);

    vl->addLayout(cfl);
    vl->addStretch();

    return wrapInScroll(page);
}

QWidget* AppSettingsDialog::buildExportPage()
{
    auto* page = new QWidget;
    page->setObjectName("settingsPage");
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(28, Theme::kSpacing7, 28, Theme::kSpacing7);
    vl->setSpacing(0);

    auto* title = new QLabel(tr("Export"), page);
    title->setObjectName("settingsPageTitle");
    vl->addWidget(title);
    vl->addSpacing(18);

    addSection(vl, tr("Output Location"), page);
    auto* ofl = makeForm();

    auto* dir_row = new QWidget(page);
    auto* dhl = makeCompactLayout<QHBoxLayout>(dir_row, Theme::kSpacing2);

    m_export_dir_edit = new QLineEdit(dir_row);
    m_export_dir_edit->setPlaceholderText(tr("Same folder as source file"));
    m_export_dir_edit->setObjectName("dlgLineEdit");
    dhl->addWidget(m_export_dir_edit, 1);

    auto* browse_btn = new QPushButton(tr("Browse…"), dir_row);
    browse_btn->setObjectName("dlgBtnBrowse");
    connect(browse_btn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Choose Export Directory"),
            m_export_dir_edit->text());
        if (!dir.isEmpty()) m_export_dir_edit->setText(dir);
    });
    dhl->addWidget(browse_btn);
    ofl->addRow(tr("Directory:"), dir_row);
    addHint(ofl, tr("Leave blank to save exports alongside the source file."), page);

    vl->addLayout(ofl);

    addSection(vl, tr("Format Defaults"), page);
    auto* ffl = makeForm();

    m_export_fmt_combo = new QComboBox(page);
    m_export_fmt_combo->addItems({
        tr("CSV  (.csv)"),
        tr("GeoJSON  (.geojson)"),
        tr("KMZ  (.kmz)"),
        tr("Shapefile  (.shp)"),
    });
    m_export_fmt_combo->setToolTip(tr("Pre-selected format in the Export dialog.\n"
                                       "You can always change it before each export."));
    ffl->addRow(tr("Contact export:"), m_export_fmt_combo);

    vl->addLayout(ffl);
    vl->addStretch();

    return wrapInScroll(page);
}

QWidget* AppSettingsDialog::buildAboutPage()
{
    auto* page = new QWidget;
    page->setObjectName("settingsPage");
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(28, 28, 28, 28);
    vl->setSpacing(0);

    auto* name_lbl = new QLabel(tr("Dolphin Explorer"), page);
    name_lbl->setObjectName("settingsPageTitle");
    vl->addWidget(name_lbl);
    vl->addSpacing(6);

    const QString ver = QApplication::applicationVersion().isEmpty()
                            ? tr("1.0.0")
                            : QApplication::applicationVersion();
    auto* ver_lbl = new QLabel(
        tr("Version %1  ·  Built %2").arg(ver).arg(__DATE__), page);
    ver_lbl->setObjectName("settingsSectionTitle");
    vl->addWidget(ver_lbl);
    vl->addSpacing(20);

    auto* div = new QFrame(page);
    div->setFrameShape(QFrame::HLine);
    div->setObjectName("settingsDivider");
    vl->addWidget(div);
    vl->addSpacing(16);

    auto* sys_lbl = new QLabel(
        tr("Platform:  %1\nQt:  %2")
            .arg(QSysInfo::prettyProductName())
            .arg(qVersion()),
        page);
    sys_lbl->setObjectName("fieldHint");
    vl->addWidget(sys_lbl);
    vl->addSpacing(20);

    auto* div2 = new QFrame(page);
    div2->setFrameShape(QFrame::HLine);
    div2->setObjectName("settingsDivider");
    vl->addWidget(div2);
    vl->addSpacing(16);

    auto* copy_lbl = new QLabel(
        tr("© 2024–2026  Dolphin Explorer  —  All rights reserved.\n\n"
           "Professional hydrographic survey software for sidescan sonar,\n"
           "sub-bottom profiler, and multibeam data acquisition and analysis.\n\n"
           "Built with Qt 6 and OpenGL."),
        page);
    copy_lbl->setObjectName("aboutCopy");
    copy_lbl->setWordWrap(true);
    vl->addWidget(copy_lbl);

    vl->addStretch();

    return wrapInScroll(page);
}

QWidget* AppSettingsDialog::buildMapPage()
{
    auto* page = new QWidget;
    page->setObjectName("settingsPage");
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(28, Theme::kSpacing7, 28, Theme::kSpacing7);
    vl->setSpacing(0);

    auto* title = new QLabel(tr("Map"), page);
    title->setObjectName("settingsPageTitle");
    vl->addWidget(title);
    vl->addSpacing(18);

    addSection(vl, tr("Viewport"), page);
    auto* vfl = makeForm();

    m_map_bg_combo = new QComboBox(page);
    m_map_bg_combo->setToolTip(tr("Background colour shown in the 2D and 3D map viewports"));
    {
        QPixmap pm(14, 10);
        for (const auto& p : kMapBgPresets) {
            pm.fill(QColor(p.hex));
            m_map_bg_combo->addItem(QIcon(pm), tr(p.name));
        }
    }
    vfl->addRow(tr("Background:"), m_map_bg_combo);

    vl->addLayout(vfl);

    addSection(vl, tr("Grid"), page);
    auto* gfl = makeForm();

    m_map_grid_combo = new QComboBox(page);
    m_map_grid_combo->setToolTip(tr("Grid line colour for the graticule (2D) and reference grid (3D)"));
    {
        QPixmap pm(14, 10);
        for (const auto& p : kMapGridPresets) {
            pm.fill(QColor(p.major3d));
            m_map_grid_combo->addItem(QIcon(pm), tr(p.name));
        }
    }
    gfl->addRow(tr("Grid lines:"), m_map_grid_combo);

    m_grid_label_size_combo = new QComboBox(page);
    m_grid_label_size_combo->addItems({
        tr("Small  (7 pt)"),
        tr("Normal  (8 pt, default)"),
        tr("Large  (10 pt)"),
    });
    m_grid_label_size_combo->setToolTip(tr("Point size of the coordinate labels on the graticule grid."));
    gfl->addRow(tr("Label size:"), m_grid_label_size_combo);

    m_grid_label_rotated_check = new QCheckBox(tr("Rotate latitude labels 90°"), page);
    m_grid_label_rotated_check->setToolTip(
        tr("Draw latitude coordinate labels vertically along the left map edge."));
    gfl->addRow(QString(), m_grid_label_rotated_check);

    m_grat_coord_combo = new QComboBox(page);
    m_grat_coord_combo->addItems({
        tr("Auto  (match data CRS)"),
        tr("Degrees  (Lat / Lon)"),
        tr("Easting / Northing  (UTM)"),
        tr("Both  (degrees + E/N)"),
    });
    m_grat_coord_combo->setToolTip(
        tr("Coordinate format shown on the graticule grid labels.\n"
           "\"Easting / Northing\" and \"Both\" convert geographic data to UTM on the fly.\n"
           "For data already in a projected CRS, the projected values are always shown."));
    gfl->addRow(tr("Coord labels:"), m_grat_coord_combo);

    addHint(gfl, tr("Dark backgrounds give the best sonar mosaic contrast; light backgrounds suit "
                    "chart-style review. Changes take effect immediately."), page);
    vl->addLayout(gfl);

    vl->addStretch();

    return wrapInScroll(page);
}

} // namespace dolphin::ui
