// SidescanViewController.cpp — constructor.
// Logic is split across companion files:
//   SidescanMapQuality.cpp    — setMapSonarQuality
//   SidescanMapLoadTask.cpp   — activateLayer (background load task)
//   SidescanMapDiagnostics.cpp — setGeorefParams, reloadCurrentLayer,
//                                setPaletteIndex, unloadLayer, deactivate
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/MapView.h"
#include "ui/shared/dialogs/SettingsDialog.h"
#include "render/sonar/SSSPalette.h"

#include <QSettings>

namespace dolphin::ui {

SidescanViewController::SidescanViewController(MapView*            map_view,
                                               QLabel*             status_ping,
                                               QLabel*             status_pos,
                                               QLabel*             status_depth,
                                               QObject*            parent)
    : QObject(parent)
    , m_map_view(map_view)
    , m_status_ping(status_ping)
    , m_status_pos(status_pos)
    , m_status_depth(status_depth)
{
    // Initialise palette from QSettings.
    // "sss/paletteIdx" is the SSS-specific override written by setPaletteIndex().
    // If absent (first launch or migrated install) fall back to the app-wide
    // default palette stored by SettingsDialog / AppSettingsDialog as a name string.
    QSettings qs;
    const QVariant sss_var = qs.value(QStringLiteral("sss/paletteIdx"));
    if (sss_var.isValid()) {
        m_palette_idx = sss_var.toInt();
    } else {
        m_palette_idx = SSSPalette::indexFromName(
            qs.value(SettingsDialog::kKeyDefaultPalette,
                     QStringLiteral("Gray")).toString());
    }

    // Staged-load upgrade: when a heavy tier (High/Full) was requested, activateLayer
    // paints a fast Medium preview, then prebuilds the full tier in the background.
    // When it lands, swap it in — but only if the user hasn't switched quality since
    // and the layer is still on the map.
    connect(this, &SidescanViewController::prebuildTierComplete, this,
            [this](const std::string& layer_id, MapSonarQuality quality) {
                if (quality != m_quality) return;
                if (!m_loaded_layers.count(layer_id)) return;
                if (applyCachedTier(layer_id, quality) && m_map_view
                        && layer_id == m_active_layer_id)
                    m_map_view->setActiveLayer(layer_id);
            });
}


void SidescanViewController::onViewerRefresh(ViewerRefreshReason reason,
                                              const std::string& layer_id)
{
    switch (reason) {
    case ViewerRefreshReason::LayerDataChanged:
        if (!layer_id.empty() && m_loaded_layers.count(layer_id))
            reloadLayer(layer_id);
        else if (layer_id.empty() && !m_active_layer_id.empty())
            reloadCurrentLayer();
        break;
    case ViewerRefreshReason::CrsChanged:
        // Always reload all loaded SSS tiles: the Geodesy panel applies the CRS
        // change to every layer in the project, so even a non-SSS active_layer_id
        // in the broadcast means SSS source CRSes are stale.
        if (!m_active_layer_id.empty())
            reloadCurrentLayer();
        break;
    case ViewerRefreshReason::DisplaySettingsChanged:
        // Palette changes arrive via setPaletteIndex() (triggered by the
        // defaultPaletteChanged signal) which already calls repaletteAllLayers().
        // Other display settings (grid, background colour, etc.) do not affect
        // the SSS swath image, so there is nothing to do here.
        break;
    case ViewerRefreshReason::ProjectReplaced:
        deactivate(true);
        break;
    }
}

} // namespace dolphin::ui
