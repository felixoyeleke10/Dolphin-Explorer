// MainWindow.Tools.cpp — export/tool stubs, contact actions, onAbout.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/features/export/ExportManagerWindow.h"
#include "ui/features/contacts/ContactReport.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/sidescan/SidescanCorrectionDialog.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "io/raster/RasterReader.h"
#include "io/raster/RasterWriter.h"
#include "core/Contact.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/widgets/LayerPickerWidget.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QTextStream>
#include <QToolButton>
#include <vector>

namespace dolphin::ui {

// -- Export helpers ------------------------------------------------------------

namespace {
// Returns the user's preferred export directory, falling back to home.
QString exportStartDir()
{
    const QString dir = AppSettingsDialog::loadDefaults().export_dir;
    return dir.isEmpty() ? QDir::homePath() : dir;
}
} // namespace

// -- Export stubs --------------------------------------------------------------

void MainWindow::onExportCsv()
{
    if (!currentProject()) { appendJobMessage(tr("Open a project before exporting.")); return; }

    const auto& contacts = currentProject()->contacts();
    if (contacts.empty()) {
        appendJobMessage(tr("No contacts to export."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Contacts as CSV"), exportStartDir(),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Failed"),
            tr("Could not write to:\n%1").arg(path));
        return;
    }

    auto quoted = [](const QString& s) -> QString {
        return '"' + QString(s).replace('"', "\"\"") + '"';
    };
    auto confidenceStr = [](core::Confidence c) -> QLatin1StringView {
        switch (c) {
        case core::Confidence::Possible: return QLatin1StringView("Possible");
        case core::Confidence::Probable: return QLatin1StringView("Probable");
        case core::Confidence::Certain:  return QLatin1StringView("Certain");
        }
        return QLatin1StringView("Possible");
    };

    QTextStream ts(&file);
    ts << "Label,Latitude,Longitude,Depth_m,Range_m,Width_m,Height_m,"
          "Classification,Confidence,Line,Notes\n";

    for (const auto& c : contacts) {
        ts << quoted(QString::fromStdString(c.label))          << ','
           << QString::number(c.lat,      'f', 8)              << ','
           << QString::number(c.lon,      'f', 8)              << ','
           << QString::number(c.depth_m,  'f', 2)              << ','
           << QString::number(c.range_m,  'f', 2)              << ','
           << QString::number(c.width_m,  'f', 2)              << ','
           << QString::number(c.height_m, 'f', 2)              << ','
           << quoted(QString::fromStdString(c.classification))  << ','
           << confidenceStr(c.confidence)                       << ','
           << quoted(QString::fromStdString(c.line_id))         << ','
           << quoted(QString::fromStdString(c.notes))           << '\n';
    }

    ts.flush();
    if (file.error() != QFileDevice::NoError) {
        file.close();
        file.remove();
        QMessageBox::warning(this, tr("Export Failed"),
            tr("Export failed: could not write to file."));
        return;
    }

    appendJobMessage(tr("Exported %1 contact(s) to %2")
        .arg(static_cast<int>(contacts.size()))
        .arg(QFileInfo(path).fileName()));
    recordActivity(ActivityKind::Export,
        tr("Exported %1 contact(s) → %2")
            .arg(static_cast<int>(contacts.size()))
            .arg(QFileInfo(path).fileName()));
}

void MainWindow::onExportGeotiff()
{
    if (!currentProject()) { appendJobMessage(tr("Open a project before exporting.")); return; }
    std::vector<std::string> ids;
    for (const auto& l : currentProject()->layers())
        if (l && l->modality == app::Modality::Raster && l->raster.valid)
            ids.push_back(l->id);
    if (ids.empty()) {
        appendJobMessage(tr("No raster layers to export. Import a raster (GeoTIFF / image) first."));
        return;
    }
    onExportLayers(ids, QStringLiteral("geotiff"));   // single → file dialog, many → folder
}

void MainWindow::onExportKmz()
{
    if (!currentProject()) { appendJobMessage(tr("Open a project before exporting.")); return; }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export as KMZ"), exportStartDir(),
        tr("KMZ files (*.kmz);;All files (*)"));
    if (path.isEmpty()) return;
    appendJobMessage(tr("KMZ export is not yet available in this version."));
}

