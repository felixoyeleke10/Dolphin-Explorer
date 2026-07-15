// SSSMetadataLoad.cpp — project/data loading for SSSMetadataWindow.
#include "ui/features/metadata/SSSMetadataWindow.h"
#include "ui/features/map/sidescan/SidescanEntryFilter.h"
#include "app/layers/DataLayer.h"
#include "app/services/ImportService.h"
#include "app/project/Project.h"

#include <QAction>
#include <QCheckBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>

namespace dolphin::ui {

void SSSMetadataWindow::setProject(app::Project*        project,
                                    const std::string&   active_layer_id)
{
    m_project         = project;
    m_active_layer_id = active_layer_id;

    m_line_menu->clear();
    if (!m_project) { updateLineButtonLabel(); return; }

    const auto& layers = m_project->layers();
    for (const auto& layer : layers) {
        auto* act = m_line_menu->addAction(QString::fromStdString(layer->label));
        act->setCheckable(true);
        act->setData(QString::fromStdString(layer->id));
        act->setChecked(layer->id == active_layer_id || active_layer_id.empty());
        connect(act, &QAction::toggled, this, &SSSMetadataWindow::onLineSelectionChanged);
    }
    updateLineButtonLabel();
    startLoad();
}

void SSSMetadataWindow::setVisiblePingRange(int first_ping, int count)
{
    m_visible_first = first_ping;
    m_visible_count = count;
}

void SSSMetadataWindow::startLoad()
{
    const int gen = ++m_load_gen;
    if (!m_project) return;

    struct LoadItem {
        std::string sp, sf, src_path;
        core::ArtifactIndex idx;
    };
    QVector<LoadItem> items;
    const auto& layers = m_project->layers();
    for (auto* act : m_line_menu->actions()) {
        if (!act->isCheckable() || !act->isChecked()) continue;
        const QString id = act->data().toString();
        for (const auto& layer : layers) {
            if (QString::fromStdString(layer->id) != id) continue;
            if (!layer->index_built) break;
            const app::ProjectSource* src = m_project->findSource(layer->source_id);

            core::ArtifactIndex filtered = layer->artifact_index;
            if (layer->low_frequency_hz == 0.f)
                filterSidescanEntriesByBand(filtered, layer->frequency_hz);

            items.push_back({layer->artifact_store_path,
                             layer->artifact_store_format,
                             src ? src->path : std::string{},
                             std::move(filtered)});
            break;
        }
    }

    if (items.isEmpty()) {
        m_model->setPings({});
        m_all_pings.clear();
        m_load_status->setText("No lines selected");
        updatePlot(); updateChart();
        return;
    }

    m_load_status->setText("Loading…");
    m_model->setPings({});
    m_all_pings.clear();

    auto* watcher = new QFutureWatcher<std::vector<core::SidescanPing>>(this);
    connect(watcher, &QFutureWatcher<std::vector<core::SidescanPing>>::finished,
            this, [this, watcher, gen]() {
                watcher->deleteLater();
                if (gen != m_load_gen) return;
                try { onPingsLoaded(watcher->result()); }
                catch (...) { m_load_status->setText("Load failed"); }
            });
    watcher->setFuture(QtConcurrent::run(
        [items]() -> std::vector<core::SidescanPing> {
            std::vector<core::SidescanPing> all;
            for (const auto& item : items) {
                auto pings = app::ImportService::loadAllSidescanNavFromStore(
                    item.sp, item.sf, item.idx, item.src_path);
                all.insert(all.end(),
                           std::make_move_iterator(pings.begin()),
                           std::make_move_iterator(pings.end()));
            }
            std::sort(all.begin(), all.end(),
                [](const core::SidescanPing& a, const core::SidescanPing& b){
                    return a.timestamp_us < b.timestamp_us; });
            return all;
        }));
}

void SSSMetadataWindow::onPingsLoaded(std::vector<core::SidescanPing> pings)
{
    m_all_pings = std::move(pings);
    bool projected = false;
    for (const auto& p : m_all_pings) {
        if (p.nav.lat != 0.0 || p.nav.lon != 0.0) { projected = p.nav.is_projected; break; }
    }
    m_model->setCoordinatesProjected(projected);
    if (m_field_list->count() > 10) {
        m_field_list->item(5)->setText(projected ? "Northing (m)"        : "Latitude (°)");
        m_field_list->item(6)->setText(projected ? "Easting (m)"         : "Longitude (°)");
        m_field_list->item(7)->setText(projected ? "Fish Northing (m)"   : "Fish Lat (°)");
        m_field_list->item(8)->setText(projected ? "Fish Easting (m)"    : "Fish Lon (°)");
        m_field_list->item(9)->setText(projected ? "Vessel Northing (m)" : "Vessel Lat (°)");
        m_field_list->item(10)->setText(projected ? "Vessel Easting (m)" : "Vessel Lon (°)");
    }
    applyVisibleFilter();
}

void SSSMetadataWindow::applyVisibleFilter()
{
    if (m_visible_only_cb->isChecked() && m_visible_count > 0) {
        const size_t first = static_cast<size_t>(std::max(0, m_visible_first));
        const size_t count = static_cast<size_t>(std::max(0, m_visible_count));
        std::vector<core::SidescanPing> filtered;
        if (first < m_all_pings.size()) {
            const size_t last = std::min(first + count, m_all_pings.size());
            filtered.assign(m_all_pings.begin() + static_cast<ptrdiff_t>(first),
                            m_all_pings.begin() + static_cast<ptrdiff_t>(last));
        }
        m_model->setPings(std::move(filtered));
    } else {
        m_model->setPings(m_all_pings);
    }
    m_load_status->setText(QString("%1 pings").arg(m_model->rowCount()));
    updatePlot();
    updateChart();
}

void SSSMetadataWindow::updateLineButtonLabel()
{
    int total = 0, checked = 0;
    for (auto* act : m_line_menu->actions()) {
        if (!act->isCheckable()) continue;
        ++total;
        if (act->isChecked()) ++checked;
    }
    if (total == 0)            m_line_btn->setText("No lines ▾");
    else if (checked == 0)     m_line_btn->setText("None selected ▾");
    else if (checked == total) m_line_btn->setText("All lines ▾");
    else                       m_line_btn->setText(QString("%1 / %2 lines ▾").arg(checked).arg(total));
}

void SSSMetadataWindow::onLineSelectionChanged() { updateLineButtonLabel(); m_load_debounce->start(); }
void SSSMetadataWindow::onShowOnlyVisibleToggled(bool) { applyVisibleFilter(); }

} // namespace dolphin::ui
