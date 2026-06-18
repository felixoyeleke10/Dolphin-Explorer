// DisplayStateManager.cpp — coordinator/authority for display state.
#include "ui/systems/DisplayStateManager.h"
#include "ui/systems/AppState.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"

#include <QSettings>

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

void DisplayStateManager::notifyLayerChanged(const std::string& layer_id, DisplayAspect aspect)
{
    emit displayStateChanged(QString::fromStdString(layer_id), aspect);
}

} // namespace dolphin::ui
