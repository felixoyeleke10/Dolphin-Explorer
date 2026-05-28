// MetadataExportUtils.cpp — shared clipboard and CSV export for SSS and SBP metadata windows.
#include "ui/features/metadata/MetadataExportUtils.h"

#include <QAbstractItemModel>
#include <QFile>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTextStream>
#include <QVector>

#include <algorithm>

namespace dolphin::ui::MetadataExport {

QString buildTabText(QSortFilterProxyModel* proxy, QTableView* table,
                     bool selection_only, bool with_headers)
{
    QString out;
    QTextStream ts(&out);

    const int cols = proxy->columnCount();
    const int rows = proxy->rowCount();

    QSet<int> sel_rows;
    QVector<QPair<int,int>> sel_cells;
    if (selection_only) {
        for (const auto& idx : table->selectionModel()->selectedIndexes()) {
            sel_rows.insert(idx.row());
            sel_cells.append({idx.row(), idx.column()});
        }
    }

    if (with_headers) {
        for (int c = 0; c < cols; ++c) {
            if (c) ts << '\t';
            ts << proxy->headerData(c, Qt::Horizontal, Qt::DisplayRole)
                        .toString().replace('\n', ' ');
        }
        ts << '\n';
    }

    if (selection_only && !sel_cells.isEmpty()) {
        std::sort(sel_cells.begin(), sel_cells.end());
        int  last_row  = sel_cells.first().first;
        bool first_col = true;
        for (const auto& [r, c] : sel_cells) {
            if (r != last_row) { ts << '\n'; last_row = r; first_col = true; }
            if (!first_col) ts << '\t';
            ts << proxy->data(proxy->index(r, c), Qt::DisplayRole).toString();
            first_col = false;
        }
        ts << '\n';
    } else {
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (c) ts << '\t';
                ts << proxy->data(proxy->index(r, c), Qt::DisplayRole).toString();
            }
            ts << '\n';
        }
    }
    return out;
}

void exportToCsv(QWidget* parent, QSortFilterProxyModel* proxy, QTableView* table,
                 bool selection_only)
{
    const QString path = QFileDialog::getSaveFileName(
        parent,
        selection_only ? QObject::tr("Export Selection to CSV")
                       : QObject::tr("Export All to CSV"),
        QString(), QObject::tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(parent, QObject::tr("Export Failed"),
                             QObject::tr("Could not write to:\n%1").arg(path));
        return;
    }

    QTextStream ts(&f);
    const int cols = proxy->columnCount();
    const int rows = proxy->rowCount();

    for (int c = 0; c < cols; ++c) {
        if (c) ts << ',';
        QString h = proxy->headerData(c, Qt::Horizontal, Qt::DisplayRole)
                       .toString().replace('\n', ' ');
        ts << '"' << h.replace('"', "\"\"") << '"';
    }
    ts << '\n';

    auto writeRows = [&](auto rowFilter) {
        for (int r = 0; r < rows; ++r) {
            if (!rowFilter(r)) continue;
            for (int c = 0; c < cols; ++c) {
                if (c) ts << ',';
                QString v = proxy->data(proxy->index(r, c), Qt::DisplayRole).toString();
                ts << '"' << v.replace('"', "\"\"") << '"';
            }
            ts << '\n';
        }
    };

    if (selection_only) {
        QSet<int> sel_rows;
        for (const auto& idx : table->selectionModel()->selectedIndexes())
            sel_rows.insert(idx.row());
        writeRows([&](int r){ return sel_rows.contains(r); });
    } else {
        writeRows([](int){ return true; });
    }
}

} // namespace dolphin::ui::MetadataExport
