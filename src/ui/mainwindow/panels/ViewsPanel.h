#pragma once
#include <QWidget>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QToolButton;

namespace dolphin::ui {

class PanelTabBar;
class HistogramRangeSlider;

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

    // Mirror the right panel's sensor tabs: show only the modalities the
    // project actually contains (MAP is always available), and follow the
    // active layer's modality. If the current tab disappears, fall back to MAP.
    void setModalities(bool has_sss, bool has_sbp);
    void setCurrentTab(int tab_id);   // 0 = MAP, 1 = SSS, 2 = SBP

    // MAP tab
    void setMapPalette(int palette_idx);
    void setMapQuality(int quality_value);            // MapSonarQuality as int
    void setDrapingSurface(const QString& file_name); // empty = none

    // Sensor tabs — has_layer=false disables the controls (no active line of
    // that modality); palette_idx < 0 means "app default" (shown as such);
    // opacity_pct is the layer's map transparency [0,100] (100 = opaque).
    void setSssLayer(bool has_layer, int palette_idx,
                     int opacity_pct = 100, int blend_mode = 0,
                     bool clip_polygons = false, bool show_beams = false,
                     int beam_spacing = 10);
    void setSbpLayer(bool has_layer, int palette_idx,
                     double gain = 1.0, double contrast = 1.0,
                     bool invert = false, int opacity_pct = 100);

    // SSS dynamic-range control (black/white points in [0,1]) + its histogram.
    void setSssDynamicRange(double low, double high);
    void setSssHistogram(std::vector<float> bins);   // empty = no active line

signals:
    void mapPaletteSelected(int palette_idx);
    void mapQualitySelected(int quality_value);
    void drapingBrowseRequested();
    void drapingClearRequested();
    void sssPaletteSelected(int palette_idx);
    void sbpPaletteSelected(int palette_idx);
    // SSS mosaic blend mode (0=Blend,1=Cover up,2=Lighten,3=Darken) — cheap repaint.
    void sssBlendSelected(int blend_mode);
    // SSS overlays (cheap repaint): clip mosaic to drawn polygons; beam fan.
    void sssClipPolygonsToggled(bool on);
    void sssShowBeamsToggled(bool on);
    void sssBeamSpacingChanged(int spacing);
    // Show/hide the near-nadir seabed band — survey-wide; re-rasters loaded lines.
    void sssShowNadirToggled(bool on);
    // SSS dynamic range (black/white points in [0,1]). Committed fires on drag
    // release → the caller does the mosaic re-raster there.
    void sssDynamicRangeCommitted(double low, double high);
    // SBP display controls (moved here from the right panel's Display section).
    void sbpDisplayEdited(double gain, double contrast, bool invert);
    // Per-layer map transparency — cheap, live (no re-raster). One signal per
    // tab since each targets that tab's active layer.
    void sssOpacityEdited(int percent);
    void sbpOpacityEdited(int percent);

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

    QComboBox*      m_sss_palette  = nullptr;
    QComboBox*      m_sss_blend    = nullptr;
    QSpinBox*       m_sss_opacity  = nullptr;
    QCheckBox*      m_sss_clip     = nullptr;
    QCheckBox*      m_sss_beams        = nullptr;
    QSpinBox*       m_sss_beam_spacing = nullptr;
    QCheckBox*      m_sss_nadir        = nullptr;
    HistogramRangeSlider* m_sss_hist = nullptr;
    QLabel*         m_sss_hint     = nullptr;
    QComboBox*      m_sbp_palette  = nullptr;
    QDoubleSpinBox* m_sbp_gain     = nullptr;
    QDoubleSpinBox* m_sbp_contrast = nullptr;
    QCheckBox*      m_sbp_invert   = nullptr;
    QSpinBox*       m_sbp_opacity  = nullptr;
    QLabel*         m_sbp_hint     = nullptr;
};

} // namespace dolphin::ui
