#pragma once
#include "ui/features/subbottom/panels/SubBottomDisplayPanel.h"  // SubBottomDisplayParams
#include "app/layers/LayerUtils.h"
#include <QVector>
#include <QWidget>
#include <memory>

namespace dolphin::app { class DataLayer; }
class QVBoxLayout;
class QFrame;

namespace dolphin::ui {

class IRightPanelModule;
class CollapsibleSection;
class InfoModule;
class SubBottomDisplayModule;
class SbpGainModule;
class SbpSignalModule;
class NavigationModule;
class GeometryModule;
class RadiometryModule;
class EnhancementModule;
class ContactPickingModule;
class NavInfoPanel;
class HeadingInfoPanel;
class GainControlPanel;
class ImagingControlPanel;
class ContactPickingPanel;

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

    // Restricts visible sections to the given sensor's modules. Pass Unknown
    // (the Map tab) to show only universal modules — the modal host has none,
    // so the Map tab shows no tools.
    void setModalityFilter(app::Modality filter);

    // Panel accessors — used by the waterfall / sub-bottom coordinators for
    // signal wiring. Navigation / Geometry are per-modality: pass the modality
    // whose sensor-tab instance you want (Sidescan or SubBottom).
    NavInfoPanel*        navPanel(app::Modality m)     const;
    HeadingInfoPanel*    headingPanel(app::Modality m) const;
    GainControlPanel*    gainPanel()     const;
    ImagingControlPanel* imagingPanel()  const;
    // Universal annotation tool sections (Contact Picking / Feature Drawing).
    ContactPickingPanel* contactPickingPanel() const;

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
    bool computeFilterVisible(app::Modality primary) const;

    // Context menu (Qt::CustomContextMenu — matches the app's other panels).
    void showContextMenu(const QPoint& local_pos);
    void resetSectionOrder();              // restore construction order, clear persisted
    void setAllExpanded(bool expanded);    // expand / collapse all visible sections

    // -- Drag-to-reorder --------------------------------------------------------
    void onSectionReorderStarted(CollapsibleSection* sec);
    void onSectionReorderMoved(CollapsibleSection* sec, const QPoint& global_pos);
    void onSectionReorderFinished(CollapsibleSection* sec);
    // m_sections index to insert the dragged section *before* (size = end), based on
    // the pointer's vertical position among the currently-visible sections.
    int  dropTargetIndex(const QPoint& global_pos) const;
    void positionDropIndicator(int target_index);
    void moveSection(int from, int to);     // reorder m_sections/m_modules + relayout
    void relayoutSections();                // re-add sections to m_layout in list order
    // Persisted section order (per ShowMode), keyed on each module's identity.
    QString moduleKey(IRightPanelModule* mod) const;
    void    saveOrder() const;
    void    applySavedOrder();

    ShowMode         m_show_mode       = ShowMode::UniversalOnly;
    QVBoxLayout*     m_layout         = nullptr;
    app::Modality    m_modality_filter = app::Modality::Unknown;
    app::DataLayer*  m_current_layer   = nullptr;

    // QWidget modules — Qt-owned after addModule re-parents them into sections.
    InfoModule*             m_info        = nullptr;
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
    std::unique_ptr<ContactPickingModule> m_contact_picking;

    // Parallel lists for generic setLayer / clearLayer iteration.
    QVector<IRightPanelModule*>  m_modules;
    QVector<CollapsibleSection*> m_sections;
    // Construction order (modules), captured before any persisted order is applied —
    // the target for "Reset section order".
    QVector<IRightPanelModule*>  m_default_order;

    // Drag-to-reorder runtime state.
    CollapsibleSection* m_drag_section   = nullptr;
    QFrame*             m_drop_indicator = nullptr;
};

} // namespace dolphin::ui
