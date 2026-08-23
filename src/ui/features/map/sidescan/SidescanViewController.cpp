// SidescanViewController.cpp — constructor.
// Logic is split across companion files:
//   SidescanMapQuality.cpp    — setMapSonarQuality
//   SidescanMapLoadTask.cpp   — activateLayer (background load task)
//   SidescanMapDiagnostics.cpp — setGeorefParams, reloadCurrentLayer,
//                                setPaletteIndex, unloadLayer, deactivate
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/MapView.h"
#include "ui/shared/settings/SettingsKeys.h"
#include "render/sonar/SSSPalette.h"
#include "app/tasks/OperationManager.h"

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
    // The application settings store the default palette as a name string.
    QSettings qs;
    const QVariant sss_var = qs.value(QStringLiteral("sss/paletteIdx"));
    if (sss_var.isValid()) {
        m_palette_idx = sss_var.toInt();
    } else {
        m_palette_idx = SSSPalette::indexFromName(
            qs.value(SettingsKeys::kDefaultPalette,
                     QStringLiteral("Gray")).toString());
    }

    // Operator display preference: show the near-nadir seabed band (default)
    // or leave the QC gap open. Owned here; Views ▸ SSS toggles it.
    m_georef_params.show_nadir =
        qs.value(QStringLiteral("sss/showNadir"), true).toBool();

    // Staged-load upgrade: when a heavy tier (High/Full) was requested, activateLayer
    // paints a fast Medium preview, then prebuilds the full tier in the background.
    // When it lands, swap it in — but only if the user hasn't switched quality since
    // and the layer is still on the map.
    connect(this, &SidescanViewController::prebuildTierComplete, this,
            [this](const std::string& layer_id, MapSonarQuality quality) {
                if (m_staged_refreshes.count(layer_id)) return;
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
        if (!layer_id.empty() && m_loaded_layers.count(layer_id)) {
            // The authoritative store/index changed (including reverting the last
            // baked correction to the imported baseline). Invalidate every cache,
            // but retain the old map until a Low raster from the new store is
            // ready; then upgrade to the requested image tier.
            if (m_op_mgr) {
                m_op_mgr->cancelByKey("sss:load:" + layer_id);
                m_op_mgr->cancelByPrefix("sss:prebuild:" + layer_id + ":");
            }
            m_resident_quality.erase(layer_id);
            m_layer_intensity_cache.erase(layer_id);
            m_quality_tier_cache.erase(layer_id);
            m_staged_refreshes.erase(layer_id);
            applyGeometryCorrections({layer_id});
        }
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
