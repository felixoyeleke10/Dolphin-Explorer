// DataLibraryWindow.Refresh.cpp — filter application and all table/status refresh functions.

#include "ui/features/datalibrary/DataLibraryWindow.h"
#include "ui/shared/CoordFormat.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/layers/LayerUtils.h"
#include "app/contracts/ContractTypes.h"
#include "core/Contact.h"
#include "core/ModalityCapabilities.h"
#include "core/SpatialRef.h"

#include <algorithm>

#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QRadioButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>

namespace dolphin::ui {

// -- Constants -----------------------------------------------------------------
namespace {
    constexpr QRgb kClrReady   = 0xff28a745u;
    constexpr QRgb kClrFailed  = 0xffdc3545u;
    constexpr QRgb kClrPending = 0xfffd7e14u;
}

// -- Formatting helpers --------------------------------------------------------

static QString fmtBytes(uint64_t b)
{
    if (b < 1024)           return QString::number(b) + " B";
    if (b < 1024ull * 1024) return QString::number(b / 1024) + " KB";
    if (b < 1024ull * 1024 * 1024)
        return QString::number(b / 1024.0 / 1024.0, 'f', 1) + " MB";
    return QString::number(b / 1024.0 / 1024.0 / 1024.0, 'f', 2) + " GB";
}

static QString fmtDuration(double start_s, double end_s)
{
    if (start_s <= 0.0 || end_s <= start_s) return {};
    const int s = static_cast<int>(end_s - start_s);
    return QString("%1:%2:%3")
        .arg(s / 3600,        2, 10, QChar('0'))
        .arg((s % 3600) / 60, 2, 10, QChar('0'))
        .arg(s % 60,          2, 10, QChar('0'));
}

static QString fmtUtc(double unix_s)
{
    if (unix_s <= 0.0) return {};
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(unix_s), Qt::UTC)
               .toString(QStringLiteral("dd MMM yyyy  HH:mm"));
}

static QString fmtEndpoint(const app::DataLayer* layer, bool last)
{
    const auto& entries = layer->artifact_index.entries;
    if (entries.empty()) return {};
    const auto& e = last ? entries.back() : entries.front();
    if (e.lat == 0.0 && e.lon == 0.0) return {};
    if (e.is_projected)
        return QString("N%1  E%2").arg(e.lat, 0, 'f', 1).arg(e.lon, 0, 'f', 1);
    const QChar lS = e.lat >= 0.0 ? 'N' : 'S';
    const QChar oS = e.lon >= 0.0 ? 'E' : 'W';
    return QString("%1%5%2  %3%5%4")
        .arg(std::abs(e.lat), 0, 'f', 5).arg(lS)
        .arg(std::abs(e.lon), 0, 'f', 5).arg(oS)
        .arg(QString(QChar(0x00b0)));
}

static QString fmtLayerState(app::LayerState s)
{
    switch (s) {
        case app::LayerState::Placeholder: return QObject::tr("Pending");
        case app::LayerState::Indexing:    return QObject::tr("Indexing") + QChar(0x2026);
        case app::LayerState::Ready:       return QObject::tr("Ready");
        case app::LayerState::Failed:      return QObject::tr("Failed");
    }
    return {};
}

static QBrush stateColor(app::LayerState s)
{
    switch (s) {
        case app::LayerState::Ready:       return QBrush(QColor(kClrReady));
        case app::LayerState::Failed:      return QBrush(QColor(kClrFailed));
        case app::LayerState::Indexing:
        case app::LayerState::Placeholder: return QBrush(QColor(kClrPending));
    }
    return {};
}

static QString fmtBottomTrack(app::BottomTrackKind k)
{
    switch (k) {
        case app::BottomTrackKind::Unknown: return {};
        case app::BottomTrackKind::None:    return QObject::tr("None");
        case app::BottomTrackKind::Auto:    return QObject::tr("Auto");
        case app::BottomTrackKind::Manual:  return QObject::tr("Manual");
        case app::BottomTrackKind::Mixed:   return QObject::tr("Auto + Manual");
    }
    return {};
}

static QString fmtConfidence(core::Confidence c)
{
    switch (c) {
        case core::Confidence::Possible: return QObject::tr("Possible");
        case core::Confidence::Probable: return QObject::tr("Probable");
        case core::Confidence::Certain:  return QObject::tr("Certain");
    }
    return {};
}

// -- Filter application --------------------------------------------------------

