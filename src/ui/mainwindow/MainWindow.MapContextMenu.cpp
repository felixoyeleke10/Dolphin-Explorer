// MainWindow.MapContextMenu.cpp — right-click context menu for the map view.
#include "ui/mainwindow/MainWindow.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/map/MapView3D.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"

#include <QActionGroup>
#include <QKeySequence>
#include <QMenu>

#include <algorithm>
#include <vector>

namespace dolphin::ui {

void MainWindow::onMapContextMenu(QPoint globalPos)
{
    QMenu menu;

    const bool is_3d_mode = m_viewport_host && m_viewport_host->isMode3D();
    const QPoint local_pos = m_map_view ? m_map_view->mapFromGlobal(globalPos) : QPoint{};
    const std::string clicked_id = (!is_3d_mode && m_map_view)
        ? m_map_view->hitTestLayer(local_pos)
        : std::string{};
    const std::string layer_id = !clicked_id.empty() ? clicked_id : m_active_layer_id;
    app::DataLayer* layer = (m_project && !layer_id.empty())
        ? m_project->findLayer(layer_id)
        : nullptr;
    const bool has_layer  = layer != nullptr;
    const bool is_sidescan = layer && layer->modality == app::Modality::Sidescan;
    const bool is_sbp      = layer && layer->modality == app::Modality::SubBottom;

    if (!clicked_id.empty() && clicked_id != m_active_layer_id)
        onLayerSelected(clicked_id);

    auto addLayerAction = [&](const QString& text, auto slot) {
        QAction* act = menu.addAction(text, this, [this, layer_id, slot] {
            if (!layer_id.empty()) slot(layer_id);
        });
        act->setEnabled(has_layer);
        return act;
    };

    QAction* open_viewer = menu.addAction(
        is_sbp ? tr("Open sub-bottom viewer") : tr("Open waterfall"),
        this, [this] {
            if (!m_project || m_active_layer_id.empty()) return;
            const auto* active = m_project->findLayer(m_active_layer_id);
            if (active && active->modality == app::Modality::SubBottom)
                onSubBottomOpen();
            else
                onWaterfallOpen();
        });
    open_viewer->setEnabled(has_layer && (is_sidescan || is_sbp));

    QAction* metadata = menu.addAction(tr("Open metadata viewer"), this, [this] {
        onWaterfallMetadata();
    });
    metadata->setEnabled(is_sidescan);

    QAction* zoom_to = addLayerAction(tr("Zoom to"), [this](const std::string& id) {
        if (m_map_view) m_map_view->fitToLayer(id);
    });
    zoom_to->setShortcut(QKeySequence(Qt::Key_F2));

    menu.addSeparator();

    QMenu* group_menu = menu.addMenu(tr("Group"));
    group_menu->setEnabled(has_layer);
    group_menu->addAction(tr("Create group from selection"))->setEnabled(false);
    group_menu->addAction(tr("Move to group"))->setEnabled(false);

    QMenu* tags_menu = menu.addMenu(tr("Tags"));
    tags_menu->setEnabled(has_layer);
    tags_menu->addAction(tr("Add tag"))->setEnabled(false);
    tags_menu->addAction(tr("Clear tags"))->setEnabled(false);

    QAction* edit_nav = menu.addAction(tr("Edit navigation"), this, [this] {
        onRenumberContacts();
    });
    edit_nav->setEnabled(is_sidescan);

    menu.addSeparator();

    auto reorderLayer = [this](const std::string& id, int mode) {
        if (!m_project || id.empty()) return;
        std::vector<std::string> ids;
        ids.reserve(m_project->layers().size());
        for (const auto& l : m_project->layers())
            if (l) ids.push_back(l->id);

        auto it = std::find(ids.begin(), ids.end(), id);
        if (it == ids.end()) return;
        const size_t idx = static_cast<size_t>(std::distance(ids.begin(), it));
        if (mode == -1 && idx > 0) {
            std::swap(ids[idx], ids[idx - 1]);
        } else if (mode == 1 && idx + 1 < ids.size()) {
            std::swap(ids[idx], ids[idx + 1]);
        } else if (mode == -2 && idx > 0) {
            ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(idx));
            ids.insert(ids.begin(), id);
        } else if (mode == 2 && idx + 1 < ids.size()) {
            ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(idx));
            ids.push_back(id);
        } else {
            return;
        }