void MainWindow::onExportNav()
{
    if (!currentProject()) { appendJobMessage(tr("Open a project before exporting.")); return; }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Navigation"), exportStartDir(),
        tr("CSV files (*.csv);;NMEA files (*.nmea *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    appendJobMessage(tr("Navigation export is not yet available in this version."));
}

void MainWindow::onExportPdf()
{
    if (!currentProject()) { appendJobMessage(tr("Open a project before exporting.")); return; }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Survey Report"), exportStartDir(),
        tr("PDF files (*.pdf);;All files (*)"));
    if (path.isEmpty()) return;
    appendJobMessage(tr("PDF report export is not yet available in this version."));
}

void MainWindow::onExportManagerOpen()
{
    if (!m_export_win) {
        auto* win = new ExportManagerWindow(nullptr);
        win->setAttribute(Qt::WA_DeleteOnClose);
        m_export_win = win;
        connect(win, &ExportManagerWindow::exportContactsCsvRequested,
                this, &MainWindow::onExportCsv);
        connect(win, &ExportManagerWindow::exportContactsPdfRequested,
                this, [this]() { exportContactsReport(/*docx=*/false); });
        connect(win, &ExportManagerWindow::exportContactsWordRequested,
                this, [this]() { exportContactsReport(/*docx=*/true); });
        connect(win, &ExportManagerWindow::exportScreenshotRequested,
                this, &MainWindow::onExportScreenshot);
        connect(win, &ExportManagerWindow::exportRastersRequested, this, [this]() {
            if (!currentProject()) return;
            std::vector<std::string> ids;
            for (const auto& l : currentProject()->layers())
                if (l && l->modality == app::Modality::Raster && l->raster.valid)
                    ids.push_back(l->id);
            if (ids.empty()) { appendJobMessage(tr("No raster layers to export.")); return; }
            onExportLayers(ids, QStringLiteral("geotiff"));   // folder dialog for >1
        });
    }
    m_export_win->show();
    m_export_win->raise();
    m_export_win->activateWindow();
}

void MainWindow::exportContactsReport(bool docx)
{
    if (!currentProject()) { appendJobMessage(tr("Open a project before exporting.")); return; }
    const auto& contacts = currentProject()->contacts();
    if (contacts.empty()) { appendJobMessage(tr("No contacts to export.")); return; }

    const QString filter = docx ? tr("Word Document (*.docx)") : tr("PDF Document (*.pdf)");
    const QString ext    = docx ? QStringLiteral(".docx") : QStringLiteral(".pdf");
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Contact Report"), exportStartDir(), filter);
    if (path.isEmpty()) return;
    if (!path.endsWith(ext, Qt::CaseInsensitive)) path += ext;

    const QString title = tr("Contact Report — %1")
        .arg(QString::fromStdString(currentProject()->name()));
    const std::vector<core::Contact> rows(contacts.begin(), contacts.end());
    const bool ok = docx ? ContactReport::writeDocx(path, title, rows, currentProject())
                         : ContactReport::writePdf(path, title, rows, currentProject());
    if (ok) {
        appendJobMessage(tr("Exported %1 contact(s) → %2")
            .arg(static_cast<int>(rows.size())).arg(QFileInfo(path).fileName()));
        recordActivity(ActivityKind::Export,
            tr("Exported contact report → %1").arg(QFileInfo(path).fileName()));
    } else {
        QMessageBox::warning(this, tr("Export Failed"), tr("Could not write the report file."));
    }
}