void DataLibraryWindow::applyFilters()
{
    const bool match_any    = m_match_any   && m_match_any->isChecked();
    const bool text_enabled = m_text_chk    && m_text_chk->isChecked();
    const QString needle    = (text_enabled && m_filter_text)
                                  ? m_filter_text->text().trimmed().toLower()
                                  : QString();
    const bool by_name = !m_search_in_name || m_search_in_name->isChecked();
    const bool by_src  = !m_search_in_src  || m_search_in_src->isChecked();

    if (m_current_page == PageLayers && m_layers_table) {
        for (int row = 0; row < m_layers_table->rowCount(); ++row) {
            auto* name_item = m_layers_table->item(row, 0);
            if (!name_item) { m_layers_table->setRowHidden(row, false); continue; }

            // Text pass
            bool text_pass = !text_enabled || needle.isEmpty();
            if (!text_pass) {
                if (by_name) {
                    if (name_item->text().toLower().contains(needle)) text_pass = true;
                }
                if (!text_pass && by_src) {
                    auto* it = m_layers_table->item(row, 3);
                    if (it && it->text().toLower().contains(needle)) text_pass = true;
                }
            }

            // Status pass (state stored in UserRole+1 on the name item)
            bool status_pass = true;
            const auto ls = static_cast<app::LayerState>(
                name_item->data(Qt::UserRole + 1).toInt());
            switch (ls) {
                case app::LayerState::Ready:
                    if (m_state_ready  && !m_state_ready->isChecked())  status_pass = false;
                    break;
                case app::LayerState::Failed:
                    if (m_state_failed && !m_state_failed->isChecked()) status_pass = false;
                    break;
                case app::LayerState::Indexing:
                case app::LayerState::Placeholder:
                    if (m_state_pending && !m_state_pending->isChecked()) status_pass = false;
                    break;
            }

            const bool show = match_any
                ? (text_pass || status_pass)
                : (text_pass && status_pass);
            m_layers_table->setRowHidden(row, !show);
        }
    } else {
        QTableWidget* tbl = (m_current_page == PageContacts)
            ? m_contacts_table : m_issues_table;
        if (!tbl) return;
        for (int row = 0; row < tbl->rowCount(); ++row) {
            bool match = needle.isEmpty();
            for (int col = 0; col < tbl->columnCount() && !match; ++col) {
                auto* it = tbl->item(row, col);
                if (it && it->text().toLower().contains(needle)) match = true;
            }
            tbl->setRowHidden(row, !match);
        }
    }

    updateStatusBar();
    updateTabCounts();
}

// -- Refresh -------------------------------------------------------------------

void DataLibraryWindow::updateWindowTitle()
{
    const QString name = (m_project && !m_project->name().empty())
        ? QString::fromStdString(m_project->name()) : QString();
    setWindowTitle(name.isEmpty() ? tr("Data Library")
                                  : tr("%1 — Data Library").arg(name));
}

void DataLibraryWindow::refreshAll()
{
    refreshLayers();
    refreshContacts();
    refreshIssues();
    updateTabCounts();
    updateStatusBar();
}

void DataLibraryWindow::updateTabCounts()
{
    if (!m_layers_table || !m_tab_layers) return;

    int visible = 0;
    for (int r = 0; r < m_layers_table->rowCount(); ++r)
        if (!m_layers_table->isRowHidden(r)) ++visible;
    const int total = m_layers_table->rowCount();

    m_tab_layers->setText(visible == total
        ? tr("Layers (%1)").arg(total)
        : tr("Layers (%1/%2)").arg(visible).arg(total));

    const int n_con = m_project ? static_cast<int>(m_project->contacts().size()) : 0;
    const int n_iss = m_issues_table ? m_issues_table->rowCount() : 0;
    m_tab_contacts->setText(tr("Contacts (%1)").arg(n_con));
    m_tab_issues->setText(tr("Issues (%1)").arg(n_iss));
}

void DataLibraryWindow::updateStatusBar()
{
    if (!m_project) { statusBar()->showMessage(tr("No project open")); return; }

    int visible = 0, total = m_layers_table ? m_layers_table->rowCount() : 0;
    if (m_layers_table)
        for (int r = 0; r < total; ++r)
            if (!m_layers_table->isRowHidden(r)) ++visible;

    const int n_con = static_cast<int>(m_project->contacts().size());
    const int n_iss = m_issues_table ? m_issues_table->rowCount() : 0;

    const QString layers_str = (visible == total)
        ? tr("%1 layers").arg(total)
        : tr("%1 of %2 layers").arg(visible).arg(total);
    statusBar()->showMessage(
        tr("%1").arg(layers_str)
        + QString("  ") + QChar(0x00b7) + QString("  ")
        + tr("%1 contacts").arg(n_con)
        + QString("  ") + QChar(0x00b7) + QString("  ")
        + tr("%1 issues").arg(n_iss));
}

