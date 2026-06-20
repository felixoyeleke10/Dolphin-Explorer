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

RightPanelHost::RightPanelHost(ShowMode mode, QWidget* parent)
    : QWidget(parent)
    , m_show_mode(mode)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_layout = makeCompactLayout<QVBoxLayout>(this);

    using M = app::Modality;
    if (m_show_mode == ShowMode::UniversalOnly) {
        // Universal modules: always relevant, never modality-filtered.
        m_info = new InfoModule(this);
        addModule(m_info);
    } else {
        // Modal modules: sensor-specific, shown/hidden by modality filter.
        m_display     = new DisplayModule(this);
        m_sbp_display = new SubBottomDisplayModule(this);
        m_sbp_gain    = new SbpGainModule(this);
        m_sbp_signal  = new SbpSignalModule(this);
        m_radiometry  = std::make_unique<RadiometryModule>();
        m_enhancement = std::make_unique<EnhancementModule>();
        // Navigation + Geometry are per-modality: one instance per sensor tab so
        // SSS and SBP each get their own section (and their own panel for wiring).
        m_navigation_sss = std::make_unique<NavigationModule>(M::Sidescan);
        m_navigation_sbp = std::make_unique<NavigationModule>(M::SubBottom);
        m_geometry_sss   = std::make_unique<GeometryModule>(M::Sidescan);
        m_geometry_sbp   = std::make_unique<GeometryModule>(M::SubBottom);

        // Section order per tab: display → processing → Navigation → Geometry.
        addModule(m_display);
        addModule(m_sbp_display);
        addModule(m_sbp_gain);
        addModule(m_sbp_signal);
        addModule(m_radiometry.get());
        addModule(m_enhancement.get());
        addModule(m_navigation_sss.get());
        addModule(m_geometry_sss.get());
        addModule(m_navigation_sbp.get());
        addModule(m_geometry_sbp.get());

        connect(m_display, &DisplayModule::paletteChanged,
                this,      &RightPanelHost::paletteChanged);
        connect(m_sbp_display, &SubBottomDisplayModule::paramsChanged,
                this,          &RightPanelHost::sbpParamsChanged);
    }

    m_layout->addStretch(1);
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

void RightPanelHost::setModalityFilter(app::Modality filter)
{
    m_modality_filter = filter;
    if (!m_current_layer) return;
    for (int i = 0; i < m_modules.size(); ++i)
        m_sections[i]->setVisible(computeFilterVisible(m_modules[i]->primaryModality()));
}

bool RightPanelHost::computeFilterVisible(app::Modality primary) const
{
    if (m_show_mode == ShowMode::UniversalOnly)
        return true;  // all modules in this host are Unknown-primary
    // ModalOnly: show only the active sensor tab's sections. The Map tab (Unknown
    // filter) has no sensor sections — it shows only universal (Unknown-primary)
    // modules, of which the modal host has none, so Map shows no tools.
    return primary == m_modality_filter;
}

void RightPanelHost::setLayer(app::DataLayer* layer)
{
    m_current_layer = layer;
    for (int i = 0; i < m_modules.size(); ++i) {
        const auto primary = m_modules[i]->primaryModality();
        // Always evaluate supports() so setApplicable is correct even for
        // sections that are currently hidden by the modality filter.
        const bool ok = m_modules[i]->supports(*layer);
        m_sections[i]->setApplicable(ok, m_modules[i]->notApplicableHint());
        if (ok) m_modules[i]->setLayer(layer);
        m_sections[i]->setVisible(computeFilterVisible(primary));
    }
}

void RightPanelHost::clearLayer()
{
    m_current_layer = nullptr;
    for (auto* sec : m_sections)
        sec->setVisible(false);
}

NavInfoPanel* RightPanelHost::navPanel(app::Modality m) const
{
    const auto& mod = (m == app::Modality::SubBottom) ? m_navigation_sbp : m_navigation_sss;
    return mod ? mod->panel() : nullptr;
}

HeadingInfoPanel* RightPanelHost::headingPanel(app::Modality m) const
{
    const auto& mod = (m == app::Modality::SubBottom) ? m_geometry_sbp : m_geometry_sss;
    return mod ? mod->panel() : nullptr;
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
