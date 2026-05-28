#pragma once
#include "ui/features/subbottom/panels/SubBottomDisplayPanel.h"  // SubBottomDisplayParams
#include "app/layers/LayerUtils.h"
#include <QSet>
#include <QVector>
#include <QWidget>
#include <memory>

namespace dolphin::app { class DataLayer; }
class QVBoxLayout;

namespace dolphin::ui {

class IRightPanelModule;
class CollapsibleSection;
class InfoModule;
class DisplayModule;
class SubBottomDisplayModule;
class SbpGainModule;
class SbpSignalModule;
class NavigationModule;
class GeometryModule;
class RadiometryModule;
class EnhancementModule;
class NavInfoPanel;
class HeadingInfoPanel;
class GainControlPanel;
class ImagingControlPanel;

// Hosts all right-panel modules as CollapsibleSections.
// Each module decides whether it supports the current layer via supports().
// setLayer() shows relevant modules and feeds each one the active layer.
class RightPanelHost : public QWidget {
    Q_OBJECT
public:
    explicit RightPanelHost(QWidget* parent = nullptr);
    ~RightPanelHost() override;

    void setLayer(app::DataLayer* layer);
    void clearLayer();

    // Called whenever the project's layer list changes so the panel knows
    // which modality-specific sections to show vs. hide entirely.
    void setAvailableModalities(const QSet<app::Modality>& modalities);

    // Panel accessors — used by WaterfallCoordinator for signal wiring.
    NavInfoPanel*        navPanel()      const;
    HeadingInfoPanel*    headingPanel()  const;
    GainControlPanel*    gainPanel()     const;
    ImagingControlPanel* imagingPanel()  const;

    // Palette forwarding from DisplayModule (SSS).
    int  currentPaletteIndex() const;
    void setPalette(int idx);

    // SBP display params forwarding.
    void setSbpParams(const SubBottomDisplayParams& p);

    // SBP processing module accessors — used by SubBottomCoordinator for signal wiring.
    SbpGainModule*   sbpGainModule()   const;
    SbpSignalModule* sbpSignalModule() const;

signals:
    void paletteChanged(int idx);
    void sbpParamsChanged(SubBottomDisplayParams params);

private:
    void addModule(IRightPanelModule* mod);

    QVBoxLayout* m_layout = nullptr;

    // QWidget modules — Qt-owned after addModule re-parents them into sections.
    InfoModule*             m_info        = nullptr;
    DisplayModule*          m_display     = nullptr;
    SubBottomDisplayModule* m_sbp_display = nullptr;
    SbpGainModule*          m_sbp_gain    = nullptr;
    SbpSignalModule*        m_sbp_signal  = nullptr;

    // Non-QObject wrapper modules — owned via unique_ptr.
    std::unique_ptr<NavigationModule>  m_navigation;
    std::unique_ptr<GeometryModule>    m_geometry;
    std::unique_ptr<RadiometryModule>  m_radiometry;
    std::unique_ptr<EnhancementModule> m_enhancement;

    // Parallel lists for generic setLayer / clearLayer iteration.
    QVector<IRightPanelModule*>  m_modules;
    QVector<CollapsibleSection*> m_sections;

    QSet<app::Modality> m_available_modalities;
};

} // namespace dolphin::ui