void DataLibraryWindow::refreshLayers()
{
    m_layers_table->setSortingEnabled(false);
    m_layers_table->setRowCount(0);
    m_layers_table->verticalHeader()->setDefaultSectionSize(m_compact_rows ? 22 : 28);

    if (!m_project) return;

    for (const auto& layer : m_project->layers()) {
        if (m_modality_filter >= 0 &&
            static_cast<int>(layer->modality) != m_modality_filter)
            continue;

        const int row = m_layers_table->rowCount();
        m_layers_table->insertRow(row);

        // Col 0: Name — UserRole = layer id, UserRole+1 = state (for filter)
        auto* name_item = new QTableWidgetItem(QString::fromStdString(layer->label));
        name_item->setData(Qt::UserRole,     QString::fromStdString(layer->id));
        name_item->setData(Qt::UserRole + 1, static_cast<int>(layer->state));
        m_layers_table->setItem(row, 0, name_item);

        // Col 1: Format
        const auto* src = m_project->findSource(layer->source_id);
        m_layers_table->setItem(row, 1, new QTableWidgetItem(
            src ? QString::fromStdString(src->format).toUpper() : QString()));

        // Col 2: State (color-coded)
        auto* state_item = new QTableWidgetItem(fmtLayerState(layer->state));
        state_item->setForeground(stateColor(layer->state));
        m_layers_table->setItem(row, 2, state_item);

        // Col 3: Source file name
        m_layers_table->setItem(row, 3, new QTableWidgetItem(
            src ? QFileInfo(QString::fromStdString(src->path)).fileName() : QString()));

        // Col 4: Pings
        auto* ping_item = new QTableWidgetItem(QString::number(layer->artifactCount()));
        ping_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_layers_table->setItem(row, 4, ping_item);

        // Col 5: Duration
        m_layers_table->setItem(row, 5, new QTableWidgetItem(
            fmtDuration(layer->start_time_utc, layer->end_time_utc)));

        // Col 6: Start UTC
        m_layers_table->setItem(row, 6, new QTableWidgetItem(
            fmtUtc(layer->start_time_utc)));

        // Col 7: SOL
        m_layers_table->setItem(row, 7, new QTableWidgetItem(
            fmtEndpoint(layer.get(), false)));

        // Col 8: EOL
        m_layers_table->setItem(row, 8, new QTableWidgetItem(
            fmtEndpoint(layer.get(), true)));

        // Col 9: Contacts on this layer
        int n_con = 0;
        for (const auto& c : m_project->contacts())
            if (c.line_id == layer->id) ++n_con;
        auto* con_item = new QTableWidgetItem(n_con > 0 ? QString::number(n_con) : QString());
        con_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_layers_table->setItem(row, 9, con_item);

        // Col 10: Bottom Track
        const auto caps = core::capabilitiesFor(
            app::artifactTypeForModality(layer->modality));
        m_layers_table->setItem(row, 10, new QTableWidgetItem(
            caps.hasBottomTrack ? fmtBottomTrack(layer->bottom_track_kind) : QString()));

        // Col 11: File size
        m_layers_table->setItem(row, 11, new QTableWidgetItem(
            src ? fmtBytes(src->size_bytes) : QString()));
    }

    m_layers_table->setSortingEnabled(true);

    for (int c = 0; c < m_layers_table->columnCount(); ++c)
        if (m_layers_table->horizontalHeader()->sectionResizeMode(c) != QHeaderView::Stretch)
            m_layers_table->resizeColumnToContents(c);

    applyFilters();
}

void DataLibraryWindow::refreshContacts()
{
    m_contacts_table->setRowCount(0);
    m_contacts_table->verticalHeader()->setDefaultSectionSize(m_compact_rows ? 22 : 28);
    if (!m_project) return;

    const auto& contacts = m_project->contacts();
    bool any_proj = false;
    for (const auto& c : contacts)
        if (core::spatialRefIsProjected(c.spatial_ref)) { any_proj = true; break; }

    m_contacts_table->setHorizontalHeaderLabels({
        tr("#"), tr("Label"),
        any_proj ? tr("Northing") : tr("Lat"),
        any_proj ? tr("Easting")  : tr("Lon"),
        tr("Class"), tr("Confidence"), tr("Range (m)"), tr("Depth (m)"), tr("Line")
    });

    for (const auto& c : contacts) {
        const int row = m_contacts_table->rowCount();
        m_contacts_table->insertRow(row);

        auto* id_item = new QTableWidgetItem(QString::number(c.id));
        id_item->setData(Qt::UserRole, static_cast<qulonglong>(c.id));
        m_contacts_table->setItem(row, 0, id_item);
        m_contacts_table->setItem(row, 1,
            new QTableWidgetItem(QString::fromStdString(c.label)));
        const bool proj = core::spatialRefIsProjected(c.spatial_ref);
        m_contacts_table->setItem(row, 2,
            new QTableWidgetItem(formatCoord(c.lat, proj, 'N', 'S')));
        m_contacts_table->setItem(row, 3,
            new QTableWidgetItem(formatCoord(c.lon, proj, 'E', 'W')));
        m_contacts_table->setItem(row, 4,
            new QTableWidgetItem(QString::fromStdString(c.classification)));
        m_contacts_table->setItem(row, 5,
            new QTableWidgetItem(fmtConfidence(c.confidence)));
        m_contacts_table->setItem(row, 6, new QTableWidgetItem(
            c.range_m > 0.f ? QString::number(c.range_m, 'f', 1) : QString()));
        m_contacts_table->setItem(row, 7, new QTableWidgetItem(
            c.depth_m > 0.f ? QString::number(c.depth_m, 'f', 1) : QString()));

        QString line_label;
        if (!c.line_id.empty()) {
            if (auto* lyr = m_project->findLayer(c.line_id))
                line_label = QString::fromStdString(lyr->label);
            else
                line_label = QString::fromStdString(c.line_id);
        }
        m_contacts_table->setItem(row, 8, new QTableWidgetItem(line_label));
    }

    for (int c = 0; c < m_contacts_table->columnCount(); ++c)
        if (m_contacts_table->horizontalHeader()->sectionResizeMode(c) != QHeaderView::Stretch)
            m_contacts_table->resizeColumnToContents(c);
}

