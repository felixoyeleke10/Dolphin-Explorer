// MainWindow.Processing.cpp — app-wide processing dialog lifecycle

#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/ProcessingDialog.h"

namespace dolphin::ui {

void MainWindow::taskBegin(const QString& id, const QString& label)
{
    if (!m_processing_dlg) {
        m_processing_dlg = new ProcessingDialog(this);
        m_processing_dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_processing_dlg, &ProcessingDialog::cancelRequested,
                this, &MainWindow::onCancelProcessing);
        connect(m_processing_dlg, &QObject::destroyed,
                this, [this] { m_processing_dlg = nullptr; });
    }
    m_processing_dlg->taskBegin(id, label);
    if (!m_processing_dlg->isVisible())
        m_processing_dlg->show();
}

void MainWindow::taskDone(const QString& id)
{
    if (m_processing_dlg)
        m_processing_dlg->taskDone(id);
}

void MainWindow::taskFail(const QString& id, const QString& error)
{
    if (m_processing_dlg)
        m_processing_dlg->taskFail(id, error);
}

void MainWindow::onCancelProcessing()
{
    m_op_mgr->cancelAll();
}

} // namespace dolphin::ui
