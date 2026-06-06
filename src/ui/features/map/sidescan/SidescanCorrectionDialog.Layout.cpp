// SidescanCorrectionDialog.Layout.cpp — section builders and static layout helpers.
#include "ui/features/map/sidescan/SidescanCorrectionDialog.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace dolphin::ui {

// -- Static helpers ------------------------------------------------------------

static QFrame* makeSectionFrame(QWidget* parent)
{
    auto* sect = new QFrame(parent);
    sect->setObjectName("dlgSection");
    return sect;
}

static QLabel* makeSectionLabel(const QString& text, QWidget* parent)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setObjectName("dlgSectionLabel");
    return lbl;
}

static void addRow(QVBoxLayout* vl, QWidget* parent,
                   const QString& label, QWidget* widget,
                   int label_width = 160)
{
    auto* row = new QWidget(parent);
    auto* rl  = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(Theme::kSpacing4);
    auto* cap = new QLabel(label, row);
    cap->setFixedWidth(label_width);
    rl->addWidget(cap);
    rl->addWidget(widget, 1);
    vl->addWidget(row);
}

// -- Section builders ----------------------------------------------------------

void SidescanCorrectionDialog::buildLayerSection(QVBoxLayout* body,
                                                 const QString& layer_name)
{
    auto* sect = makeSectionFrame(this);
    auto* vl   = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing5, 14, Theme::kSpacing5, 14);
    vl->setSpacing(Theme::kSpacing3);
    vl->addWidget(makeSectionLabel(tr("Layer"), sect));
    auto* name_lbl = new QLabel(layer_name, sect);
    name_lbl->setWordWrap(true);
    vl->addWidget(name_lbl);
    body->addWidget(sect);
}

void SidescanCorrectionDialog::buildNavigationSection(QVBoxLayout* body)
{
    auto* sect = makeSectionFrame(this);
    auto* vl   = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing5, 14, Theme::kSpacing5, 14);
    vl->setSpacing(10);
    vl->addWidget(makeSectionLabel(tr("Position Correction"), sect));

    m_along_track = new QDoubleSpinBox(sect);
    m_along_track->setRange(-9999.0, 9999.0);
    m_along_track->setSuffix(tr(" m"));
    m_along_track->setDecimals(2);
    m_along_track->setToolTip(tr(
        "Shift the sonar position forward (positive) or backward (negative) along\n"
        "the direction of vessel travel. Use to correct GPS antenna–to–towpoint offset."));

    m_cross_track = new QDoubleSpinBox(sect);
    m_cross_track->setRange(-9999.0, 9999.0);
    m_cross_track->setSuffix(tr(" m"));
    m_cross_track->setDecimals(2);
    m_cross_track->setToolTip(tr(
        "Shift the sonar position to starboard (positive) or port (negative)\n"
        "perpendicular to the direction of travel. Use to correct transverse mounting offset."));

    m_time_delay = new QDoubleSpinBox(sect);
    m_time_delay->setRange(-60.0, 60.0);
    m_time_delay->setSuffix(tr(" s"));
    m_time_delay->setDecimals(3);
    m_time_delay->setToolTip(tr(
        "Compensate for a fixed timing offset between the nav system and the sonar.\n"
        "Positive = nav timestamps are ahead of sonar; negative = nav lags behind.\n"
        "Typical values: ±0.2 s for serial latency, ±1–2 s for GPS buffering."));

    addRow(vl, sect, tr("Along-track offset:"), m_along_track);
    addRow(vl, sect, tr("Cross-track offset:"), m_cross_track);
    addRow(vl, sect, tr("Time delay:"),         m_time_delay);

    body->addWidget(sect);
}

void SidescanCorrectionDialog::buildNavSourceSection(QVBoxLayout* body)
{
    auto* sect = makeSectionFrame(this);
    auto* vl   = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing5, 14, Theme::kSpacing5, 14);
    vl->setSpacing(10);
    vl->addWidget(makeSectionLabel(tr("Nav Position Source"), sect));

    m_nav_source = new QComboBox(sect);
    m_nav_source->addItem(tr("Auto (fish → vessel)"));
    m_nav_source->addItem(tr("Fish / Sensor Position"));
    m_nav_source->addItem(tr("Vessel / Ship Position"));
    m_nav_source->addItem(tr("Vessel + Layback"));
    m_nav_source->addItem(tr("Manual Offset Only"));
    m_nav_source->setToolTip(tr(
        "Auto: use the towfish/sensor GPS if recorded, otherwise fall back to vessel GPS.\n"
        "Fish / Sensor: always use the towfish GPS antenna — skip pings with no fish nav.\n"
        "Vessel / Ship: always use the ship GPS antenna — good for hull-mounted systems.\n"
        "Vessel + Layback: estimate fish position from ship GPS plus a cable-length offset\n"
        "    along the vessel heading (configure layback distance in the Layback section).\n"
        "Manual Offset Only: apply a fixed east/north shift to the best available position."));

    addRow(vl, sect, tr("Position source:"), m_nav_source);
    body->addWidget(sect);
}

