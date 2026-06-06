// SidescanCorrectionDialog.cpp — constructor, headingParams, and slots.
//
// Section builders + static helpers  → SidescanCorrectionDialog.Layout.cpp
#include "ui/features/map/sidescan/SidescanCorrectionDialog.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace dolphin::ui {

static constexpr int kMinW      = 460;
static constexpr int kMinH      = 400;
static constexpr int kInitW     = 480;
static constexpr int kInitNavH  = 420;  // navigation mode
static constexpr int kInitGeoH  = 600;  // georef mode

SidescanCorrectionDialog::SidescanCorrectionDialog(Mode           mode,
                                                   const QString& layer_name,
                                                   QWidget*       parent)
    : QDialog(parent, Qt::Dialog)
    , m_mode(mode)
{
    const bool is_nav = (mode == Mode::Navigation);
    setWindowTitle(is_nav ? tr("Navigation Processing")
                          : tr("Navigation / Georeference"));
    setMinimumSize(kMinW, kMinH);
    resize(kInitW, is_nav ? kInitNavH : kInitGeoH);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // -- Header ------------------------------------------------------------
    {
        auto* hdr = new QFrame(this);
        hdr->setObjectName("dlgHeader");
        auto* hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(Theme::kSpacing7, Theme::kSpacing6, Theme::kSpacing7, Theme::kSpacing5);

        auto* vl = new QVBoxLayout;
        vl->setSpacing(2);

        auto* title = new QLabel(is_nav ? tr("Navigation Processing")
                                        : tr("Navigation / Georeference"), hdr);
        title->setObjectName("dlgTitle");

        auto* sub = new QLabel(is_nav
            ? tr("Apply position offsets and timing corrections to the survey track.")
            : tr("Control how nav positions and headings are resolved for map building."),
            hdr);
        sub->setObjectName("dlgSubtitle");
        sub->setWordWrap(true);

        vl->addWidget(title);
        vl->addWidget(sub);
        hl->addLayout(vl, 1);
        root->addWidget(hdr);
    }

    // -- Divider -----------------------------------------------------------
    {
        auto* div = new QFrame(this);
        div->setFixedHeight(Theme::kSepSz);
        div->setObjectName("dlgDivider");
        root->addWidget(div);
    }

    // -- Scrollable body ---------------------------------------------------
    auto* body_widget = new QWidget;
    body_widget->setObjectName("dlgBody");
    auto* bl = new QVBoxLayout(body_widget);
    bl->setContentsMargins(Theme::kSpacing7, Theme::kSpacing6, Theme::kSpacing7, Theme::kSpacing4);
    bl->setSpacing(Theme::kSpacing5);

    buildLayerSection(bl, layer_name);

    if (is_nav) {
        buildNavigationSection(bl);
    } else {
        buildNavSourceSection(bl);
        buildHeadingSourceSection(bl);
        buildLaybackSection(bl);
        buildSmoothingSection(bl);
        buildManualOffsetSection(bl);
        buildDebugSection(bl);
    }

    buildScopeRow(bl);
    bl->addStretch();

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(body_widget);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll, 1);

    // -- Footer ------------------------------------------------------------
    {
        auto* foot = new QFrame(this);
        foot->setObjectName("dlgFooter");
        auto* fl = new QHBoxLayout(foot);
        fl->setContentsMargins(Theme::kSpacing7, Theme::kSpacing4, Theme::kSpacing7, Theme::kSpacing5);
        fl->setSpacing(Theme::kSpacing3);

        auto* btn_close = new QPushButton(tr("Close"), foot);
        btn_close->setObjectName("dlgBtnSecondary");
        btn_close->setFixedHeight(Theme::kDialogBtnH);
        btn_close->setMinimumWidth(80);
        btn_close->setToolTip(tr("Close this dialog. Any unapplied changes are discarded."));

        if (!is_nav) {
            m_btn_reset = new QPushButton(tr("Reset to Raw"), foot);
            m_btn_reset->setObjectName("dlgBtnSecondary");
            m_btn_reset->setFixedHeight(Theme::kDialogBtnH);
            m_btn_reset->setMinimumWidth(100);
            m_btn_reset->setToolTip(tr(
                "Restore all parameters to their default values and rebuild the map.\n"
                "Use to undo all corrections and return to the as-recorded data."));

            m_btn_preview = new QPushButton(tr("Preview"), foot);
            m_btn_preview->setObjectName("dlgBtnSecondary");
            m_btn_preview->setFixedHeight(Theme::kDialogBtnH);
            m_btn_preview->setMinimumWidth(80);
            m_btn_preview->setToolTip(tr(
                "Apply the current settings to the active layer and rebuild the map\n"
                "immediately — without closing the dialog. Use to iterate corrections\n"
                "while watching the live map result."));
        }

        m_btn_apply = new QPushButton(is_nav ? tr("Apply") : tr("Apply to Line"), foot);
        m_btn_apply->setObjectName("dlgBtnPrimary");
        m_btn_apply->setFixedHeight(Theme::kDialogBtnH);
        m_btn_apply->setMinimumWidth(110);
        m_btn_apply->setDefault(true);
        m_btn_apply->setToolTip(is_nav
            ? tr("Apply the position and timing corrections and rebuild the map.")
            : tr("Apply the selected nav / heading corrections to the layer scope\n"
                 "chosen above and rebuild the map display."));

        fl->addStretch(1);
        fl->addWidget(btn_close);
        if (m_btn_reset)   fl->addWidget(m_btn_reset);
        if (m_btn_preview) fl->addWidget(m_btn_preview);
        fl->addWidget(m_btn_apply);
        root->addWidget(foot);

        connect(btn_close,    &QPushButton::clicked, this, &QDialog::close);
        connect(m_btn_apply,  &QPushButton::clicked, this, &SidescanCorrectionDialog::onApply);
        if (m_btn_preview)
            connect(m_btn_preview, &QPushButton::clicked,
                    this, &SidescanCorrectionDialog::onPreview);
        if (m_btn_reset)
            connect(m_btn_reset, &QPushButton::clicked,
                    this, &SidescanCorrectionDialog::onReset);
    }
}

