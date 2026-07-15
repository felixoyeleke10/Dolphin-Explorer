#include "ui/mainwindow/coordinators/ExportController.h"

#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/features/export/ExportManagerWindow.h"
#include "ui/features/contacts/ContactReport.h"
#include "ui/features/map/MapViewportHost.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "io/raster/RasterReader.h"
#include "io/raster/RasterWriter.h"
#include "core/Contact.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QPainter>
#include <QWidget>

#include <utility>

namespace dolphin::ui {

namespace {

QString exportStartDir()
{
    const QString dir = AppSettingsDialog::loadDefaults().export_dir;
    return dir.isEmpty() ? QDir::homePath() : dir;
}

} // namespace

ExportController::ExportController(ProjectProvider project_provider,
                                   QWidget* capture_widget,
                                   MapViewportHost* viewport_host,
                                   QWidget* dialog_parent,
                                   QObject* parent)
    : QObject(parent)
    , m_project_provider(std::move(project_provider))
    , m_capture_widget(capture_widget)
    , m_viewport_host(viewport_host)
    , m_dialog_parent(dialog_parent)
{}

app::Project* ExportController::project() const
{
    return m_project_provider ? m_project_provider() : nullptr;
}

void ExportController::exportContactsCsv()
{
    auto* proj = project();
    if (!proj) { emit statusMessage(tr("Open a project before exporting.")); return; }

    const auto& contacts = proj->contacts();
    if (contacts.empty()) { emit statusMessage(tr("No contacts to export.")); return; }

    QString path = QFileDialog::getSaveFileName(
        m_dialog_parent, tr("Export Contacts as CSV"), exportStartDir(),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv");

    const QString title = tr("Contacts - %1")
        .arg(QString::fromStdString(proj->name()));
    if (!ContactReport::writeCsv(path, title, contacts, proj)) {
        QMessageBox::warning(m_dialog_parent, tr("Export Failed"),
            tr("Could not write to:\n%1").arg(path));
        return;
    }

    emit statusMessage(tr("Exported %1 contact(s) to %2")
        .arg(static_cast<int>(contacts.size()))
        .arg(QFileInfo(path).fileName()));
    emit activityRecorded(tr("Exported %1 contact(s) -> %2")
        .arg(static_cast<int>(contacts.size()))
        .arg(QFileInfo(path).fileName()));
}

void ExportController::exportGeoTiff()
{
    auto* proj = project();
    if (!proj) { emit statusMessage(tr("Open a project before exporting.")); return; }
    std::vector<std::string> ids;
    for (const auto& layer : proj->layers())
        if (layer && layer->modality == app::Modality::Raster && layer->raster.valid)
            ids.push_back(layer->id);
    if (ids.empty()) {
        emit statusMessage(
            tr("No raster layers to export. Import a raster (GeoTIFF / image) first."));
        return;
    }
    exportLayers(ids, QStringLiteral("geotiff"));
}

void ExportController::openManager()
{
    if (!m_window) {
        auto* win = new ExportManagerWindow(nullptr);
        win->setAttribute(Qt::WA_DeleteOnClose);
        m_window = win;
        connect(win, &ExportManagerWindow::exportContactsCsvRequested,
                this, &ExportController::exportContactsCsv);
        connect(win, &ExportManagerWindow::exportContactsPdfRequested,
                this, [this] { exportContactsReport(false); });
        connect(win, &ExportManagerWindow::exportContactsWordRequested,
                this, [this] { exportContactsReport(true); });
        connect(win, &ExportManagerWindow::exportScreenshotRequested,
                this, &ExportController::exportScreenshot);
        connect(win, &ExportManagerWindow::exportRastersRequested, this, [this] {
            auto* proj = project();
            if (!proj) return;
            std::vector<std::string> ids;
            for (const auto& layer : proj->layers())
                if (layer && layer->modality == app::Modality::Raster
                          && layer->raster.valid)
                    ids.push_back(layer->id);
            if (ids.empty()) {
                emit statusMessage(tr("No raster layers to export."));
                return;
            }
            exportLayers(ids, QStringLiteral("geotiff"));
        });
    }
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void ExportController::exportContactsReport(bool docx)
{
    auto* proj = project();
    if (!proj) { emit statusMessage(tr("Open a project before exporting.")); return; }
    const auto& contacts = proj->contacts();
    if (contacts.empty()) { emit statusMessage(tr("No contacts to export.")); return; }

    const QString filter = docx ? tr("Word Document (*.docx)")
                                : tr("PDF Document (*.pdf)");
    const QString ext = docx ? QStringLiteral(".docx") : QStringLiteral(".pdf");
    QString path = QFileDialog::getSaveFileName(
        m_dialog_parent, tr("Export Contact Report"), exportStartDir(), filter);
    if (path.isEmpty()) return;
    if (!path.endsWith(ext, Qt::CaseInsensitive)) path += ext;

    const QString title = tr("Contact Report - %1")
        .arg(QString::fromStdString(proj->name()));
    const std::vector<core::Contact> rows(contacts.begin(), contacts.end());
    const bool ok = docx ? ContactReport::writeDocx(path, title, rows, proj)
                         : ContactReport::writePdf(path, title, rows, proj);
    if (!ok) {
        QMessageBox::warning(m_dialog_parent, tr("Export Failed"),
                             tr("Could not write the report file."));
        return;
    }
    emit statusMessage(tr("Exported %1 contact(s) -> %2")
        .arg(static_cast<int>(rows.size())).arg(QFileInfo(path).fileName()));
    emit activityRecorded(
        tr("Exported contact report -> %1").arg(QFileInfo(path).fileName()));
}

void ExportController::exportScreenshot()
{
    if (!m_capture_widget) return;
    const auto* proj = project();
    const QString base = proj ? QString::fromStdString(proj->name())
                              : QStringLiteral("DolphinExplorer");
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString suggested = QDir(exportStartDir())
        .filePath(QString("%1_screenshot_%2.png").arg(base, stamp));

    QString path = QFileDialog::getSaveFileName(
        m_dialog_parent, tr("Export Screenshot"), suggested,
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg);;All files (*)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".png");

    const qreal dpr = m_capture_widget->devicePixelRatio();
    QImage image(QSize(qRound(m_capture_widget->width() * dpr),
                       qRound(m_capture_widget->height() * dpr)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::black);
    m_capture_widget->render(&image);

    if (m_viewport_host) {
        const QImage viewport = m_viewport_host->grabViewportImage();
        if (!viewport.isNull()) {
            const QPoint top_left = m_viewport_host->mapTo(m_capture_widget, QPoint(0, 0));
            const QRect target(
                QPoint(qRound(top_left.x() * dpr), qRound(top_left.y() * dpr)),
                QSize(qRound(m_viewport_host->width() * dpr),
                      qRound(m_viewport_host->height() * dpr)));
            QPainter painter(&image);
            painter.drawImage(target, viewport);
        }
    }

    const QString suffix = QFileInfo(path).suffix();
    const QByteArray format = suffix.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0
                           || suffix.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0
        ? QByteArrayLiteral("JPG") : QByteArrayLiteral("PNG");
    if (!image.save(path, format.constData())) {
        QMessageBox::warning(m_dialog_parent, tr("Export Failed"),
            tr("Could not write screenshot to:\n%1").arg(path));
        return;
    }

    emit statusMessage(
        tr("Screenshot exported to %1").arg(QFileInfo(path).fileName()));
    emit activityRecorded(
        tr("Screenshot -> %1").arg(QFileInfo(path).fileName()));
}

bool ExportController::exportRasterLayer(app::DataLayer* layer, const QString& path)
{
    if (!layer || !layer->raster.valid) return false;
    std::string error;
    bool ok = false;
    if (layer->raster.is_depth) {
        core::RasterGrid grid;
        if (io::readElevationRaster(layer->artifact_store_path, grid, &error))
            ok = io::writeElevationGeoTiff(path.toStdString(), grid, &error);
    } else {
        io::RasterImage image;
        if (io::readImageRaster(layer->artifact_store_path, image, &error))
            ok = io::writeImageGeoTiff(path.toStdString(), image.width, image.height,
                image.rgba, image.geo_transform, image.crs_wkt, true, &error);
    }
    if (ok) {
        emit statusMessage(tr("Exported GeoTIFF: %1").arg(path));
        emit activityRecorded(
            tr("Exported raster %1").arg(QFileInfo(path).fileName()));
    } else {
        emit statusMessage(
            tr("Raster export failed - %1").arg(QString::fromStdString(error)));
    }
    return ok;
}

void ExportController::exportLayers(const std::vector<std::string>& layer_ids,
                                    const QString& format)
{
    auto* proj = project();
    if (!proj || layer_ids.empty()) return;

    std::vector<app::DataLayer*> rasters;
    for (const auto& id : layer_ids)
        if (auto* layer = proj->findLayer(id);
            layer && layer->modality == app::Modality::Raster && layer->raster.valid)
            rasters.push_back(layer);

    if (rasters.empty()) {
        emit statusMessage(tr("Export as %1 is not available for these layers.")
            .arg(format.toUpper()));
        return;
    }
    if (rasters.size() == 1) {
        const QString suggested = QString::fromStdString(rasters.front()->label) + ".tif";
        const QString path = QFileDialog::getSaveFileName(
            m_dialog_parent, tr("Export GeoTIFF"), suggested,
            tr("GeoTIFF (*.tif *.tiff)"));
        if (!path.isEmpty()) exportRasterLayer(rasters.front(), path);
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(
        m_dialog_parent, tr("Export GeoTIFFs to Folder"));
    if (dir.isEmpty()) return;
    int exported = 0;
    for (auto* layer : rasters) {
        const QString path = QDir(dir).filePath(
            QString::fromStdString(layer->label) + ".tif");
        if (exportRasterLayer(layer, path)) ++exported;
    }
    emit statusMessage(tr("Exported %n GeoTIFF(s)", nullptr, exported));
}

} // namespace dolphin::ui