void SidescanCorrectionDialog::buildHeadingSourceSection(QVBoxLayout* body)
{
    auto* sect = makeSectionFrame(this);
    auto* vl   = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing5, 14, Theme::kSpacing5, 14);
    vl->setSpacing(10);
    vl->addWidget(makeSectionLabel(tr("Heading Source"), sect));

    m_heading_source = new QComboBox(sect);
    m_heading_source->addItem(tr("Auto (sensor → ship → COG)"));
    m_heading_source->addItem(tr("Fish sensor / AHRS"));
    m_heading_source->addItem(tr("Vessel gyro / gyrocompass"));
    m_heading_source->addItem(tr("Course over ground (raw)"));
    m_heading_source->addItem(tr("Course over ground (smoothed)"));
    m_heading_source->setToolTip(tr(
        "Auto: use towfish AHRS heading if recorded, fall back to ship gyro, then COG.\n"
        "Fish sensor / AHRS: use only the inertial heading from the towfish — most accurate\n"
        "    for deep-tow systems where the fish heading differs from the vessel heading.\n"
        "Vessel gyro: use only the ship gyrocompass — suitable for hull-mounted sonars.\n"
        "Course over ground (raw): derive heading from consecutive GPS positions.\n"
        "    Noisy at low speed; affected by cross-currents.\n"
        "Course over ground (smoothed): EMA-filtered COG — less noisy but lags on turns."));

    m_hdg_offset = new QDoubleSpinBox(sect);
    m_hdg_offset->setRange(-180.0, 180.0);
    m_hdg_offset->setSuffix(tr(" °"));
    m_hdg_offset->setDecimals(2);
    m_hdg_offset->setToolTip(tr(
        "Add a fixed bearing correction to the resolved heading.\n"
        "Use to compensate for a known sensor mounting misalignment or\n"
        "a gyrocompass calibration offset. Applied after source selection."));

    m_swap_channels = new QCheckBox(tr("Swap port / starboard"), sect);
    m_swap_channels->setToolTip(tr(
        "Flip the port and starboard channel assignments.\n"
        "Enable if the sonar appears mirrored on the map — this can happen\n"
        "when a cable was connected inverted or when the fish was towed tail-first."));

    addRow(vl, sect, tr("Heading source:"), m_heading_source);
    addRow(vl, sect, tr("Heading offset:"), m_hdg_offset);
    addRow(vl, sect, tr("Channel swap:"),   m_swap_channels);
    body->addWidget(sect);
}

void SidescanCorrectionDialog::buildLaybackSection(QVBoxLayout* body)
{
    auto* sect = makeSectionFrame(this);
    auto* vl   = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing5, 14, Theme::kSpacing5, 14);
    vl->setSpacing(10);
    vl->addWidget(makeSectionLabel(tr("Layback"), sect));

    m_enable_layback = new QCheckBox(tr("Enable layback correction"), sect);
    m_enable_layback->setToolTip(tr(
        "Estimate the towfish position by offsetting the vessel GPS along the vessel\n"
        "heading by the cable-out (layback) distance. Only meaningful when Position\n"
        "Source is set to 'Vessel + Layback'."));

    m_use_file_layback = new QCheckBox(tr("Use layback from file"), sect);
    m_use_file_layback->setChecked(true);
    m_use_file_layback->setEnabled(false);
    m_use_file_layback->setToolTip(tr(
        "When checked, read the layback distance from the value recorded in each ping\n"
        "(varies per ping as cable is paid out or retrieved).\n"
        "When unchecked, use the fixed manual distance entered below."));

    m_manual_layback_dist = new QDoubleSpinBox(sect);
    m_manual_layback_dist->setRange(0.0, 9999.0);
    m_manual_layback_dist->setSuffix(tr(" m"));
    m_manual_layback_dist->setDecimals(1);
    m_manual_layback_dist->setEnabled(false);
    m_manual_layback_dist->setToolTip(tr(
        "Fixed cable-out distance in metres. Applied to every ping when\n"
        "'Use layback from file' is unchecked."));

    addRow(vl, sect, tr("Layback:"),         m_enable_layback);
    addRow(vl, sect, tr("Layback source:"),  m_use_file_layback);
    addRow(vl, sect, tr("Manual distance:"), m_manual_layback_dist);

    connect(m_enable_layback,   &QCheckBox::toggled,
            this, &SidescanCorrectionDialog::onLaybackToggled);
    connect(m_use_file_layback, &QCheckBox::toggled,
            this, &SidescanCorrectionDialog::onUseFileLaybackToggled);

    body->addWidget(sect);
}

