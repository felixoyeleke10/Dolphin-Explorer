// WaterfallSettingsDialog.cpp — professional tabbed settings for WaterfallWindow.

#include "ui/features/waterfall/WaterfallSettingsDialog.h"
#include "render/sonar/SSSPalette.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kMinW = 460;
static constexpr int kMinH = 380;

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------

static QGroupBox* makeGroup(const QString& title, QWidget* parent)
{
    auto* g = new QGroupBox(title, parent);
    return g;
}

static QFormLayout* makeForm(QGroupBox* g)
{
    auto* f = new QFormLayout(g);
    f->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    f->setSpacing(Theme::kSpacing3);
    f->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3, Theme::kSpacing4, 10);
    return f;
}

// -----------------------------------------------------------------------------
//  Construction
// -----------------------------------------------------------------------------

WaterfallSettingsDialog::WaterfallSettingsDialog(const Settings& cur, QWidget* parent)
    : QDialog(parent)
    , m_xhair_color(cur.overlay.xhair_color)
    , m_grid_color (cur.overlay.grid_color)
{
    setWindowTitle(tr("Sidescan Sonar Settings"));
    setMinimumWidth(kMinW);
    setMinimumHeight(kMinH);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4);

    auto* tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    root->addWidget(tabs, 1);

    buildDisplayTab(tabs);
    buildPerformanceTab(tabs);
    buildCrosshairTab(tabs);
    buildGridTab(tabs);

    // Populate from current settings
    m_channel_combo->setCurrentIndex(static_cast<int>(cur.display_channel));
    m_amp_bar_check->setChecked(cur.show_amp_bar);
    m_window_size_spin->setValue(cur.window_size);
    m_xhair_show_check->setChecked(cur.overlay.xhair_show);
    updateColorButton(m_xhair_color_btn, m_xhair_color);
    m_xhair_style_combo->setCurrentIndex(
        [&] {
            switch (cur.overlay.xhair_style) {
            case Qt::SolidLine:    return 0;
            case Qt::DashLine:     return 1;
            case Qt::DotLine:      return 2;
            case Qt::DashDotLine:  return 3;
            default:               return 0;
            }
        }());
    m_xhair_width_spin->setValue(cur.overlay.xhair_width);
    m_xhair_opacity_spin->setValue(cur.overlay.xhair_opacity);
    m_grid_show_check->setChecked(cur.overlay.grid_show);
    updateColorButton(m_grid_color_btn, m_grid_color);
    m_grid_opacity_spin->setValue(cur.overlay.grid_opacity);
    m_grid_interval_spin->setValue(static_cast<double>(cur.overlay.grid_interval_m));

    // -- Buttons -----------------------------------------------------------
    root->addSpacing(8);
    auto* bbox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bbox->button(QDialogButtonBox::Ok)->setObjectName("dlgBtnOk");
    auto* apply_btn = bbox->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
    root->addWidget(bbox);

    connect(apply_btn, &QPushButton::clicked, this, &WaterfallSettingsDialog::onApply);
    connect(bbox, &QDialogButtonBox::accepted, this, [this] { onApply(); accept(); });
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// -----------------------------------------------------------------------------
//  Tab builders
// -----------------------------------------------------------------------------

void WaterfallSettingsDialog::buildDisplayTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    vbox->setSpacing(10);

    // -- Channel -----------------------------------------------------------
    auto* g1 = makeGroup(tr("Sonar Channel"), page);
    auto* f1 = makeForm(g1);

    m_channel_combo = new QComboBox(this);
    m_channel_combo->addItem(tr("Both channels"),
                              static_cast<int>(DisplayChannel::Both));
    m_channel_combo->addItem(tr("Port only"),
                              static_cast<int>(DisplayChannel::Port));
    m_channel_combo->addItem(tr("Starboard only"),
                              static_cast<int>(DisplayChannel::Starboard));
    m_channel_combo->setToolTip(
        tr("Which sonar transducer channel(s) to render in the waterfall.\n\n"
           "• Both — full swath; port nadir at centre-left, stbd at centre-right\n"
           "• Port only — hides the starboard half-swath\n"
           "• Starboard only — hides the port half-swath\n\n"
           "Takes effect immediately on Apply."));
    f1->addRow(tr("Display channel:"), m_channel_combo);
    vbox->addWidget(g1);

    // -- Overlays ----------------------------------------------------------
    auto* g2 = makeGroup(tr("Display Overlays"), page);
    auto* f2 = makeForm(g2);

    m_amp_bar_check = new QCheckBox(tr("Show amplitude profile bar"), this);
    m_amp_bar_check->setToolTip(
        tr("Display the per-column mean amplitude chart at the bottom of the waterfall.\n"
           "Useful for diagnosing system sensitivity and identifying AGC anomalies.\n"
           "Can also be toggled from the Inspector panel."));
    f2->addRow(QString{}, m_amp_bar_check);
    vbox->addWidget(g2);

    vbox->addStretch();
    tabs->addTab(page, tr("Display"));
}

