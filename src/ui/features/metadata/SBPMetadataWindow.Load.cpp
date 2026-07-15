// SBPMetadataWindow.Load.cpp — project binding, async trace loading, line selection.
#include "ui/features/metadata/SBPMetadataWindow.h"
#include "app/project/Project.h"
#include "app/services/ImportService.h"

#include <QAction>
#include <QFutureWatcher>
#include <QListWidget>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Data loading
// -----------------------------------------------------------------------------
void SBPMetadataWindow::setProject(app::Project*       project,
                                   const std::string&  active_layer_id)
{
    m_project         = project;
    m_active_layer_id = active_layer_id;

    m_line_menu->clear();
    if (!m_project) { updateLineButtonLabel(); return; }

    for (const auto& layer : m_project->layers()) {
        if (layer->modality != app::Modality::SubBottom) continue;
        auto* act = m_line_menu->addAction(QString::fromStdString(layer->label));
        act->setCheckable(true);
        act->setData(QString::fromStdString(layer->id));
        act->setChecked(layer->id == active_layer_id || active_layer_id.empty());
        connect(act, &QAction::toggled, this, &SBPMetadataWindow::onLineSelectionChanged);
    }
    updateLineButtonLabel();
    startLoad();
}

void SBPMetadataWindow::startLoad()
{
    const int gen = ++m_load_gen;
    if (!m_project) return;

    struct LoadItem {
        std::string sp, sf, src_path;
        core::ArtifactIndex idx;
    };
    QVector<LoadItem> items;

    for (auto* act : m_line_menu->actions()) {
        if (!act->isCheckable() || !act->isChecked()) continue;
        const QString id = act->data().toString();
        for (const auto& layer : m_project->layers()) {
            if (QString::fromStdString(layer->id) != id) continue;
            if (!layer->index_built) break;
            const auto* src = m_project->findSource(layer->source_id);
            items.push_back({layer->artifact_store_path,
                             layer->artifact_store_format,
                             src ? src->path : std::string{},
                             layer->artifact_index});
            break;
        }
    }

    if (items.isEmpty()) {
        m_model->setTraces({});
        m_all_traces.clear();
        m_load_status->setText("No lines selected");
        updatePlot();
        updateChart();
        return;
    }

    m_load_status->setText("Loading…");
    m_model->setTraces({});
    m_all_traces.clear();

    auto* watcher = new QFutureWatcher<std::vector<core::SubBottomTrace>>(this);
    connect(watcher, &QFutureWatcher<std::vector<core::SubBottomTrace>>::finished,
            this, [this, watcher, gen] {
                watcher->deleteLater();
                if (gen != m_load_gen) return;
                try { onTracesLoaded(watcher->result()); }
                catch (...) { m_load_status->setText("Load failed"); }
            });
    watcher->setFuture(QtConcurrent::run(
        [items]() -> std::vector<core::SubBottomTrace> {
            std::vector<core::SubBottomTrace> all;
            for (const auto& item : items) {
                auto traces = app::ImportService::loadAllSubBottomTraces(
                    item.sp, item.sf, item.idx, item.src_path);
                all.insert(all.end(),
                           std::make_move_iterator(traces.begin()),
                           std::make_move_iterator(traces.end()));
            }
            std::sort(all.begin(), all.end(),
                [](const core::SubBottomTrace& a, const core::SubBottomTrace& b) {
                    return a.timestamp_us < b.timestamp_us;
                });
            return all;
        }));
}

void SBPMetadataWindow::onTracesLoaded(std::vector<core::SubBottomTrace> traces)
{
    m_all_traces = std::move(traces);
    bool projected = false;
    for (const auto& t : m_all_traces)
        if (t.nav.lat != 0.0 || t.nav.lon != 0.0) { projected = t.nav.is_projected; break; }
    m_model->setCoordinatesProjected(projected);
    if (m_field_list->count() > 9) {
        m_field_list->item(4)->setText(projected ? "Northing (m)"        : "Latitude (°)");
        m_field_list->item(5)->setText(projected ? "Easting (m)"         : "Longitude (°)");
        m_field_list->item(6)->setText(projected ? "Fish Northing (m)"   : "Fish Lat (°)");
        m_field_list->item(7)->setText(projected ? "Fish Easting (m)"    : "Fish Lon (°)");
        m_field_list->item(8)->setText(projected ? "Vessel Northing (m)" : "Vessel Lat (°)");
        m_field_list->item(9)->setText(projected ? "Vessel Easting (m)"  : "Vessel Lon (°)");
    }
    m_model->setTraces(m_all_traces);
    m_load_status->setText(QString("%1 traces").arg(m_model->rowCount()));
    updatePlot();
    updateChart();
}

void SBPMetadataWindow::updateLineButtonLabel()
{
    int total = 0, checked = 0;
    for (auto* act : m_line_menu->actions()) {
        if (!act->isCheckable()) continue;
        ++total;
        if (act->isChecked()) ++checked;
    }
    if      (total == 0)        m_line_btn->setText("No lines ▾");
    else if (checked == 0)      m_line_btn->setText("None selected ▾");
    else if (checked == total)  m_line_btn->setText("All lines ▾");
    else m_line_btn->setText(QString("%1 / %2 lines ▾").arg(checked).arg(total));
}

void SBPMetadataWindow::onLineSelectionChanged()
{
    updateLineButtonLabel();
    startLoad();
}

} // namespace dolphin::ui
