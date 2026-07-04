// MainWindow.MapContextMenu.cpp — right-click context menu for the map view.
// The menu is described declaratively as a list of MenuEntry (see MenuSpec.h) and
// rendered by buildMenu(); the structure is data, not imperative addAction() surgery.
#include "ui/mainwindow/MainWindow.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/map/MapView3D.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/shared/widgets/MenuSpec.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"

#include <QKeySequence>
#include <QMenu>

#include <algorithm>
#include <initializer_list>
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
    const std::string layer_id = !clicked_id.empty() ? clicked_id : activeLayerId();
    app::DataLayer* layer = (currentProject() && !layer_id.empty())
        ? currentProject()->findLayer(layer_id)
        : nullptr;
    const bool has_layer  = layer != nullptr;
    const bool is_sidescan = layer && layer->modality == app::Modality::Sidescan;
    const bool is_sbp      = layer && layer->modality == app::Modality::SubBottom;

    if (!clicked_id.empty() && clicked_id != activeLayerId())
        onLayerSelected(clicked_id);

    const bool has_project = currentProject() != nullptr;

    // Reorder a layer relative to its siblings. mode: -2 front, -1 up, 1 down, 2 back.
    auto reorderLayer = [this](const std::string& id, int mode) {
        if (!currentProject() || id.empty()) return;
        std::vector<std::string> ids;
        ids.reserve(currentProject()->layers().size());
        for (const auto& l : currentProject()->layers())
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

        currentProject()->reorderLayers(ids);
    };

    using K = Qt::Key;
    using ME = MenuEntry;

    // -- The menu, as data -----------------------------------------------------
    std::vector<MenuEntry> entries;

    entries.push_back(ME::action(
        is_sbp ? tr("Open sub-bottom viewer") : tr("Open waterfall"),
        [this] {
            if (!currentProject() || activeLayerId().empty()) return;
            const auto* active = currentProject()->findLayer(activeLayerId());
            if (active && active->modality == app::Modality::SubBottom) onSubBottomOpen();
            else                                                        onWaterfallOpen();
        },
        has_layer && (is_sidescan || is_sbp)));
    entries.push_back(ME::action(tr("Open metadata viewer"),
        [this] { onWaterfallMetadata(); }, is_sidescan));
    entries.push_back(ME::action(tr("Zoom to"),
        [this, layer_id] { if (m_map_view && !layer_id.empty()) m_map_view->fitToLayer(layer_id); },
        has_layer, QKeySequence(K::Key_F2)));

    entries.push_back(ME::separator());

    entries.push_back(ME::submenu(tr("Group"), {
        ME::action(tr("Create group from selection"), {}, false),
        ME::action(tr("Move to group"),               {}, false),
    }, has_layer));
    entries.push_back(ME::submenu(tr("Tags"), {
        ME::action(tr("Add tag"),    {}, false),
        ME::action(tr("Clear tags"), {}, false),
    }, has_layer));
    entries.push_back(ME::action(tr("Edit navigation"),
        [this] { onRenumberContacts(); }, is_sidescan));

    entries.push_back(ME::separator());

    entries.push_back(ME::action(tr("Bring to front"),
        [reorderLayer, layer_id] { reorderLayer(layer_id, -2); },
        has_layer, QKeySequence(Qt::CTRL | K::Key_Home)));
    entries.push_back(ME::action(tr("Move up"),
        [reorderLayer, layer_id] { reorderLayer(layer_id, -1); },
        has_layer, QKeySequence(Qt::CTRL | K::Key_PageUp)));
    entries.push_back(ME::action(tr("Move down"),
        [reorderLayer, layer_id] { reorderLayer(layer_id, 1); },
        has_layer, QKeySequence(Qt::CTRL | K::Key_PageDown)));
    entries.push_back(ME::action(tr("Send to back"),
        [reorderLayer, layer_id] { reorderLayer(layer_id, 2); },
        has_layer, QKeySequence(Qt::CTRL | K::Key_End)));

    entries.push_back(ME::separator());

    entries.push_back(ME::submenu(tr("Import"), {
        ME::action(tr("Import survey files..."), [this] { onImportFile(); }, has_project),
    }));
    entries.push_back(ME::submenu(tr("Export"), {}, /*enabled=*/false));  // not implemented (D-05)

    entries.push_back(ME::separator());

    entries.push_back(ME::action(tr("Rename"),
        [this, layer_id] { if (!layer_id.empty()) onRenameLayer(layer_id); }, has_layer));
    entries.push_back(ME::action(tr("Delete"),
        [this, layer_id] { if (!layer_id.empty()) onRemoveLayer(layer_id); },
        has_layer, QKeySequence(QKeySequence::Delete)));

    if (is_3d_mode && m_viewport_host && m_viewport_host->view3D()) {
        MapView3D* v3d = m_viewport_host->view3D();
        entries.push_back(ME::separator());
        entries.push_back(ME::action(tr("Reset Camera"), [v3d] { v3d->resetCamera(); }));
        // Terrain loads through the Views ▸ MAP ▸ Draping surface flow so the
        // choice persists with the project.
        entries.push_back(ME::action(tr("Set Draping Surface..."),
            [this] { onChooseDrapingSurface(); }));
    }

    entries.push_back(ME::separator());

    // Sonar Preview quality — checkable radio set reflecting the current tier.
    const int cur_q = m_display_state ? static_cast<int>(m_display_state->mapQuality())
                                      : static_cast<int>(MapSonarQuality::CoverageOnly);
    struct QEntry { MapSonarQuality q; const char* label; };
    static constexpr QEntry kQ[] = {
        { MapSonarQuality::Off,          "Off"           },
        { MapSonarQuality::CoverageOnly, "Coverage Only" },
        { MapSonarQuality::Low,          "Low"           },
        { MapSonarQuality::Medium,       "Medium"        },
        { MapSonarQuality::High,         "High"          },
    };
    std::vector<MenuEntry> quality;
    for (const auto& e : kQ) {
        const MapSonarQuality q = e.q;
        quality.push_back(ME::check(tr(e.label), static_cast<int>(q) == cur_q,
                                    [this, q] { onMapSonarQuality(q); }));
        if (q == MapSonarQuality::CoverageOnly)
            quality.push_back(ME::separator());
    }
    entries.push_back(ME::submenu(tr("Sonar Preview"), std::move(quality)));

    buildMenu(menu, entries);
    menu.exec(globalPos);
}

} // namespace dolphin::ui
