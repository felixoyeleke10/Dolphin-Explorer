#pragma once
#include <QWidget>

class QComboBox;
class QLabel;
class QStackedWidget;
class QToolButton;

namespace dolphin::ui {

class PanelTabBar;

// Left-panel "Views" section (SeaView-style): per-viewer display settings
// behind a MAP | SSS | SBP tab strip.
//
//   MAP — global mosaic palette, sonar preview tier, 3D draping surface.
//   SSS — palette override for the ACTIVE sidescan line (map mosaic).
//   SBP — palette override for the ACTIVE sub-bottom line (profile viewer).
//
// Dumb view: setters update the widgets without re-emitting; user edits emit
// the *Selected/*Requested signals. MainWindow wires both directions against
// the DisplayStateManager authority and the open project.
class ViewsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ViewsPanel(QWidget* parent = nullptr);

    // MAP tab
    void setMapPalette(int palette_idx);
    void setMapQuality(int quality_value);            // MapSonarQuality as int
    void setDrapingSurface(const QString& file_name); // empty = none

    // Sensor tabs — has_layer=false disables the controls (no active line of
    // that modality); palette_idx < 0 means "app default" (shown as such).
    void setSssLayer(bool has_layer, int palette_idx);
    void setSbpLayer(bool has_layer, int palette_idx);

signals:
    void mapPaletteSelected(int palette_idx);
    void mapQualitySelected(int quality_value);
    void drapingBrowseRequested();
    void drapingClearRequested();
    void sssPaletteSelected(int palette_idx);
    void sbpPaletteSelected(int palette_idx);

private:
    QWidget* buildMapPage();
    QWidget* buildSssPage();
    QWidget* buildSbpPage();

    PanelTabBar*    m_tabs          = nullptr;
    QStackedWidget* m_stack         = nullptr;

    QComboBox*   m_map_palette   = nullptr;
    QComboBox*   m_map_preview   = nullptr;
    QLabel*      m_drape_name    = nullptr;
    QToolButton* m_drape_browse  = nullptr;
    QToolButton* m_drape_clear   = nullptr;

    QComboBox* m_sss_palette   = nullptr;
    QLabel*    m_sss_hint      = nullptr;
    QComboBox* m_sbp_palette   = nullptr;
    QLabel*    m_sbp_hint      = nullptr;
};

} // namespace dolphin::ui
