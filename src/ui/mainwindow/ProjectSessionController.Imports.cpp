// ProjectSessionController.Imports.cpp — safe project transitions while imports run.
#include "ui/mainwindow/ProjectSessionController.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>

namespace dolphin::ui {

bool ProjectSessionController::ensureImportsIdle(const QString& dialog_title)
{
    if (!m_imports_busy_check || !m_imports_busy_check()) return true;

    QMessageBox box(m_dialog_parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(dialog_title);
    box.setText(tr("File imports are still running."));
    box.setInformativeText(
        tr("Cancel Imports drops the queued files; the file currently "
           "importing finishes first. Or wait for all imports to complete."));
    auto* cancel_btn = box.addButton(tr("Cancel Imports"), QMessageBox::AcceptRole);
    auto* wait_btn = box.addButton(tr("Wait"), QMessageBox::RejectRole);
    box.setDefaultButton(wait_btn);
    box.exec();
    if (box.clickedButton() != cancel_btn) return false;

    if (m_imports_cancel_request) m_imports_cancel_request();
    if (waitForImportsToSettle(20000)) return true;

    QMessageBox::information(
        m_dialog_parent, dialog_title,
        tr("The current file is still finishing its import. "
           "Try again in a moment."));
    return false;
}

bool ProjectSessionController::waitForImportsToSettle(int timeout_ms)
{
    if (!m_imports_busy_check) return true;

    QProgressDialog progress(tr("Stopping imports…"), QString(), 0, 0,
                             m_dialog_parent);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(300);

    QElapsedTimer elapsed;
    elapsed.start();
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (!m_imports_busy_check() || elapsed.elapsed() >= timeout_ms)
            loop.quit();
    });
    poll.start();
    loop.exec();
    return !m_imports_busy_check();
}

} // namespace dolphin::ui
