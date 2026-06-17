#pragma once
#include "ui/features/subbottom/panels/SubBottomDisplayPanel.h"  // SubBottomDisplayParams
#include "app/layers/LayerUtils.h"
#include "app/display/WaterfallParams.h"  // DisplayChannel
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

// Hosts a subset of right-panel modules as CollapsibleSections.
// ShowMode controls which module set is created at construction time:
//   UniversalOnly — Info, Navigation, Geometry (always relevant, never modality-filtered)
//   ModalOnly     — Display/SSS, SbpDisplay/Gain/Signal, Radiometry, Enhancement
//                   (shown/hidden by setModalityFilter)
class RightPanelHost : public QWidget {
    Q_OBJECT
public:
    enum class ShowMode { UniversalOnly, ModalOnly };

    explicit RightPanelHost(ShowMode mode, QWidget* parent = nullptr);
    ~RightPanelHost() override;

    void setLayer(app::DataLayer* layer);
    void clearLayer();

    // Called whenever the project's layer list changes so the panel knows
    // which modality-specific sections to show vs. hide entirely.
    void setAvailableModalities(const QSet<app::Modality>& modalities);

    // Restricts visible sections to Universal (Unknown) + the given modality.
    // Pass Unknown to show only universal modules (Map tab behaviour).
    void setModalityFilter(app::Modality filter);

    // Panel accessors — used by the waterfall / sub-bottom coordinators for
    // signal wiring. Navigation / Geometry are per-modality: pass the modality
    // whose sensor-tab instance you want (Sidescan or SubBottom).
    NavInfoPanel*        navPanel(app::Modality m)     const;
    HeadingInfoPanel*    headingPanel(app::Modality m) const;
    GainControlPanel*    gainPanel()     const;
    ImagingControlPanel* imagingPanel()  const;

    // Palette and channel forwarding from DisplayModule (SSS).
    int  currentPaletteIndex() const;
    void setPalette(int idx);
    void setChannel(DisplayChannel ch);

    // SBP display params forwarding.
    void setSbpParams(const SubBottomDisplayParams& p);

    // SBP processing module accessors — used by SubBottomCoordinator for signal wiring.
    SbpGainModule*   sbpGainModule()   const;
    SbpSignalModule* sbpSignalModule() const;

signals:
    void paletteChanged(int idx);
    void channelChanged(DisplayChannel ch);
    void sbpParamsChanged(SubBottomDisplayParams params);

private:
    void addModule(IRightPanelModule* mod);
    bool computeFilterVisible(app::Modality primary) const;

    ShowMode         m_show_mode       = ShowMode::UniversalOnly;
    QVBoxLayout*     m_layout         = nullptr;
    app::Modality    m_modality_filter = app::Modality::Unknown;
    app::DataLayer*  m_current_layer   = nullptr;

    // QWidget modules — Qt-owned after addModule re-parents them into sections.
    InfoModule*             m_info        = nullptr;
    DisplayModule*          m_display     = nullptr;
    SubBottomDisplayModule* m_sbp_display = nullptr;
    SbpGainModule*          m_sbp_gain    = nullptr;
    SbpSignalModule*        m_sbp_signal  = nullptr;

    // Non-QObject wrapper modules — owned via unique_ptr.
    // Navigation + Geometry are per-modality so SSS and SBP each get their own
    // section (with its own panel) under their respective sensor tab.
    std::unique_ptr<NavigationModule>  m_navigation_sss;
    std::unique_ptr<NavigationModule>  m_navigation_sbp;
    std::unique_ptr<GeometryModule>    m_geometry_sss;
    std::unique_ptr<GeometryModule>    m_geometry_sbp;
    std::unique_ptr<RadiometryModule>  m_radiometry;
    std::unique_ptr<EnhancementModule> m_enhancement;

    // Parallel lists for generic setLayer / clearLayer iteration.
    QVector<IRightPanelModule*>  m_modules;
    QVector<CollapsibleSection*> m_sections;

    QSet<app::Modality> m_available_modalities;
};

} // namespace dolphin::ui
