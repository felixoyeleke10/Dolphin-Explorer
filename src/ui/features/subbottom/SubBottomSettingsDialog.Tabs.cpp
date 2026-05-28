// SubBottomSettingsDialog.Tabs.cpp — Display, Acquisition, ViewScale, Crosshair, Grid tab builders.
#include "ui/features/subbottom/SubBottomSettingsDialog.h"
#include "ui/features/subbottom/SubBottomPalette.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {

QGroupBox* makeGroup(const QString& title, QWidget* parent)
{
    return new QGroupBox(title, parent);
}

QFormLayout* makeForm(QGroupBox* g)
{
    auto* f = new QFormLayout(g);
    f->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    f->setSpacing(Theme::kSpacing3);
    f->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3, Theme::kSpacing4, 10);
    return f;
}

} // namespace

void SubBottomSettingsDialog::buildDisplayTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    vbox->setSpacing(10);

    auto* g1 = makeGroup(tr("Image Rendering"), page);
    auto* f1 = makeForm(g1);

    m_palette_combo = new QComboBox(this);
    for (int i = 0; i < SbpPalette::Count; ++i)
        m_palette_combo->addItem(SbpPalette::name(i));
    m_palette_combo->setToolTip(
        tr("Colour palette applied to normalised trace amplitudes.\n"
           "• Greyscale — amplitude → luminance (industry standard)\n"
           "• Inverted Grey — reversed; black = peak energy\n"
           "• Seismic — positive polarity red, negative polarity blue\n"
           "• Thermal — black → purple → orange → yellow (high dynamic range)"));
    f1->addRow(tr("Palette:"), m_palette_combo);

    m_gain_spin = new QDoubleSpinBox(this);
    m_gain_spin->setRange(0.1, 20.0);
    m_gain_spin->setSingleStep(0.1);
    m_gain_spin->setDecimals(1);
    m_gain_spin->setToolTip(
        tr("Linear amplitude multiplier applied before palette mapping.\n"
           "1.0 = as-recorded amplitude.  Increase to brighten deep weak\n"
           "reflectors; very high values will saturate strong returns."));
    f1->addRow(tr("Gain:"), m_gain_spin);
    vbox->addWidget(g1);

    auto* g2 = makeGroup(tr("Bottom Track Overlay"), page);
    auto* f2 = makeForm(g2);

    m_bt_check = new QCheckBox(tr("Show bottom track stripe"), this);
    m_bt_check->setToolTip(
        tr("Draw the pre-computed seabed pick as a horizontal stripe\n"
           "at the water-bottom two-way travel time."));
    f2->addRow(QString{}, m_bt_check);

    m_bt_color_btn = new QPushButton(this);
    m_bt_color_btn->setFixedHeight(Theme::kColorBtnH);
    m_bt_color_btn->setToolTip(tr("Click to choose the bottom track stripe colour."));
    connect(m_bt_color_btn, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_bt_color, this, tr("Bottom Track Colour"));
        if (c.isValid()) { m_bt_color = c; updateColorButton(m_bt_color_btn, c); }
    });
    f2->addRow(tr("Colour:"), m_bt_color_btn);

    m_bt_thickness_spin = new QSpinBox(this);
    m_bt_thickness_spin->setRange(1, 8);
    m_bt_thickness_spin->setSuffix(tr(" px"));
    m_bt_thickness_spin->setToolTip(
        tr("Height of the stripe drawn at the seabed pick position (pixels)."));
    f2->addRow(tr("Thickness:"), m_bt_thickness_spin);
    vbox->addWidget(g2);

    vbox->addStretch();
    tabs->addTab(page, tr("Display"));
}

void SubBottomSettingsDialog::buildAcquisitionTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    vbox->setSpacing(10);

    auto* g = makeGroup(tr("Acoustic Parameters"), page);
    auto* f = makeForm(g);

    m_speed_spin = new QDoubleSpinBox(this);
    m_speed_spin->setRange(1400.0, 1700.0);
    m_speed_spin->setSingleStep(5.0);
    m_speed_spin->setDecimals(0);
    m_speed_spin->setSuffix(tr(" m/s"));
    m_speed_spin->setToolTip(
        tr("Two-way round-trip acoustic speed in the water column.\n"
           "Used to convert two-way travel time to depth:\n"
           "    depth [m] = time [s] × speed [m/s] ÷ 2\n\n"
           "Typical values:\n"
           "  Fresh water  ~1480 m/s\n"
           "  Temperate sea ~1500 m/s\n"
           "  Warm coastal  ~1530 m/s\n"
           "  Deep ocean    ~1510 m/s\n\n"
           "Affects depth readouts in the status bar and inspector panel."));
    f->addRow(tr("Sound speed:"), m_speed_spin);

    auto* note = new QLabel(
        tr("<small><i>Sound speed affects depth display only. "
           "Raw two-way travel time data is unchanged.</i></small>"), page);
    note->setWordWrap(true);
    note->setObjectName("dlgNote");

    vbox->addWidget(g);
    vbox->addWidget(note);
    vbox->addStretch();
    tabs->addTab(page, tr("Acquisition"));
}

