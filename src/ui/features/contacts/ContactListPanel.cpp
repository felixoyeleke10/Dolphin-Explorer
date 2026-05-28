#include "ui/features/contacts/ContactListPanel.h"
#include "ui/shell/Theme.h"
#include "ui/shared/CoordFormat.h"
#include "core/SpatialRef.h"
#include <QMenu>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QLabel>

namespace dolphin::ui {

ContactListPanel::ContactListPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(Theme::kSpacing1, Theme::kSpacing1, Theme::kSpacing1, Theme::kSpacing1);
    layout->setSpacing(Theme::kSpacing1);

    auto* header = new QLabel(tr("Contacts"), this);
    header->setObjectName("sectionLabel");
    layout->addWidget(header);

    m_table = new QTableWidget(this);
    m_table->setObjectName("contactTable");
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({tr("Label"), tr("Lat"), tr("Lon"), tr("Class")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table);

    connect(m_table, &QTableWidget::cellClicked, this,
        [this](int row, int) {
            auto* item = m_table->item(row, 0);
            if (item)
                emit contactSelected(item->data(Qt::UserRole).toULongLong());
        });

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        auto* item = m_table->itemAt(pos);
        const uint64_t id = item
            ? m_table->item(item->row(), 0)->data(Qt::UserRole).toULongLong()
            : 0;

        QMenu menu(m_table);
        auto* act_remove = menu.addAction(tr("Remove Contact"));
        act_remove->setEnabled(item != nullptr);
        connect(act_remove, &QAction::triggered, this, [this, id]() {
            emit removeContactRequested(id);
        });
        menu.addSeparator();
        auto* act_export = menu.addAction(tr("Export…"));
        act_export->setEnabled(m_project && !m_project->contacts().empty());
        connect(act_export, &QAction::triggered, this, [this]() {
            emit exportContactsRequested();
        });
        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });
}

void ContactListPanel::setProject(app::Project* project)
{
    m_project = project;
    refresh();
}

void ContactListPanel::refresh()
{
    m_table->setRowCount(0);
    if (!m_project) return;

    const auto& contacts = m_project->contacts();

    // Determine coordinate system from first contact with a known spatial ref
    bool any_projected = false;
    for (const auto& c : contacts)
        if (core::spatialRefIsProjected(c.spatial_ref)) { any_projected = true; break; }

    m_table->setHorizontalHeaderLabels({
        tr("Label"),
        any_projected ? tr("Northing") : tr("Lat"),
        any_projected ? tr("Easting")  : tr("Lon"),
        tr("Class")
    });

    for (const auto& c : contacts) {
        int row = m_table->rowCount();
        m_table->insertRow(row);

        auto* label_item = new QTableWidgetItem(QString::fromStdString(c.label));
        label_item->setData(Qt::UserRole, static_cast<qulonglong>(c.id));
        m_table->setItem(row, 0, label_item);
        const bool proj = core::spatialRefIsProjected(c.spatial_ref);
        m_table->setItem(row, 1, new QTableWidgetItem(
            formatCoord(c.lat, proj, 'N', 'S')));
        m_table->setItem(row, 2, new QTableWidgetItem(
            formatCoord(c.lon, proj, 'E', 'W')));
        m_table->setItem(row, 3, new QTableWidgetItem(
            QString::fromStdString(c.classification)));
    }
}

} // namespace dolphin::ui
