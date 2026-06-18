#pragma once
#include "ui/features/map/MapTypes.h"   // MapSonarQuality
#include <QObject>
#include <QString>
#include <string>

namespace dolphin::app { class Project; class DataLayer; }

namespace dolphin::ui {

class AppState;

// Which facet of display state changed — carried by displayStateChanged so a
// consumer can react narrowly (e.g. recolour on Palette, rebuild on MapQuality)
// instead of refreshing everything.
enum class DisplayAspect {
    // Per-layer (layer_id is the affected layer)
    Palette,        // SSS/SBP colour map (incl. invert)
    Gain,           // gain / contrast / signal params
    Channel,        // SSS display channel (port/stbd/both)
    NavOverlay,     // nav-correction / overlay choices
    Visibility,     // layer shown/hidden on the map

    // Per-view (layer_id empty) — state this manager OWNS
    MapQuality,     // map sonar preview tier (Off/CoverageOnly/Low/Medium/High)
    ThreeD,         // 3D map settings
    WaterfallView,  // waterfall zoom / view params

    // Global defaults (layer_id empty) — bridged from AppState
    DefaultPalette,
    Background,
    CoordFormat,
    DefaultGain,
};

// -----------------------------------------------------------------------------
//  DisplayStateManager — the single authority/coordinator for display state.
//
//  Design (coordinator-over-the-model): per-LAYER display state physically lives
//  on DataLayer (and is persisted in the project JSON, which already works); this
//  manager is the single place to MUTATE it so every change is notified
//  consistently. Global defaults live in AppState; this manager bridges their
//  change signals into displayStateChanged so all consumers have one bus. Per-VIEW
//  state that nothing else owned (map preview quality, 3D, waterfall view) is owned
//  here and persisted to QSettings.
//
//  Consumers (map / waterfall / SBP) connect to displayStateChanged(layer_id,
//  aspect) and update only what changed; they never poke each other directly.
//
//  One instance is owned by MainWindow; setProject() keeps it pointed at the open
//  project for per-layer lookups.
// -----------------------------------------------------------------------------
class DisplayStateManager : public QObject {
    Q_OBJECT
public:
    explicit DisplayStateManager(AppState* app_state, QObject* parent = nullptr);

    // Point at the open project (for per-layer resolution). Pass nullptr on close.
    void setProject(app::Project* project) { m_project = project; }

    // -- Per-view state (owned here) -------------------------------------------
    MapSonarQuality mapQuality() const { return m_map_quality; }
    // Persists to QSettings and emits displayStateChanged({}, MapQuality) when changed.
    void setMapQuality(MapSonarQuality q);

    // -- Global defaults (delegated to AppState) -------------------------------
    int    defaultPalette() const;
    QColor mapBackground()  const;
    int    coordFormat()    const;

    // -- Per-layer mutation (coordinator over DataLayer) -----------------------
    // Each typed setter writes the model field, marks the project dirty, and emits
    // displayStateChanged(layer_id, <aspect>). Returns false if the layer is gone.
    bool setLayerVisible   (const std::string& layer_id, bool visible);
    bool setLayerSssPalette(const std::string& layer_id, int palette_idx);
    bool setLayerSbpPalette(const std::string& layer_id, int palette_idx);
    // For callers that mutate a layer's display struct directly (e.g. the waterfall
    // applying full params): announce the change so other views stay in sync.
    void notifyLayerChanged(const std::string& layer_id, DisplayAspect aspect);

    // Load owned per-view state from QSettings (call once at startup).
    void loadPersistentState();

signals:
    // layer_id empty = a per-view or global change; otherwise the affected layer.
    void displayStateChanged(const QString& layer_id, dolphin::ui::DisplayAspect aspect);

private:
    app::DataLayer* layerById(const std::string& layer_id) const;

    AppState*        m_app_state = nullptr;
    app::Project*    m_project   = nullptr;
    MapSonarQuality  m_map_quality = MapSonarQuality::CoverageOnly;
};

} // namespace dolphin::ui
