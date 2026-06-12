#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/mainwindow/rightpanel/RightPanel.Info.h"
#include "ui/mainwindow/rightpanel/RightPanel.Display.h"
#include "ui/mainwindow/rightpanel/RightPanel.SubBottomDisplay.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpGain.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpSignal.h"
#include "ui/mainwindow/rightpanel/RightPanel.Navigation.h"
#include "ui/mainwindow/rightpanel/RightPanel.Geometry.h"
#include "ui/mainwindow/rightpanel/RightPanel.Radiometry.h"
#include "ui/mainwindow/rightpanel/RightPanel.Enhancement.h"
#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "ui/mainwindow/panels/GainControlPanel.h"
#include "ui/mainwindow/panels/ImagingControlPanel.h"
#include "ui/shared/widgets/CollapsibleSection.h"
#include "ui/shared/UiUtils.h"
#include <QVBoxLayout>

namespace dolphin::ui {

RightPanelHost::RightPanelHost(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    m_layout = makeCompactLayout<QVBoxLayout>(this);

    // QWidget modules — parented to this, re-parented into their section by addModule.
    m_info        = new InfoModule(this);
    m_display     = new DisplayModule(this);
    m_sbp_display = new SubBottomDisplayModule(this);
    m_sbp_gain    = new SbpGainModule(this);
    m_sbp_signal  = new SbpSignalModule(this);

    // Non-QObject wrapper modules.
    m_navigation  = std::make_unique<NavigationModule>();
    m_geometry    = std::make_unique<GeometryModule>();
    m_radiometry  = std::make_unique<RadiometryModule>();
    m_enhancement = std::make_unique<EnhancementModule>();

    // Section order: Info → Display (SSS) → Display (SBP) → Gain (SBP) → Signal (SBP) → Radiometry → Enhancement → Navigation → Geometry
    addModule(m_info);
    addModule(m_display);
    addModule(m_sbp_display);
    addModule(m_sbp_gain);
    addModule(m_sbp_signal);
    addModule(m_radiometry.get());
    addModule(m_enhancement.get());
    addModule(m_navigation.get());
    addModule(m_geometry.get());

    m_layout->addStretch(1);

    connect(m_display, &DisplayModule::paletteChanged,
            this,      &RightPanelHost::paletteChanged);

    connect(m_sbp_display, &SubBottomDisplayModule::paramsChanged,
            this,          &RightPanelHost::sbpParamsChanged);

    clearLayer();  // start with all sections hidden
}

RightPanelHost::~RightPanelHost() = default;

void RightPanelHost::addModule(IRightPanelModule* mod)
{
    auto* sec = new CollapsibleSection(mod->title(), this);
    sec->setContent(mod->widget());
    sec->setBadge(mod->badge());
    sec->setIcon(mod->icon());
    m_layout->addWidget(sec);
    m_modules.append(mod);
    m_sections.append(sec);
}

void RightPanelHost::setAvailableModalities(const QSet<app::Modality>& modalities)
{
    m_available_modalities = modalities;
}

void RightPanelHost::setLayer(app::DataLayer* layer)
{
    for (int i = 0; i < m_modules.size(); ++i) {
        const auto primary = m_modules[i]->primaryModality();
        // Modality-specific sections are hidden when that modality isn't in the project.
        // Universal sections (Unknown) are always shown.
        const bool show =
            primary == app::Modality::Unknown ||
            m_available_modalities.contains(primary);
        m_sections[i]->setVisible(show);
        if (!show) continue;

        const bool ok = m_modules[i]->supports(*layer);
        m_sections[i]->setApplicable(ok, m_modules[i]->notApplicableHint());
        if (ok) m_modules[i]->setLayer(layer);
    }
}

void RightPanelHost::clearLayer()
{
    for (auto* sec : m_sections)
        sec->setVisible(false);
}

NavInfoPanel* RightPanelHost::navPanel() const
{
    return m_navigation ? m_navigation->panel() : nullptr;
}

HeadingInfoPanel* RightPanelHost::headingPanel() const
{
    return m_geometry ? m_geometry->panel() : nullptr;
}

GainControlPanel* RightPanelHost::gainPanel() const
{
    return m_radiometry ? m_radiometry->panel() : nullptr;
}

ImagingControlPanel* RightPanelHost::imagingPanel() const
{
    return m_enhancement ? m_enhancement->panel() : nullptr;
}

int RightPanelHost::currentPaletteIndex() const
{
    return m_display ? m_display->currentPaletteIndex() : 0;
}

void RightPanelHost::setPalette(int idx)
{
    if (m_display) m_display->setPalette(idx);
}

void RightPanelHost::setSbpParams(const SubBottomDisplayParams& p)
{
    if (m_sbp_display) m_sbp_display->setParams(p);
}

SbpGainModule* RightPanelHost::sbpGainModule() const
{
    return m_sbp_gain;
}

SbpSignalModule* RightPanelHost::sbpSignalModule() const
{
    return m_sbp_signal;
}

} // namespace dolphin::ui
