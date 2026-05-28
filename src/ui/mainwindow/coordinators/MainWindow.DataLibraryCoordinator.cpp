// MainWindow.DataLibraryCoordinator.cpp — DataLibraryWindow lifecycle.
#include "ui/mainwindow/MainWindow.h"
#include "ui/features/datalibrary/DataLibraryWindow.h"
#include "app/project/Project.h"

namespace dolphin::ui {

void MainWindow::onDataLibraryOpen()
{
    if (!m_data_library_win) {
        m_data_library_win = new DataLibraryWindow(nullptr);
        m_data_library_win->setProject(m_project.get());

        connect(m_data_library_win, &DataLibraryWindow::layerActivated,
                this, &MainWindow::onLayerSelected);
        connect(m_data_library_win, &DataLibraryWindow::contactActivated,
                this, &MainWindow::onContactSelected);
    }

    m_data_library_win->show();
    m_data_library_win->raise();
    m_data_library_win->activateWindow();
}

} // namespace dolphin::ui