void WaterfallSettingsDialog::buildPerformanceTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    vbox->setSpacing(10);

    auto* g = makeGroup(tr("Data Window"), page);
    auto* f = makeForm(g);

    m_window_size_spin = new QSpinBox(this);
    m_window_size_spin->setRange(500, 20000);
    m_window_size_spin->setSingleStep(500);
    m_window_size_spin->setSuffix(tr(" pings"));
    m_window_size_spin->setToolTip(
        tr("Number of sidescan pings loaded per view window.\n\n"
           "Larger window = more survey history visible at once, higher RAM usage.\n"
           "Smaller window = faster loads, less memory, snappier scrolling.\n\n"
           "Recommended: 2000–6000 for typical surveys.\n"
           "Takes effect on the next window load or file open."));
    f->addRow(tr("Window size:"), m_window_size_spin);

    auto* note = new QLabel(
        tr("<small><i>Window changes take effect the next time a survey window loads. "
           "The current view is not affected until you scroll or open a new file.</i></small>"),
        page);
    note->setWordWrap(true);
    note->setObjectName("dlgNote");

    vbox->addWidget(g);
    vbox->addWidget(note);
    vbox->addStretch();
    tabs->addTab(page, tr("Performance"));
}

void WaterfallSettingsDialog::buildCrosshairTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    vbox->setSpacing(10);

    auto* g = makeGroup(tr("Cursor Crosshair"), page);
    auto* f = makeForm(g);

    m_xhair_show_check = new QCheckBox(tr("Show crosshair"), this);
    m_xhair_show_check->setToolTip(
        tr("Draw horizontal and vertical guide lines following the cursor across\n"
           "the waterfall image for precise range and position identification."));
    f->addRow(QString{}, m_xhair_show_check);

    m_xhair_color_btn = new QPushButton(this);
    m_xhair_color_btn->setFixedHeight(Theme::kColorBtnH);
    m_xhair_color_btn->setToolTip(tr("Click to choose the crosshair colour."));
    connect(m_xhair_color_btn, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_xhair_color, this, tr("Crosshair Colour"));
        if (c.isValid()) { m_xhair_color = c; updateColorButton(m_xhair_color_btn, c); }
    });
    f->addRow(tr("Colour:"), m_xhair_color_btn);

    m_xhair_style_combo = new QComboBox(this);
    m_xhair_style_combo->addItem(tr("Solid"),    static_cast<int>(Qt::SolidLine));
    m_xhair_style_combo->addItem(tr("Dash"),     static_cast<int>(Qt::DashLine));
    m_xhair_style_combo->addItem(tr("Dot"),      static_cast<int>(Qt::DotLine));
    m_xhair_style_combo->addItem(tr("Dash-Dot"), static_cast<int>(Qt::DashDotLine));
    m_xhair_style_combo->setToolTip(
        tr("Stroke pattern for the crosshair lines.\n"
           "Solid is most visible; Dash works well on dark backgrounds."));
    f->addRow(tr("Line style:"), m_xhair_style_combo);

    m_xhair_width_spin = new QSpinBox(this);
    m_xhair_width_spin->setRange(1, 4);
    m_xhair_width_spin->setSuffix(tr(" px"));
    m_xhair_width_spin->setToolTip(
        tr("Stroke width of the crosshair lines in display pixels.\n"
           "1 px is standard; increase to 2 px on high-DPI displays."));
    f->addRow(tr("Width:"), m_xhair_width_spin);

    m_xhair_opacity_spin = new QSpinBox(this);
    m_xhair_opacity_spin->setRange(5, 100);
    m_xhair_opacity_spin->setSuffix(tr(" %"));
    m_xhair_opacity_spin->setToolTip(
        tr("Opacity of the crosshair overlay (5–100 %).\n"
           "Lower values let sonar data show through; higher values make\n"
           "the crosshair more prominent for detailed contact work."));
    f->addRow(tr("Opacity:"), m_xhair_opacity_spin);

    auto* note = new QLabel(
        tr("<small><i>The horizontal line is rendered at 75% of the set opacity "
           "to maintain the visual hierarchy typical of SSS display convention.</i></small>"),
        page);
    note->setWordWrap(true);
    note->setObjectName("dlgNote");

    vbox->addWidget(g);
    vbox->addWidget(note);
    vbox->addStretch();
    tabs->addTab(page, tr("Crosshair"));
}