void SubBottomSettingsDialog::buildViewScaleTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    vbox->setSpacing(10);

    auto* g = makeGroup(tr("Spatial Scale"), page);
    auto* f = makeForm(g);

    m_trace_w_spin = new QSpinBox(this);
    m_trace_w_spin->setRange(1, 20);
    m_trace_w_spin->setSuffix(tr(" px/trace"));
    m_trace_w_spin->setToolTip(
        tr("Horizontal pixels per trace column (along-track zoom).\n"
           "Increasing this stretches the section horizontally so\n"
           "individual traces are more distinguishable.\n\n"
           "Ctrl+scroll in the view also adjusts this interactively."));
    f->addRow(tr("Trace width:"), m_trace_w_spin);

    m_depth_s_spin = new QDoubleSpinBox(this);
    m_depth_s_spin->setRange(0.05, 5.0);
    m_depth_s_spin->setSingleStep(0.05);
    m_depth_s_spin->setDecimals(2);
    m_depth_s_spin->setSuffix(tr(" px/sample"));
    m_depth_s_spin->setToolTip(
        tr("Vertical pixels per depth sample (depth-axis zoom).\n"
           "Increasing this stretches the depth axis — useful for\n"
           "examining shallow sub-bottom structure in detail.\n\n"
           "Shift+scroll in the view also adjusts this interactively."));
    f->addRow(tr("Depth scale:"), m_depth_s_spin);

    auto* hint = new QLabel(
        tr("<small><i>These settings can also be adjusted interactively "
           "with Ctrl+scroll (trace width) and Shift+scroll (depth scale) "
           "directly in the view.</i></small>"), page);
    hint->setWordWrap(true);
    hint->setObjectName("dlgNote");

    vbox->addWidget(g);
    vbox->addWidget(hint);
    vbox->addStretch();
    tabs->addTab(page, tr("View Scale"));
}

void SubBottomSettingsDialog::buildCrosshairTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    vbox->setSpacing(10);

    auto* g = makeGroup(tr("Cursor Crosshair"), page);
    auto* f = makeForm(g);

    m_xhair_show_check = new QCheckBox(tr("Show crosshair"), this);
    m_xhair_show_check->setToolTip(
        tr("Draw horizontal and vertical guide lines following the cursor."));
    f->addRow(QString{}, m_xhair_show_check);

    m_xhair_color_btn = new QPushButton(this);
    m_xhair_color_btn->setFixedHeight(Theme::kColorBtnH);
    m_xhair_color_btn->setToolTip(tr("Click to choose the crosshair line colour."));
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
        tr("Line pattern used to draw the crosshair.\n"
           "Dash is the traditional seismic display style."));
    f->addRow(tr("Line style:"), m_xhair_style_combo);

    m_xhair_width_spin = new QSpinBox(this);
    m_xhair_width_spin->setRange(1, 4);
    m_xhair_width_spin->setSuffix(tr(" px"));
    m_xhair_width_spin->setToolTip(
        tr("Stroke width of the crosshair lines in display pixels."));
    f->addRow(tr("Width:"), m_xhair_width_spin);

    m_xhair_opacity_spin = new QSpinBox(this);
    m_xhair_opacity_spin->setRange(5, 100);
    m_xhair_opacity_spin->setSuffix(tr(" %"));
    m_xhair_opacity_spin->setToolTip(
        tr("Opacity of the crosshair overlay (5–100 %).\n"
           "Lower values keep the crosshair subtle so it does not\n"
           "obscure the seismic data beneath it."));
    f->addRow(tr("Opacity:"), m_xhair_opacity_spin);

    vbox->addWidget(g);
    vbox->addStretch();
    tabs->addTab(page, tr("Crosshair"));
}

void SubBottomSettingsDialog::buildGridTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    vbox->setSpacing(10);

    auto* g = makeGroup(tr("Depth-Time Grid"), page);
    auto* f = makeForm(g);

    m_grid_show_check = new QCheckBox(tr("Show depth grid"), this);
    m_grid_show_check->setToolTip(
        tr("Draw horizontal reference lines at regular two-way travel-time intervals.\n"
           "Useful for estimating reflector depth without a scale ruler."));
    f->addRow(QString{}, m_grid_show_check);

    m_grid_color_btn = new QPushButton(this);
    m_grid_color_btn->setFixedHeight(Theme::kColorBtnH);
    m_grid_color_btn->setToolTip(tr("Click to choose the depth grid line colour."));
    connect(m_grid_color_btn, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_grid_color, this, tr("Grid Colour"));
        if (c.isValid()) { m_grid_color = c; updateColorButton(m_grid_color_btn, c); }
    });
    f->addRow(tr("Colour:"), m_grid_color_btn);

    m_grid_opacity_spin = new QSpinBox(this);
    m_grid_opacity_spin->setRange(5, 100);
    m_grid_opacity_spin->setSuffix(tr(" %"));
    m_grid_opacity_spin->setToolTip(
        tr("Grid line opacity as a percentage of full opacity."));
    f->addRow(tr("Opacity:"), m_grid_opacity_spin);

    m_grid_interval_spin = new QDoubleSpinBox(this);
    m_grid_interval_spin->setRange(0.0, 2000.0);
    m_grid_interval_spin->setSingleStep(1.0);
    m_grid_interval_spin->setDecimals(1);
    m_grid_interval_spin->setSuffix(tr(" ms"));
    m_grid_interval_spin->setSpecialValueText(tr("Auto"));
    m_grid_interval_spin->setToolTip(
        tr("Two-way travel-time spacing between grid lines in milliseconds.\n"
           "Set to 0 (Auto) to let the software choose an appropriate interval\n"
           "based on the current depth-scale so approximately 6–8 lines are visible."));
    f->addRow(tr("Interval:"), m_grid_interval_spin);

    auto* note = new QLabel(
        tr("<small><i>Grid line positions are computed in two-way travel time (ms). "
           "Conversion to depth depends on the sound speed set in the Acquisition tab.</i></small>"),
        page);
    note->setWordWrap(true);
    note->setObjectName("dlgNote");

    vbox->addWidget(g);
    vbox->addWidget(note);
    vbox->addStretch();
    tabs->addTab(page, tr("Grid"));
}

} // namespace dolphin::ui