void SidescanCorrectionDialog::buildSmoothingSection(QVBoxLayout* body)
{
    auto* sect = makeSectionFrame(this);
    auto* vl   = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing5, 14, Theme::kSpacing5, 14);
    vl->setSpacing(10);
    vl->addWidget(makeSectionLabel(tr("Nav Smoothing"), sect));

    m_smoothing_mode = new QComboBox(sect);
    m_smoothing_mode->addItem(tr("Off"));
    m_smoothing_mode->addItem(tr("Spike rejection"));
    m_smoothing_mode->addItem(tr("Moving average"));
    m_smoothing_mode->addItem(tr("Median"));
    m_smoothing_mode->setCurrentIndex(0);
    m_smoothing_mode->setToolTip(tr(
        "Off: use raw resolved nav positions — fastest, no position alteration.\n"
        "Spike rejection: discard pings whose position jumps further than a threshold\n"
        "    in a single step (removes GPS dropouts and fix errors).\n"
        "Moving average: replace each position with the mean of the surrounding window\n"
        "    (smooths noise but shifts positions slightly on curves).\n"
        "Median: replace each position with the median of the surrounding window\n"
        "    (robust to outliers; recommended for noisy fish nav)."));

    m_smoothing_window = new QSpinBox(sect);
    m_smoothing_window->setRange(3, 51);
    m_smoothing_window->setSingleStep(2);
    m_smoothing_window->setValue(5);
    m_smoothing_window->setSuffix(tr(" pings"));
    m_smoothing_window->setToolTip(tr(
        "Number of consecutive pings used when computing the smoothed position.\n"
        "Must be odd. Larger values give smoother tracks but reduce sharpness on turns.\n"
        "Typical range: 5–15 pings."));

    addRow(vl, sect, tr("Smoothing:"),   m_smoothing_mode);
    addRow(vl, sect, tr("Window size:"), m_smoothing_window);
    body->addWidget(sect);
}

void SidescanCorrectionDialog::buildManualOffsetSection(QVBoxLayout* body)
{
    auto* sect = makeSectionFrame(this);
    auto* vl   = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing5, 14, Theme::kSpacing5, 14);
    vl->setSpacing(10);
    vl->addWidget(makeSectionLabel(tr("Manual Offsets"), sect));

    m_x_offset = new QDoubleSpinBox(sect);
    m_x_offset->setRange(-9999.0, 9999.0);
    m_x_offset->setSuffix(tr(" m"));
    m_x_offset->setDecimals(2);
    m_x_offset->setToolTip(tr(
        "Shift all georeferenced data east (positive) or west (negative) by a fixed\n"
        "number of metres. Applied after nav source selection and layback.\n"
        "Use to correct a known GPS datum offset or antenna lever-arm error."));

    m_y_offset = new QDoubleSpinBox(sect);
    m_y_offset->setRange(-9999.0, 9999.0);
    m_y_offset->setSuffix(tr(" m"));
    m_y_offset->setDecimals(2);
    m_y_offset->setToolTip(tr(
        "Shift all georeferenced data north (positive) or south (negative) by a fixed\n"
        "number of metres. Applied after nav source selection and layback."));

    addRow(vl, sect, tr("X offset (east):"),  m_x_offset);
    addRow(vl, sect, tr("Y offset (north):"), m_y_offset);
    body->addWidget(sect);
}

void SidescanCorrectionDialog::buildDebugSection(QVBoxLayout* body)
{
    auto* sect = makeSectionFrame(this);
    auto* vl   = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing5, 14, Theme::kSpacing5, 14);
    vl->setSpacing(10);
    vl->addWidget(makeSectionLabel(tr("Debug Display"), sect));

    m_debug_ping_lines = new QCheckBox(
        tr("Independent ping lines (skip quad stitching)"), sect);
    m_debug_ping_lines->setToolTip(tr(
        "Render each ping as an isolated swath line rather than stitching adjacent\n"
        "pings into filled quadrilaterals. Useful for diagnosing whether a mosaic\n"
        "artefact is caused by bad positioning (lines misaligned) or by the quad\n"
        "stitching algorithm (lines look correct but filled area stretches)."));

    auto* hint = new QLabel(
        tr("If ping lines look correct but filled mosaic stretches, "
           "the issue is stitching, not nav."), sect);
    hint->setWordWrap(true);
    hint->setObjectName("dlgHint");

    vl->addWidget(m_debug_ping_lines);
    vl->addWidget(hint);
    body->addWidget(sect);
}

void SidescanCorrectionDialog::buildScopeRow(QVBoxLayout* body)
{
    auto* sect = makeSectionFrame(this);
    auto* vl   = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing5, 14, Theme::kSpacing5, 14);
    vl->setSpacing(10);
    vl->addWidget(makeSectionLabel(tr("Apply To"), sect));

    m_scope_combo = new QComboBox(sect);
    m_scope_combo->addItem(tr("Selected layer only"));
    m_scope_combo->addItem(tr("All layers in project"));
    m_scope_combo->setToolTip(tr(
        "Selected layer only: apply corrections to the currently active layer.\n"
        "All layers: apply the same corrections to every layer in the project.\n"
        "Use 'All layers' when a systematic offset (e.g. datum shift) affects the whole survey."));

    addRow(vl, sect, tr("Run processing on:"), m_scope_combo);
    body->addWidget(sect);
}

} // namespace dolphin::ui