void DataLibraryWindow::refreshIssues()
{
    m_issues_table->setRowCount(0);
    m_issues_table->verticalHeader()->setDefaultSectionSize(m_compact_rows ? 22 : 28);
    if (!m_project) return;

    struct Issue { int sev; QString cat, item, lid, prob, fix; };
    std::vector<Issue> issues;

    for (const auto& layer : m_project->layers()) {
        const QString name = QString::fromStdString(layer->label);
        const QString id   = QString::fromStdString(layer->id);

        if (layer->state == app::LayerState::Failed)
            issues.push_back({ 0, tr("Layer"), name, id,
                tr("Import failed — no data was indexed."),
                tr("Re-import the source file.") });

        if (layer->state == app::LayerState::Ready
                && layer->source_spatial_ref.empty())
            issues.push_back({ 1, tr("Layer"), name, id,
                tr("No CRS assigned."),
                tr("Open Geodesy settings and assign the source CRS.") });

        if (layer->state == app::LayerState::Ready
                && layer->artifact_index.empty())
            issues.push_back({ 0, tr("Layer"), name, id,
                tr("Index is empty despite a successful import."),
                tr("Re-import the source file.") });
    }

    const auto qc_envs = m_project->contractStore()
                             .latestByType(app::contracts::ContractType::QCFlags);
    for (const auto& env : qc_envs) {
        const auto* flags = env.payloadAs<app::contracts::QCFlags>();
        if (!flags || flags->pass) continue;
        QString item_name = QString::fromStdString(flags->layer_id);
        const QString lid = item_name;
        if (auto* lyr = m_project->findLayer(flags->layer_id))
            item_name = QString::fromStdString(lyr->label);
        for (const auto& f : flags->failures)
            issues.push_back({ 0, tr("Processing"), item_name, lid,
                QString::fromStdString(f), tr("Review processing node settings.") });
        for (const auto& w : flags->warnings)
            issues.push_back({ 1, tr("Processing"), item_name, lid,
                QString::fromStdString(w), QString() });
    }

    std::stable_sort(issues.begin(), issues.end(),
        [](const Issue& a, const Issue& b) { return a.sev < b.sev; });

    for (const auto& iss : issues) {
        const int row = m_issues_table->rowCount();
        m_issues_table->insertRow(row);

        auto* sev = new QTableWidgetItem(
            iss.sev == 0 ? tr("Error") : iss.sev == 1 ? tr("Warning") : tr("Info"));
        if (iss.sev == 0)      sev->setForeground(QBrush(QColor(kClrFailed)));
        else if (iss.sev == 1) sev->setForeground(QBrush(QColor(kClrPending)));
        m_issues_table->setItem(row, 0, sev);
        m_issues_table->setItem(row, 1, new QTableWidgetItem(iss.cat));

        auto* item_cell = new QTableWidgetItem(iss.item);
        item_cell->setData(Qt::UserRole, iss.lid);
        m_issues_table->setItem(row, 2, item_cell);
        m_issues_table->setItem(row, 3, new QTableWidgetItem(iss.prob));
        m_issues_table->setItem(row, 4, new QTableWidgetItem(iss.fix));
    }

    for (int c = 0; c < m_issues_table->columnCount(); ++c)
        if (m_issues_table->horizontalHeader()->sectionResizeMode(c) != QHeaderView::Stretch)
            m_issues_table->resizeColumnToContents(c);
}

} // namespace dolphin::ui
