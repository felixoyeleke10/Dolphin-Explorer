// DisplayStateManager.cpp — coordinator/authority for display state.
#include "ui/systems/DisplayStateManager.h"
#include "ui/systems/AppState.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <QSettings>

#include <algorithm>
#include <cmath>

namespace dolphin::ui {

// Canonical QSettings key for the map preview quality — must match
// kKeyMapSonarQuality (kept as a literal so this low-level systems
// lib doesn't depend on the dialogs).
static constexpr const char* kKeyMapSonarQuality = "display/mapSonarQuality";

DisplayStateManager::DisplayStateManager(AppState* app_state, QObject* parent)
    : QObject(parent)
    , m_app_state(app_state)
{
    // Bridge global-default changes onto the single display bus so consumers only
    // have to listen to displayStateChanged.
    if (m_app_state) {
        connect(m_app_state, &AppState::defaultPaletteChanged, this, [this](int) {
            emit displayStateChanged({}, DisplayAspect::DefaultPalette);
        });
        connect(m_app_state, &AppState::settingsChanged, this, [this](const AppSettings&) {
            // Coarse: map background / coordinate-format defaults may have changed.
            emit displayStateChanged({}, DisplayAspect::Background);
            emit displayStateChanged({}, DisplayAspect::CoordFormat);
        });
    }
}

// -- Per-view state -----------------------------------------------------------

void DisplayStateManager::setMapQuality(MapSonarQuality q)
{
    if (q == m_map_quality) return;
    m_map_quality = q;
    QSettings().setValue(kKeyMapSonarQuality, static_cast<int>(q));
    emit displayStateChanged({}, DisplayAspect::MapQuality);
}

void DisplayStateManager::setMapPalette(int idx)
{
    if (idx == m_map_palette) return;
    m_map_palette = idx;
    QSettings().setValue(QStringLiteral("sss/paletteIdx"), idx);
    emit displayStateChanged({}, DisplayAspect::Palette);
}

void DisplayStateManager::loadPersistentState()
{
    const int v = QSettings().value(kKeyMapSonarQuality,
                                    static_cast<int>(MapSonarQuality::CoverageOnly)).toInt();
    m_map_quality = mapSonarQualityFromInt(v);   // migrates the retired Low tier
}

// -- Global defaults (delegated to AppState) ----------------------------------

int    DisplayStateManager::defaultPalette() const {
    return m_app_state ? m_app_state->current().default_palette : 0;
}
QColor DisplayStateManager::mapBackground() const {
    return m_app_state ? m_app_state->current().map_bg_color : QColor();
}
int    DisplayStateManager::coordFormat() const {
    return m_app_state ? m_app_state->current().coord_format : 0;
}

// -- Per-layer mutation -------------------------------------------------------

app::DataLayer* DisplayStateManager::layerById(const std::string& layer_id) const
{
    return m_project ? m_project->findLayer(layer_id) : nullptr;
}

bool DisplayStateManager::setLayerVisible(const std::string& layer_id, bool visible)
{
    auto* l = layerById(layer_id);
    if (!l || l->visible == visible) return l != nullptr;
    l->visible = visible;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Visibility);
    return true;
}

bool DisplayStateManager::setLayerOpacity(const std::string& layer_id, float opacity)
{
    auto* l = layerById(layer_id);
    opacity = std::clamp(opacity, 0.f, 1.f);
    if (!l || std::abs(l->map_opacity - opacity) < 1e-3f) return l != nullptr;
    l->map_opacity = opacity;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Opacity);
    return true;
}

bool DisplayStateManager::setLayerBlendMode(const std::string& layer_id, int blend_mode)
{
    auto* l = layerById(layer_id);
    if (!l || l->map_blend_mode == blend_mode) return l != nullptr;
    l->map_blend_mode = blend_mode;
    // Reuse Opacity — both are cheap map-composite changes with one fan-out.
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Opacity);
    return true;
}

bool DisplayStateManager::setLayerClipPolygons(const std::string& layer_id, bool clip)
{
    auto* l = layerById(layer_id);
    if (!l || l->map_clip_polygons == clip) return l != nullptr;
    l->map_clip_polygons = clip;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Opacity);
    return true;
}

