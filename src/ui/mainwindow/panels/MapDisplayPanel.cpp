// MapDisplayPanel.cpp — map view working options (right panel "Map" tab).
#include "ui/mainwindow/panels/MapDisplayPanel.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {

constexpr auto kKeyTooltips   = "map/showTooltips";
constexpr auto kKeyHighlight  = "map/hoverHighlight";

QLabel* fieldLabel(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("ceFieldLabel"));
    return l;
}

} // namespace

MapDisplayPanel::MapDisplayPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3,
                           Theme::kSpacing4, Theme::kSpacing3);
    vl->setSpacing(Theme::kSpacing2);

    auto addSection = [&](const QString& title, bool first = false) {
        auto* lbl = new QLabel(title, this);
        lbl->setObjectName(QStringLiteral("ceSection"));
        auto* line = new QFrame(this);
        line->setObjectName(QStringLiteral("ceDivider"));
        line->setFrameShape(QFrame::HLine);
        if (!first) vl->addSpacing(Theme::kSpacing2);
        vl->addWidget(lbl);
        vl->addWidget(line);
    };

    // -- GENERAL --------------------------------------------------------------
    addSection(tr("GENERAL"), /*first=*/true);

    m_tooltips_check = new QCheckBox(tr("Show tooltips"), this);
    m_tooltips_check->setToolTip(
        tr("Show the line name in a tooltip while hovering its coverage on the map."));
    vl->addWidget(m_tooltips_check);

    m_highlight_check = new QCheckBox(tr("Highlight items under cursor"), this);
    m_highlight_check->setToolTip(
        tr("Outline the survey line under the cursor while moving over the map."));
    vl->addWidget(m_highlight_check);

    // -- CAMERA PROPERTIES ------------------------------------------------------
    addSection(tr("CAMERA PROPERTIES"));

    auto* fl = new QFormLayout;
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(Theme::kSpacing2);
    fl->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fl->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_azimuth_spin = new QDoubleSpinBox(this);
    m_azimuth_spin->setObjectName(QStringLiteral("ceSpin"));
    m_azimuth_spin->setRange(0.0, 359.9);
    m_azimuth_spin->setDecimals(1);
    m_azimuth_spin->setSingleStep(5.0);
    m_azimuth_spin->setWrapping(true);
    m_azimuth_spin->setSuffix(QStringLiteral("°"));
    m_azimuth_spin->setAlignment(Qt::AlignRight);
    m_azimuth_spin->setToolTip(
        tr("View azimuth: 0° = north-up. Rotates the 2D chart or orbits the 3D camera."));
    fl->addRow(fieldLabel(tr("Azimuth:")), m_azimuth_spin);

    m_height_spin = new QDoubleSpinBox(this);
    m_height_spin->setObjectName(QStringLiteral("ceSpin"));
    m_height_spin->setRange(1.0, 500000.0);
    m_height_spin->setDecimals(0);
    m_height_spin->setSingleStep(25.0);
    m_height_spin->setSuffix(QStringLiteral(" m"));
    m_height_spin->setAlignment(Qt::AlignRight);
    m_height_spin->setToolTip(
        tr("Camera height over the scene — lower to zoom in, raise for overview.\n"
           "Drives the 3D camera distance and the equivalent 2D scale."));
    fl->addRow(fieldLabel(tr("Height/Depth:")), m_height_spin);

    vl->addLayout(fl);

    vl->addStretch(1);

    // -- Load persisted view options ------------------------------------------
    {
        QSettings qs;
        m_loading = true;
        m_tooltips_check->setChecked(qs.value(kKeyTooltips, true).toBool());
        m_highlight_check->setChecked(qs.value(kKeyHighlight, true).toBool());
        m_loading = false;
    }

    // -- Wiring -----------------------------------------------------------------
    connect(m_tooltips_check, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading) return;
        persist();
        emit tooltipsToggled(on);
    });
    connect(m_highlight_check, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading) return;
        persist();
        emit hoverHighlightToggled(on);
    });
    connect(m_azimuth_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { if (!m_loading) emit azimuthEdited(v); });
    connect(m_height_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { if (!m_loading) emit heightEdited(v); });
}


void MapDisplayPanel::persist() const
{
    QSettings qs;
    qs.setValue(kKeyTooltips,   m_tooltips_check->isChecked());
    qs.setValue(kKeyHighlight,  m_highlight_check->isChecked());
}

void MapDisplayPanel::setCameraReadout(double azimuth_deg, double height_m)
{
    m_loading = true;
    const QSignalBlocker b1(m_azimuth_spin), b2(m_height_spin);
    // Don't fight the user mid-edit.
    if (!m_azimuth_spin->hasFocus())
        m_azimuth_spin->setValue(azimuth_deg);
    if (!m_height_spin->hasFocus() && height_m > 0.0)
        m_height_spin->setValue(height_m);
    m_loading = false;
}

void MapDisplayPanel::broadcastState()
{
    emit tooltipsToggled(m_tooltips_check->isChecked());
    emit hoverHighlightToggled(m_highlight_check->isChecked());
}

} // namespace dolphin::ui
