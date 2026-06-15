// MainWindow.Processing.cpp — thin delegates to TaskProgressController
#include "ui/mainwindow/MainWindow.h"

namespace dolphin::ui {

void MainWindow::taskBegin(const QString& id, const QString& label)
{
    if (m_task_ctrl) m_task_ctrl->taskBegin(id, label);
}

void MainWindow::taskDone(const QString& id)
{
    if (m_task_ctrl) m_task_ctrl->taskDone(id);
}

void MainWindow::taskFail(const QString& id, const QString& error)
{
    if (m_task_ctrl) m_task_ctrl->taskFail(id, error);
}

} // namespace dolphin::ui
