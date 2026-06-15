// TaskProgressController.cpp — app-wide ProcessingDialog lifecycle controller
#include "ui/mainwindow/TaskProgressController.h"
#include "ui/mainwindow/ProcessingDialog.h"
#include "app/tasks/OperationManager.h"

#include <Qt>

namespace dolphin::ui {

TaskProgressController::TaskProgressController(QWidget*               dialog_parent,
                                               app::OperationManager* op_mgr,
                                               QObject*               parent)
    : QObject(parent)
    , m_parent(dialog_parent)
    , m_op_mgr(op_mgr)
{}

void TaskProgressController::taskBegin(const QString& id, const QString& label)
{
    if (!m_dlg) {
        m_dlg = new ProcessingDialog(m_parent);
        m_dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_dlg, &ProcessingDialog::cancelRequested,
                this, &TaskProgressController::onCancelRequested);
        connect(m_dlg, &QObject::destroyed,
                this, &TaskProgressController::onDialogDestroyed);
    }
    m_dlg->taskBegin(id, label);
    if (!m_dlg->isVisible())
        m_dlg->show();
}

void TaskProgressController::taskDone(const QString& id)
{
    if (m_dlg) m_dlg->taskDone(id);
}

void TaskProgressController::taskFail(const QString& id, const QString& error)
{
    if (m_dlg) m_dlg->taskFail(id, error);
}

void TaskProgressController::onCancelRequested()
{
    m_op_mgr->cancelAll();
}

void TaskProgressController::onDialogDestroyed()
{
    m_dlg = nullptr;
}

} // namespace dolphin::ui