void WaterfallSettingsDialog::buildGridTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    vbox->setSpacing(10);

    auto* g = makeGroup(tr("Slant-Range Grid"), page);
    auto* f = makeForm(g);

    m_grid_show_check = new QCheckBox(tr("Show range grid"), this);
    m_grid_show_check->setToolTip(
        tr("Draw vertical reference lines at constant slant-range intervals across\n"
           "the waterfall on both port and starboard sides of nadir.\n"
           "Useful for quick range estimation without consulting the scale bar."));
    f->addRow(QString{}, m_grid_show_check);

    m_grid_color_btn = new QPushButton(this);
    m_grid_color_btn->setFixedHeight(Theme::kColorBtnH);
    m_grid_color_btn->setToolTip(tr("Click to choose the range grid line colour."));
    connect(m_grid_color_btn, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_grid_color, this, tr("Grid Colour"));
        if (c.isValid()) { m_grid_color = c; updateColorButton(m_grid_color_btn, c); }
    });
    f->addRow(tr("Colour:"), m_grid_color_btn);

    m_grid_opacity_spin = new QSpinBox(this);
    m_grid_opacity_spin->setRange(5, 100);
    m_grid_opacity_spin->setSuffix(tr(" %"));
    m_grid_opacity_spin->setToolTip(
        tr("Opacity of the range grid lines as a percentage.\n"
           "Keep below 30 % to avoid obscuring the sonar image."));
    f->addRow(tr("Opacity:"), m_grid_opacity_spin);

    m_grid_interval_spin = new QDoubleSpinBox(this);
    m_grid_interval_spin->setRange(0.0, 5000.0);
    m_grid_interval_spin->setSingleStep(5.0);
    m_grid_interval_spin->setDecimals(1);
    m_grid_interval_spin->setSuffix(tr(" m"));
    m_grid_interval_spin->setSpecialValueText(tr("Auto"));
    m_grid_interval_spin->setToolTip(
        tr("Slant-range spacing between grid lines in metres.\n"
           "Set to 0 (Auto) to let the software choose a clean interval\n"
           "based on the swath width so approximately 4–6 lines are visible."));
    f->addRow(tr("Interval:"), m_grid_interval_spin);

    auto* note = new QLabel(
        tr("<small><i>Grid lines are labelled in metres of slant range. "
           "The labels appear at the top of the waterfall image, just below "
           "the existing scale bar.</i></small>"),
        page);
    note->setWordWrap(true);
    note->setObjectName("dlgNote");

    vbox->addWidget(g);
    vbox->addWidget(note);
    vbox->addStretch();
    tabs->addTab(page, tr("Grid"));
}

// -----------------------------------------------------------------------------
//  Read-back
// -----------------------------------------------------------------------------