        m_project->reorderLayers(ids);
    };

    QAction* bring_front = addLayerAction(tr("Bring to front"), [reorderLayer](const std::string& id) {
        reorderLayer(id, -2);
    });
    bring_front->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home));

    QAction* move_up = addLayerAction(tr("Move up"), [reorderLayer](const std::string& id) {
        reorderLayer(id, -1);
    });
    move_up->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp));

    QAction* move_down = addLayerAction(tr("Move down"), [reorderLayer](const std::string& id) {
        reorderLayer(id, 1);
    });
    move_down->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown));

    QAction* send_back = addLayerAction(tr("Send to back"), [reorderLayer](const std::string& id) {
        reorderLayer(id, 2);
    });
    send_back->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_End));

    menu.addSeparator();

    QMenu* import_menu = menu.addMenu(tr("Import"));
    import_menu->addAction(tr("Import survey files..."), this, &MainWindow::onImportFile)
        ->setEnabled(m_project != nullptr);

    QMenu* export_menu = menu.addMenu(tr("Export"));
    export_menu->setEnabled(has_layer);
    export_menu->addAction(tr("CSV..."), this, [this, layer_id] {
        onExportLayers({ layer_id }, QStringLiteral("csv"));
    });
    export_menu->addAction(tr("GeoTIFF..."), this, [this, layer_id] {
        onExportLayers({ layer_id }, QStringLiteral("geotiff"));
    });

    QAction* processing_settings = menu.addAction(tr("Processing settings"), this,
        &MainWindow::onRunSelectedLayer);
    processing_settings->setEnabled(has_layer);

    QAction* navigation_settings = menu.addAction(tr("Navigation settings"), this,
        &MainWindow::onGeodeticSettings);
    navigation_settings->setEnabled(m_project != nullptr);

    menu.addSeparator();

    addLayerAction(tr("Rename"), [this](const std::string& id) {
        onRenameLayer(id);
    });

    QAction* del = addLayerAction(tr("Delete"), [this](const std::string& id) {
        onRemoveLayer(id);
    });
    del->setShortcut(QKeySequence::Delete);

    QAction* properties = menu.addAction(tr("Properties"), this, [this] {
        onTogglePropertiesPanel();
    });
    properties->setEnabled(has_layer);

    menu.addSeparator();

    menu.addAction(tr("Fit to Data"), this, [this] {
        if (m_map_view) m_map_view->fitToDataAndReset();
    });

    if (is_3d_mode && m_viewport_host && m_viewport_host->view3D()) {
        menu.addAction(tr("Fit to Scene"), m_viewport_host->view3D(), &MapView3D::fitToScene);
        menu.addAction(tr("Reset Camera"), m_viewport_host->view3D(), &MapView3D::resetCamera);
        menu.addAction(tr("Load Terrain..."), m_viewport_host, &MapViewportHost::promptLoadTerrain);
    }

    QMenu* quality_menu = menu.addMenu(tr("Sonar Preview"));
    auto*  ag           = new QActionGroup(&menu);
    ag->setExclusive(true);

    struct Entry { MapSonarQuality q; const char* label; };
    static constexpr Entry kEntries[] = {
        { MapSonarQuality::Off,          "Off"                  },
        { MapSonarQuality::CoverageOnly, "Coverage Only"        },
        { MapSonarQuality::Low,          "Low"                  },
        { MapSonarQuality::Medium,       "Medium"               },
        { MapSonarQuality::High,         "High"                 },
        { MapSonarQuality::Full,         "Full / Best Available"},
    };

    const int cur_q = m_sss_ctrl ? static_cast<int>(m_sss_ctrl->mapSonarQuality())
                                 : static_cast<int>(MapSonarQuality::CoverageOnly);
    for (const auto& e : kEntries) {
        auto* act = quality_menu->addAction(tr(e.label));
        act->setCheckable(true);
        act->setChecked(static_cast<int>(e.q) == cur_q);
        ag->addAction(act);
        const MapSonarQuality q = e.q;
        connect(act, &QAction::triggered, this, [this, q] { onMapSonarQuality(q); });
        if (e.q == MapSonarQuality::CoverageOnly)
            quality_menu->addSeparator();
    }

    menu.exec(globalPos);
}

} // namespace dolphin::ui
