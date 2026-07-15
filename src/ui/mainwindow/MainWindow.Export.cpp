// MainWindow.Export.cpp - composition-level delegates to ExportController.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/coordinators/ExportController.h"

namespace dolphin::ui {

void MainWindow::onExportCsv()
{
    if (m_export_ctrl) m_export_ctrl->exportContactsCsv();
}

void MainWindow::onExportGeotiff()
{
    if (m_export_ctrl) m_export_ctrl->exportGeoTiff();
}

void MainWindow::onExportScreenshot()
{
    if (m_export_ctrl) m_export_ctrl->exportScreenshot();
}

void MainWindow::onExportManagerOpen()
{
    if (m_export_ctrl) m_export_ctrl->openManager();
}

void MainWindow::onExportLayers(const std::vector<std::string>& layer_ids,
                                const QString& format)
{
    if (m_export_ctrl) m_export_ctrl->exportLayers(layer_ids, format);
}

} // namespace dolphin::ui
