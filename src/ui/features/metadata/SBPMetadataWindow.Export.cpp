// SBPMetadataWindow.Export.cpp — CSV export and clipboard copy operations.
#include "ui/features/metadata/SBPMetadataWindow.h"
#include "ui/features/metadata/MetadataExportUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QTableView>

namespace dolphin::ui {

void SBPMetadataWindow::keyPressEvent(QKeyEvent* ev)
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

void SBPMetadataWindow::showTableContextMenu(const QPoint& pos)
{
    const bool has_sel = !m_table->selectionModel()->selectedIndexes().isEmpty();
    QMenu menu(this);
    menu.addAction("Select All\tCtrl+A",     this, [this]{ m_table->selectAll(); });
    menu.addSeparator();
    menu.addAction("Copy Selection\tCtrl+C", this, &SBPMetadataWindow::onCopySelection)
        ->setEnabled(has_sel);
    menu.addAction("Copy All",               this, &SBPMetadataWindow::onCopyAll);
    menu.addSeparator();
    menu.addAction("Export Selection to CSV…", this, &SBPMetadataWindow::onExportSelection)
        ->setEnabled(has_sel);
    menu.addAction("Export All to CSV…",       this, &SBPMetadataWindow::onExportAll);
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

QString SBPMetadataWindow::buildTabText(bool selection_only, bool with_headers) const
{
    return MetadataExport::buildTabText(m_proxy, m_table, selection_only, with_headers);
}

void SBPMetadataWindow::exportToCsv(bool selection_only)
{
    MetadataExport::exportToCsv(this, m_proxy, m_table, selection_only);
}

void SBPMetadataWindow::onExportAll()       { exportToCsv(false); }
void SBPMetadataWindow::onExportSelection() { exportToCsv(true);  }

void SBPMetadataWindow::onCopyAll()
{
    QApplication::clipboard()->setText(buildTabText(false, true));
    m_load_status->setText("Copied to clipboard");
}

void SBPMetadataWindow::onCopySelection()
{
    const bool has_sel = !m_table->selectionModel()->selectedIndexes().isEmpty();
    QApplication::clipboard()->setText(buildTabText(has_sel, true));
    m_load_status->setText("Copied to clipboard");
}

} // namespace dolphin::ui