void MainWindow::onExportScreenshot()
{
    const QString base = currentProject()
        ? QString::fromStdString(currentProject()->name())
        : QStringLiteral("DolphinExplorer");
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString suggested = QDir(exportStartDir())
        .filePath(QString("%1_screenshot_%2.png").arg(base, stamp));

    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Screenshot"), suggested,
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg);;All files (*)"));
    if (path.isEmpty()) return;

    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty())
        path += QStringLiteral(".png");

    // Render the shell into an offscreen QImage instead of grab()-ing the live
    // window. grab() forces DWM/OpenGL recomposition while a QOpenGLWidget is
    // visible and causes the entire window to visibly flicker on Windows.
    const qreal dpr = devicePixelRatio();
    QImage image(QSize(qRound(width() * dpr), qRound(height() * dpr)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::black);
    render(&image);

    if (m_viewport_host) {
        const QImage viewport = m_viewport_host->grabViewportImage();
        if (!viewport.isNull()) {
            const QPoint top_left = m_viewport_host->mapTo(this, QPoint(0, 0));
            const QRect target(
                QPoint(qRound(top_left.x() * dpr), qRound(top_left.y() * dpr)),
                QSize(qRound(m_viewport_host->width() * dpr),
                      qRound(m_viewport_host->height() * dpr)));

            QPainter painter(&image);
            painter.drawImage(target, viewport);
        }
    }

    const QByteArray format =
        QFileInfo(path).suffix().compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0 ||
        QFileInfo(path).suffix().compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0
            ? QByteArrayLiteral("JPG")
            : QByteArrayLiteral("PNG");

    if (!image.save(path, format.constData())) {
        QMessageBox::warning(this, tr("Export Failed"),
            tr("Could not write screenshot to:\n%1").arg(path));
        return;
    }

    appendJobMessage(tr("Screenshot exported to %1").arg(QFileInfo(path).fileName()));
    recordActivity(ActivityKind::Export,
        tr("Screenshot → %1").arg(QFileInfo(path).fileName()));
}

bool MainWindow::exportRasterLayer(app::DataLayer* layer, const QString& path)
{
    if (!layer || !layer->raster.valid) return false;
    std::string err;
    bool ok = false;
    if (layer->raster.is_depth) {
        core::RasterGrid g;
        if (io::readElevationRaster(layer->artifact_store_path, g, &err))
            ok = io::writeElevationGeoTiff(path.toStdString(), g, &err);
    } else {
        io::RasterImage im;
        if (io::readImageRaster(layer->artifact_store_path, im, &err))
            ok = io::writeImageGeoTiff(path.toStdString(), im.width, im.height, im.rgba,
                                       im.geo_transform, im.crs_wkt, /*alpha*/ true, &err);
    }
    if (ok) {
        appendJobMessage(tr("Exported GeoTIFF: %1").arg(path));
        recordActivity(ActivityKind::Export,
            tr("Exported raster %1").arg(QFileInfo(path).fileName()));
    } else {
        appendJobMessage(tr("Raster export failed — %1").arg(QString::fromStdString(err)));
    }
    return ok;
}

void MainWindow::onExportLayers(const std::vector<std::string>& layer_ids,
                                const QString& format)
{
    if (!currentProject() || layer_ids.empty()) return;

    // Raster layers export to GeoTIFF via GDAL (RasterWriter).
    std::vector<app::DataLayer*> rasters;
    for (const auto& id : layer_ids)
        if (auto* l = currentProject()->findLayer(id);
            l && l->modality == app::Modality::Raster && l->raster.valid)
            rasters.push_back(l);

    if (rasters.empty()) {
        appendJobMessage(tr("Export as %1 is not yet available for these layers.")
            .arg(format.toUpper()));
        return;
    }

    if (rasters.size() == 1) {
        const QString def = QString::fromStdString(rasters.front()->label) + ".tif";
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export GeoTIFF"), def, tr("GeoTIFF (*.tif *.tiff)"));
        if (path.isEmpty()) return;
        exportRasterLayer(rasters.front(), path);
    } else {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Export GeoTIFFs to Folder"));
        if (dir.isEmpty()) return;
        int n = 0;
        for (auto* l : rasters)
            if (exportRasterLayer(l, dir + "/" + QString::fromStdString(l->label) + ".tif")) ++n;
        appendJobMessage(tr("Exported %n GeoTIFF(s)", nullptr, n));
    }
}

void MainWindow::onMergeLayers(const std::vector<std::string>& layer_ids)
{
    appendJobMessage(tr("Merge Lines is not yet available in this version."));
}

// -- Tool stubs ----------------------------------------------------------------

