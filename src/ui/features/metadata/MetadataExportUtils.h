#pragma once
#include <QString>

class QSortFilterProxyModel;
class QTableView;
class QWidget;

namespace dolphin::ui::MetadataExport {

// Tab-separated text for clipboard (selection-aware, optional header row).
QString buildTabText(QSortFilterProxyModel* proxy, QTableView* table,
                     bool selection_only, bool with_headers);

// RFC 4180 CSV written to a user-chosen file (quoted cells).
void exportToCsv(QWidget* parent, QSortFilterProxyModel* proxy, QTableView* table,
                 bool selection_only);

} // namespace dolphin::ui::MetadataExport
