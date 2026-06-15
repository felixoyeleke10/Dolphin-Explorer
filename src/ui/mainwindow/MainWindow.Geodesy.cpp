// MainWindow.Geodesy.cpp — onGeodeticSettings standalone window.
#include "ui/mainwindow/MainWindow.h"
#include "ui/shared/UiUtils.h"
#include "ui/features/geodesy/GeodesyPanel.h"
#include "ui/features/map/MapView.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "geo/EpsgDatabase.h"
#include "core/SpatialRef.h"
#include "app/project/Project.h"
#include "app/project/ProjectTransaction.h"
#include "app/layers/DataLayer.h"

#include <QDialog>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kGeodesyMinW = 360;
static constexpr int kGeodesyMinH = 500;

void MainWindow::onGeodeticSettings()
{
    // -- Create the window once; reuse on subsequent calls -----------------
    if (!m_geodesy_win) {
        m_geodesy_win = new QDialog(this, Qt::Dialog);
        m_geodesy_win->setObjectName("geodesyWin");
        m_geodesy_win->setWindowTitle(tr("Geodetic Settings"));
        m_geodesy_win->setMinimumSize(kGeodesyMinW, kGeodesyMinH);
        m_geodesy_win->setModal(false);

        auto* vl = makeCompactLayout<QVBoxLayout>(m_geodesy_win);

        m_geodesy_panel = new GeodesyPanel(m_geodesy_win);
        vl->addWidget(m_geodesy_panel);

        // -- Signal wiring -------------------------------------------------
        connect(m_geodesy_panel, &GeodesyPanel::crsChanged,
                this, [this](const core::SpatialRef& crs) {
                    m_session_ctrl->setPendingCrs(crs);
                    if (crs.empty())
                        appendJobMessage(tr("Source CRS reset to Auto-detect"));
                    else
                        appendJobMessage(
                            tr("Source CRS set to %1")
                                .arg(QString::fromStdString(
                                    geo::epsgDisplayName(crs))));
                });

        connect(m_geodesy_panel, &GeodesyPanel::applyRequested,
                this, [this](const core::SpatialRef& crs, bool force) {
                    if (!currentProject() || crs.empty()) return;
                    int n_applied = 0;
                    app::ProjectTransaction tx(currentProject());
                    for (const auto& layer : currentProject()->layers()) {
                        if (!layer) continue;
                        if (!core::spatialRefIsProjected(layer->source_spatial_ref)) continue;
                        if (layer->source_spatial_ref.exact && !force) continue;
                        layer->source_spatial_ref       = crs;
                        layer->source_spatial_ref.exact = true;
                        ++n_applied;
                    }
                    for (const auto& layer : currentProject()->layers()) {
                        if (!layer) continue;
                        auto* src = currentProject()->findSource(layer->source_id);
                        if (!src) continue;
                        if (!core::spatialRefIsProjected(src->source_spatial_ref)) continue;
                        if (src->source_spatial_ref.exact && !force) continue;
                        src->source_spatial_ref       = crs;
                        src->source_spatial_ref.exact = true;
                    }
                    tx.commit();
                    m_geodesy_panel->refresh(currentProject(), m_session_ctrl->pendingCrs());
                    if (!activeLayerId().empty()) {
                        const auto* al = currentProject()->findLayer(activeLayerId());
                        // Non-SSS active layer: rebuild the nav track with the new CRS.
                        // SSS tiles are reloaded by the CrsChanged broadcast below.
                        if (m_map_view && al && al->modality != app::Modality::Sidescan)
                            m_map_view->setActiveLayer(activeLayerId());
                    }
                    if (m_inspector && !activeLayerId().empty()) {
                        if (auto* layer = currentProject()->findLayer(activeLayerId()))
                            m_inspector->showLayer(layer);
                    }
                    m_window_registry->broadcast(
                        ViewerRefreshReason::CrsChanged, activeLayerId());
                    if (!activeLayerId().empty())
                        applyStoredNavParams(activeLayerId());
                    appendJobMessage(
                        tr("Source CRS applied to %1 layer(s)").arg(n_applied));
                });
    }

    m_geodesy_panel->refresh(currentProject(), m_session_ctrl->pendingCrs());
    m_geodesy_win->show();
    m_geodesy_win->raise();
    m_geodesy_win->activateWindow();
}

} // namespace dolphin::ui