void MainWindow::onToolCursor()
{
    if (m_map_view) m_map_view->setInputMode(MapView::ModePan);
    if (m_viewport_host) m_viewport_host->setToolMode(ToolMode::Pan);
    if (m_cursor_btn) { QSignalBlocker sb(m_cursor_btn); m_cursor_btn->setChecked(true); }
    m_app_state->setToolMode(ToolMode::Pan);
}

void MainWindow::onToolSelect()
{
    if (m_map_view) m_map_view->setInputMode(MapView::ModeSelect);
    if (m_viewport_host) m_viewport_host->setToolMode(ToolMode::Select);
    if (m_select_btn) { QSignalBlocker sb(m_select_btn); m_select_btn->setChecked(true); }
    m_app_state->setToolMode(ToolMode::Select);
}

void MainWindow::onToolZoom()
{
    if (m_map_view) m_map_view->setInputMode(MapView::ModeZoom);
    if (m_viewport_host) m_viewport_host->setToolMode(ToolMode::Zoom);
    if (m_zoom_btn) { QSignalBlocker sb(m_zoom_btn); m_zoom_btn->setChecked(true); }
    m_app_state->setToolMode(ToolMode::Zoom);
}

void MainWindow::onToolMeasure()
{
    if (m_map_view) m_map_view->setInputMode(MapView::ModeMeasure);
    if (m_viewport_host) m_viewport_host->setToolMode(ToolMode::Measure);
    if (m_measure_btn) { QSignalBlocker sb(m_measure_btn); m_measure_btn->setChecked(true); }
    m_app_state->setToolMode(ToolMode::Measure);
    appendJobMessage(tr("Click to add points. Right-click or double-click to reset."));
}


void MainWindow::onBottomTrack()
{
    if (!currentProject() || activeLayerId().empty()) {
        appendJobMessage(tr("Select a sub-bottom layer first."));
        return;
    }
    const auto* layer = currentProject()->findLayer(activeLayerId());
    if (!layer || layer->modality != app::Modality::SubBottom) {
        appendJobMessage(tr("Sub-bottom Viewer requires a sub-bottom profiler layer."));
        return;
    }
    // Open the viewer — the seabed picks are overlaid there
    onSubBottomOpen();
}

// -- Contact / layer actions ---------------------------------------------------

void MainWindow::onAddContact()
{
    if (!currentProject()) {
        appendJobMessage(tr("Open a project before placing contacts."));
        return;
    }
    if (m_map_view) m_map_view->setInputMode(MapView::ModePickContact);
    if (m_viewport_host) m_viewport_host->setToolMode(ToolMode::ContactPick);
    if (m_contact_btn) { QSignalBlocker sb(m_contact_btn); m_contact_btn->setChecked(true); }
    m_app_state->setToolMode(ToolMode::ContactPick);
    appendJobMessage(tr("Click on the map to place a contact. Press V or another tool to stop."));
}

void MainWindow::onToggle3D()
{
    if (!m_viewport_host) return;
    m_viewport_host->setMode3D(!m_viewport_host->isMode3D());
}

void MainWindow::onMeasurementUpdated(double metres)
{
    if (metres < 0.0) {
        appendJobMessage({});
        return;
    }
    QString text;
    if (metres >= 1000.0)
        text = tr("Distance: %1 km").arg(metres / 1000.0, 0, 'f', 3);
    else
        text = tr("Distance: %1 m").arg(metres, 0, 'f', 1);
    appendJobMessage(text);
}

void MainWindow::onContactPickedOnMap(double lon, double lat)
{
    if (!currentProject()) return;
    core::Contact c;
    c.lat = lat;
    c.lon = lon;
    c.spatial_ref = currentProject()->displaySpatialRef();
    // Leave the label empty: the project assigns a stable, monotonic "Cnnn" from the
    // contact id, so removals never cause a later pick to reuse a surviving number.
    auto* cmd = new AddContactCommand(currentProject(), c, [this]() {  });
    m_undo_stack->push(cmd);

    QString label;
    for (const auto& ct : currentProject()->contacts())
        if (ct.id == cmd->assignedId()) { label = QString::fromStdString(ct.label); break; }

    appendJobMessage(tr("Contact %1 placed at %2, %3")
        .arg(label)
        .arg(lat, 0, 'f', 6)
        .arg(lon, 0, 'f', 6));
    recordActivity(ActivityKind::ContactPick,
        tr("Contact %1 placed").arg(label));
}

