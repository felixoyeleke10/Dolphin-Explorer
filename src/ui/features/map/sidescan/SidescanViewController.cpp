// SidescanViewController.cpp — constructor.
// Logic is split across companion files:
//   SidescanMapQuality.cpp    — setMapSonarQuality
//   SidescanMapLoadTask.cpp   — activateLayer (background load task)
//   SidescanMapDiagnostics.cpp — setGeorefParams, reloadCurrentLayer,
//                                setPaletteIndex, unloadLayer, deactivate
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/shared/dialogs/SettingsDialog.h"
#include "render/sonar/SSSPalette.h"

#include <QSettings>

namespace dolphin::ui {

SidescanViewController::SidescanViewController(MapView*            map_view,
                                               app::ImportService* import_service,
                                               QLabel*             status_ping,
                                               QLabel*             status_pos,
                                               QLabel*             status_depth,
                                               QObject*            parent)
    : QObject(parent)
    , m_map_view(map_view)
    , m_import_service(import_service)
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
