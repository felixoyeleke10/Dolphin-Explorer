#pragma once
#include "core/SpatialRef.h"
#include <QDialog>

class QLabel;
class QLineEdit;
class QTableWidget;
class QTableWidgetItem;

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  CrsPickerDialog — searchable coordinate reference system picker.
//
//  Presents a curated list of survey-relevant EPSG CRS (from EpsgDatabase).
//  Common entries are pinned at the top; the search box filters both name and
//  EPSG code.  On accept, selectedRef() returns a fully-populated SpatialRef.
// ─────────────────────────────────────────────────────────────────────────────

class CrsPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit CrsPickerDialog(const core::SpatialRef& current,
                             QWidget* parent = nullptr);

    // The SpatialRef the user confirmed. Only valid after Accepted.
    core::SpatialRef selectedRef() const { return m_selected; }

private slots:
    void applyFilter();         // called by debounce timer
    void onSelectionChanged();
    void onItemDoubleClicked(int row, int col);

private:
    void buildTable();          // called once in constructor
    void applyFilter(const QString& text);

    QLineEdit*    m_search       = nullptr;
    QTableWidget* m_table        = nullptr;
    QLabel*       m_preview      = nullptr;
    QTimer*       m_debounce     = nullptr;
    QString       m_pending_filter;

    core::SpatialRef m_selected;
};

} // namespace dolphin::ui