void MainWindow::onRenumberContacts()
{
    const QString layer_name = (!activeLayerId().empty() && currentProject())
        ? [&]() -> QString {
              auto* l = currentProject()->findLayer(activeLayerId());
              return l ? QString::fromStdString(l->label) : tr("No layer selected");
          }()
        : tr("No layer selected");

    auto* dlg = new SidescanCorrectionDialog(
        SidescanCorrectionDialog::Mode::Navigation, layer_name, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &SidescanCorrectionDialog::applyRequested,
            this, [this, dlg](SidescanCorrectionDialog::ApplyScope /*scope*/) {
                if (m_sss_ctrl) {
                    m_sss_ctrl->setGeorefParams(dlg->headingParams());
                    m_sss_ctrl->reloadCurrentLayer();
                }
            });
    connect(dlg, &SidescanCorrectionDialog::previewRequested,
            this, [this, dlg]() {
                if (m_sss_ctrl) {
                    m_sss_ctrl->setGeorefParams(dlg->headingParams());
                    m_sss_ctrl->reloadLayer(activeLayerId());
                }
            });
    connect(dlg, &SidescanCorrectionDialog::resetRequested,
            this, [this]() {
                if (m_sss_ctrl) {
                    m_sss_ctrl->setGeorefParams({});
                    m_sss_ctrl->reloadLayer(activeLayerId());
                }
            });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::onLineProps()
{
    const QString layer_name = (!activeLayerId().empty() && currentProject())
        ? [&]() -> QString {
              auto* l = currentProject()->findLayer(activeLayerId());
              return l ? QString::fromStdString(l->label) : tr("No layer selected");
          }()
        : tr("No layer selected");

    auto* dlg = new SidescanCorrectionDialog(
        SidescanCorrectionDialog::Mode::Heading, layer_name, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &SidescanCorrectionDialog::applyRequested,
            this, [this, dlg](SidescanCorrectionDialog::ApplyScope /*scope*/) {
                if (m_sss_ctrl) {
                    m_sss_ctrl->setGeorefParams(dlg->headingParams());
                    m_sss_ctrl->reloadCurrentLayer();
                }
            });
    connect(dlg, &SidescanCorrectionDialog::previewRequested,
            this, [this, dlg]() {
                if (m_sss_ctrl) {
                    m_sss_ctrl->setGeorefParams(dlg->headingParams());
                    m_sss_ctrl->reloadLayer(activeLayerId());
                }
            });
    connect(dlg, &SidescanCorrectionDialog::resetRequested,
            this, [this]() {
                if (m_sss_ctrl) {
                    m_sss_ctrl->setGeorefParams({});
                    m_sss_ctrl->reloadLayer(activeLayerId());
                }
            });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::onResetRaw()
{
    if (!m_sss_ctrl || activeLayerId().empty()) {
        appendJobMessage(tr("Select a layer first."));
        return;
    }
    m_sss_ctrl->setGeorefParams({});
    m_sss_ctrl->reloadCurrentLayer();
    appendJobMessage(tr("Layer reloaded with raw navigation."));
}

void MainWindow::onClearContacts()
{
    if (!currentProject()) return;
    const auto& contacts = currentProject()->contacts();
    if (contacts.empty()) return;
    if (QMessageBox::question(this, tr("Clear Contacts"),
            tr("Move all contacts to the Recycle Bin?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    // Snapshot ids first (recycling mutates the list), then one undoable macro.
    std::vector<uint64_t> ids;
    ids.reserve(contacts.size());
    for (const auto& c : contacts) ids.push_back(c.id);
    m_undo_stack->beginMacro(tr("Clear Contacts"));
    for (uint64_t id : ids)
        m_undo_stack->push(new RecycleContactCommand(currentProject(), id));
    m_undo_stack->endMacro();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About Dolphin Explorer"),
        tr("<b>Dolphin Explorer</b><br>"
           "Version 0.1<br><br>"
           "Marine survey data review platform.<br>"
           "© Astra Marine"));
}

} // namespace dolphin::ui