WaterfallSettingsDialog::Settings WaterfallSettingsDialog::currentSettings() const
{
    Settings s;
    s.window_size     = m_window_size_spin->value();
    s.display_channel = static_cast<DisplayChannel>(m_channel_combo->currentIndex());
    s.show_amp_bar    = m_amp_bar_check->isChecked();

    s.overlay.xhair_show    = m_xhair_show_check->isChecked();
    s.overlay.xhair_color   = m_xhair_color;
    s.overlay.xhair_style   = static_cast<Qt::PenStyle>(
        m_xhair_style_combo->currentData().toInt());
    s.overlay.xhair_width   = m_xhair_width_spin->value();
    s.overlay.xhair_opacity = m_xhair_opacity_spin->value();

    s.overlay.grid_show       = m_grid_show_check->isChecked();
    s.overlay.grid_color      = m_grid_color;
    s.overlay.grid_opacity    = m_grid_opacity_spin->value();
    s.overlay.grid_interval_m = static_cast<float>(m_grid_interval_spin->value());

    return s;
}

// -----------------------------------------------------------------------------
//  Colour button swatch helper
// -----------------------------------------------------------------------------

// static
void WaterfallSettingsDialog::updateColorButton(QPushButton* btn, const QColor& c)
{
    btn->setStyleSheet(colorPickerSheet(c));
    btn->setText(c.name().toUpper());
}

// -----------------------------------------------------------------------------
//  Persist / Apply
// -----------------------------------------------------------------------------

void WaterfallSettingsDialog::onApply()
{
    const Settings s = currentSettings();
    QSettings qs;
    qs.setValue(kKeyWindowSize,     s.window_size);
    qs.setValue(kKeyDisplayChannel, static_cast<int>(s.display_channel));
    qs.setValue(kKeyShowAmpBar,     s.show_amp_bar);
    qs.setValue(kKeyXhairShow,      s.overlay.xhair_show);
    qs.setValue(kKeyXhairColor,     s.overlay.xhair_color.name());
    qs.setValue(kKeyXhairStyle,     static_cast<int>(s.overlay.xhair_style));
    qs.setValue(kKeyXhairWidth,     s.overlay.xhair_width);
    qs.setValue(kKeyXhairOpacity,   s.overlay.xhair_opacity);
    qs.setValue(kKeyGridShow,       s.overlay.grid_show);
    qs.setValue(kKeyGridColor,      s.overlay.grid_color.name());
    qs.setValue(kKeyGridOpacity,    s.overlay.grid_opacity);
    qs.setValue(kKeyGridInterval,   static_cast<double>(s.overlay.grid_interval_m));
    emit applied(s);
}

// -----------------------------------------------------------------------------
//  loadDefaults — restores persisted settings for WaterfallWindow constructor
// -----------------------------------------------------------------------------

// static
WaterfallSettingsDialog::Settings WaterfallSettingsDialog::loadDefaults()
{
    QSettings qs;
    Settings s;
    s.window_size     = qs.value(kKeyWindowSize,     4000).toInt();
    s.display_channel = static_cast<DisplayChannel>(
        qs.value(kKeyDisplayChannel, static_cast<int>(DisplayChannel::Both)).toInt());
    s.show_amp_bar    = qs.value(kKeyShowAmpBar, true).toBool();

    s.overlay.xhair_show    = qs.value(kKeyXhairShow,    true).toBool();
    s.overlay.xhair_color   = QColor(qs.value(kKeyXhairColor,
                                               QStringLiteral("#ffffff")).toString());
    s.overlay.xhair_style   = static_cast<Qt::PenStyle>(
        qs.value(kKeyXhairStyle, static_cast<int>(Qt::SolidLine)).toInt());
    s.overlay.xhair_width   = qs.value(kKeyXhairWidth,   1).toInt();
    s.overlay.xhair_opacity = qs.value(kKeyXhairOpacity, 22).toInt();

    s.overlay.grid_show      = qs.value(kKeyGridShow,    false).toBool();
    s.overlay.grid_color     = QColor(qs.value(kKeyGridColor,
                                                QStringLiteral("#ffffff")).toString());
    s.overlay.grid_opacity   = qs.value(kKeyGridOpacity, 20).toInt();
    s.overlay.grid_interval_m = static_cast<float>(
        qs.value(kKeyGridInterval, 0.0).toDouble());

    return s;
}

} // namespace dolphin::ui