// -- headingParams -------------------------------------------------------------

SssGeorefParams SidescanCorrectionDialog::headingParams() const
{
    SssGeorefParams p;

    if (m_nav_source) {
        switch (m_nav_source->currentIndex()) {
        case 1:  p.nav_source = SssNavPositionSource::FishSensor;    break;
        case 2:  p.nav_source = SssNavPositionSource::VesselShip;    break;
        case 3:  p.nav_source = SssNavPositionSource::VesselLayback; break;
        case 4:  p.nav_source = SssNavPositionSource::ManualOffset;  break;
        default: p.nav_source = SssNavPositionSource::Auto;          break;
        }
    }

    if (m_heading_source) {
        switch (m_heading_source->currentIndex()) {
        case 1:  p.heading_source = SssHeadingSource::FishSensor;               break;
        case 2:  p.heading_source = SssHeadingSource::VesselShip;               break;
        case 3:  p.heading_source = SssHeadingSource::CourseOverGround;         break;
        case 4:  p.heading_source = SssHeadingSource::SmoothedCourseOverGround; break;
        default: p.heading_source = SssHeadingSource::Auto;                     break;
        }
    }
    if (m_hdg_offset)     p.heading_offset_deg  = m_hdg_offset->value();
    if (m_swap_channels)  p.swap_port_starboard  = m_swap_channels->isChecked();

    if (m_enable_layback)      p.enable_layback   = m_enable_layback->isChecked();
    if (m_use_file_layback)    p.use_file_layback = m_use_file_layback->isChecked();
    if (m_manual_layback_dist) p.manual_layback_m = m_manual_layback_dist->value();

    if (m_smoothing_mode) {
        switch (m_smoothing_mode->currentIndex()) {
        case 0:  p.smoothing_mode = SssNavSmoothingMode::Off;            break;
        case 1:  p.smoothing_mode = SssNavSmoothingMode::SpikeRejection; break;
        case 2:  p.smoothing_mode = SssNavSmoothingMode::MovingAverage;  break;
        case 3:  p.smoothing_mode = SssNavSmoothingMode::Median;         break;
        default: p.smoothing_mode = SssNavSmoothingMode::Off;            break;
        }
    }
    if (m_smoothing_window) p.smoothing_window = m_smoothing_window->value();

    if (m_x_offset) p.x_offset_m = m_x_offset->value();
    if (m_y_offset) p.y_offset_m = m_y_offset->value();

    if (m_debug_ping_lines) p.debug_ping_lines_only = m_debug_ping_lines->isChecked();

    return p;
}

// -- Slots ---------------------------------------------------------------------

void SidescanCorrectionDialog::onApply()
{
    const ApplyScope scope = (m_scope_combo && m_scope_combo->currentIndex() == 1)
        ? ApplyScope::AllLayers
        : ApplyScope::SelectedLayer;
    emit applyRequested(scope);
}

void SidescanCorrectionDialog::onPreview()
{
    emit previewRequested();
}

void SidescanCorrectionDialog::onReset()
{
    emit resetRequested();
}

void SidescanCorrectionDialog::onLaybackToggled(bool enabled)
{
    if (m_use_file_layback)    m_use_file_layback->setEnabled(enabled);
    if (m_manual_layback_dist)
        m_manual_layback_dist->setEnabled(enabled && m_use_file_layback
                                          && !m_use_file_layback->isChecked());
}

void SidescanCorrectionDialog::onUseFileLaybackToggled(bool useFile)
{
    if (m_manual_layback_dist)
        m_manual_layback_dist->setEnabled(
            m_enable_layback && m_enable_layback->isChecked() && !useFile);
}

} // namespace dolphin::ui