bool DisplayStateManager::setLayerShowBeams(const std::string& layer_id, bool show)
{
    auto* l = layerById(layer_id);
    if (!l || l->map_show_beams == show) return l != nullptr;
    l->map_show_beams = show;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Opacity);
    return true;
}

bool DisplayStateManager::setLayerBeamSpacing(const std::string& layer_id, int spacing)
{
    auto* l = layerById(layer_id);
    if (!l || l->map_beam_spacing == spacing) return l != nullptr;
    l->map_beam_spacing = spacing;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Opacity);
    return true;
}

bool DisplayStateManager::setLayerSssPalette(const std::string& layer_id, int palette_idx)
{
    auto* l = layerById(layer_id);
    if (!l || l->sss_palette == palette_idx) return l != nullptr;
    l->sss_palette = palette_idx;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Palette);
    return true;
}

bool DisplayStateManager::setLayerSbpPalette(const std::string& layer_id, int palette_idx)
{
    auto* l = layerById(layer_id);
    if (!l || l->sbp_palette == palette_idx) return l != nullptr;
    l->sbp_palette = palette_idx;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Palette);
    return true;
}

bool DisplayStateManager::setLayerSbpGain(const std::string& layer_id,
                                          const app::SbpGainParams& gain)
{
    auto* l = layerById(layer_id);
    if (!l) return false;
    l->sbp_display_state.gain            = gain;
    l->sbp_display_state.gain_customized = true;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Gain);
    return true;
}

bool DisplayStateManager::setLayerSbpSignal(const std::string& layer_id,
                                            const app::SbpSignalParams& sig)
{
    auto* l = layerById(layer_id);
    if (!l) return false;
    l->sbp_display_state.signal            = sig;
    l->sbp_display_state.signal_customized = true;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Gain);
    return true;
}

bool DisplayStateManager::setLayerSbpDisplay(const std::string& layer_id,
                                             const SubBottomDisplayParams& disp)
{
    auto* l = layerById(layer_id);
    if (!l) return false;
    l->sbp_display_state.display            = disp;
    l->sbp_display_state.display_customized = true;
    l->sbp_palette                         = disp.palette_index;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Palette);
    return true;
}

void DisplayStateManager::setAllSbpGain(const app::SbpGainParams& gain)
{
    if (!m_project) return;
    for (const auto& l : m_project->layers())
        if (l && l->modality == app::Modality::SubBottom)
            setLayerSbpGain(l->id, gain);
}

void DisplayStateManager::setAllSbpSignal(const app::SbpSignalParams& sig)
{
    if (!m_project) return;
    for (const auto& l : m_project->layers())
        if (l && l->modality == app::Modality::SubBottom)
            setLayerSbpSignal(l->id, sig);
}

bool DisplayStateManager::setLayerSssDisplay(const std::string& layer_id,
                                             const WaterfallParams& params)
{
    auto* l = layerById(layer_id);
    if (!l) return false;
    l->sss_display_state.params     = params;
    l->sss_display_state.customized = true;
    l->slant_range_corrected        = params.slant_range_correction;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::Gain);
    return true;
}

bool DisplayStateManager::setLayerNav(const std::string& layer_id,
                                      const NavProcessingParams& nav)
{
    auto* l = layerById(layer_id);
    if (!l) return false;
    l->nav_state      = nav;
    l->nav_customized = true;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::NavOverlay);
    return true;
}

bool DisplayStateManager::clearLayerNav(const std::string& layer_id)
{
    auto* l = layerById(layer_id);
    if (!l) return false;
    l->nav_state      = NavProcessingParams{};
    l->nav_customized = false;
    emit displayStateChanged(QString::fromStdString(layer_id), DisplayAspect::NavOverlay);
    return true;
}

void DisplayStateManager::notifyLayerChanged(const std::string& layer_id, DisplayAspect aspect)
{
    emit displayStateChanged(QString::fromStdString(layer_id), aspect);
}

} // namespace dolphin::ui
