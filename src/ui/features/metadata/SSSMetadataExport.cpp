// SSSMetadataExport.cpp — clipboard copy, CSV export, keyboard shortcuts, context menu.
#include "ui/features/metadata/SSSMetadataWindow.h"
#include "ui/features/metadata/MetadataExportUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QTableView>

namespace dolphin::ui {

void SSSMetadataWindow::keyPressEvent(QKeyEvent* ev)
{
    if (ev->matches(QKeySequence::SelectAll)) {
        m_table->selectAll();
        ev->accept();
        return;
    }
    if (ev->matches(QKeySequence::Copy)) {
        const bool has_sel = !m_table->selectionModel()->selectedIndexes().isEmpty();
        QApplication::clipboard()->setText(buildTabText(has_sel, true));
        ev->accept();
        return;
    }
    QWidget::keyPressEvent(ev);
}

void SSSMetadataWindow::showTableContextMenu(const QPoint& pos)
{
    const bool has_sel = !m_table->selectionModel()->selectedIndexes().isEmpty();
    QMenu menu(this);
    menu.addAction("Select All\tCtrl+A",     this, [this]{ m_table->selectAll(); });
    menu.addSeparator();
    menu.addAction("Copy Selection\tCtrl+C", this, &SSSMetadataWindow::onCopySelection)
        ->setEnabled(has_sel);
    menu.addAction("Copy All",               this, &SSSMetadataWindow::onCopyAll);
    menu.addSeparator();
    menu.addAction("Export Selection to CSV…", this, &SSSMetadataWindow::onExportSelection)
        ->setEnabled(has_sel);
    menu.addAction("Export All to CSV…",       this, &SSSMetadataWindow::onExportAll);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

QString SSSMetadataWindow::buildTabText(bool selection_only, bool with_headers) const
{
    return MetadataExport::buildTabText(m_proxy, m_table, selection_only, with_headers);
}

void SSSMetadataWindow::exportToCsv(bool selection_only)
{
    MetadataExport::exportToCsv(this, m_proxy, m_table, selection_only);
}

void SSSMetadataWindow::onExportAll()       { exportToCsv(false); }
void SSSMetadataWindow::onExportSelection() { exportToCsv(true);  }

void SSSMetadataWindow::onCopyAll()
{
    QApplication::clipboard()->setText(buildTabText(false, true));
    m_load_status->setText("Copied to clipboard");
}

void SSSMetadataWindow::onCopySelection()
{
    const bool has_sel = !m_table->selectionModel()->selectedIndexes().isEmpty();
    QApplication::clipboard()->setText(buildTabText(has_sel, true));
    m_load_status->setText("Copied to clipboard");
}

} // namespace dolphin::ui
