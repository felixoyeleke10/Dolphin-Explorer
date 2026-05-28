// MainWindow.ProcessingCoordinator.cpp — ProcessingWindow lifetime and wiring.
#include "ui/mainwindow/MainWindow.h"
#include "ui/features/processing/ProcessingWindow.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "app/services/ProcessingService.h"

namespace dolphin::ui {

void MainWindow::onOpenProcessingWindow()
{
    if (!m_processing_win) {
        m_processing_win = new ProcessingWindow(this);
        m_processing_win->setProject(m_project, m_processing_service, m_sss_ctrl);
    }

    m_processing_win->show();
    m_processing_win->raise();
    m_processing_win->activateWindow();
}

} // namespace dolphin::ui
