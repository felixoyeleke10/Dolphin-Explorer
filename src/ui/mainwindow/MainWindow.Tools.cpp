// MainWindow.Tools.cpp — export/tool stubs, contact actions, onAbout.
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/sidescan/SidescanCorrectionDialog.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
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
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export as GeoTIFF"), exportStartDir(),
        tr("GeoTIFF files (*.tif *.tiff);;All files (*)"));
    if (path.isEmpty()) return;
    appendJobMessage(tr("GeoTIFF export is not yet available in this version."));
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

void MainWindow::onExportLayers(const std::vector<std::string>& layer_ids,
                                const QString& format)
{
    appendJobMessage(tr("Export as %1 is not yet available in this version.")
        .arg(format.toUpper()));
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
    if (!currentProject() || m_active_layer_id.empty()) {
        appendJobMessage(tr("Select a sub-bottom layer first."));
        return;
    }
    const auto* layer = currentProject()->findLayer(m_active_layer_id);
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
    const int n = static_cast<int>(currentProject()->contacts().size()) + 1;
    c.label = "C" + QString::number(n).rightJustified(3, '0').toStdString();
    m_undo_stack->push(new AddContactCommand(
        currentProject(), c,
        [this]() {  }));
    appendJobMessage(tr("Contact %1 placed at %2, %3")
        .arg(QString::fromStdString(c.label))
        .arg(lat, 0, 'f', 6)
        .arg(lon, 0, 'f', 6));
    recordActivity(ActivityKind::ContactPick,
        tr("Contact %1 placed").arg(QString::fromStdString(c.label)));
}

void MainWindow::onRenumberContacts()
{
    const QString layer_name = (!m_active_layer_id.empty() && currentProject())
        ? [&]() -> QString {
              auto* l = currentProject()->findLayer(m_active_layer_id);
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
                    m_sss_ctrl->reloadLayer(m_active_layer_id);
                }
            });
    connect(dlg, &SidescanCorrectionDialog::resetRequested,
            this, [this]() {
                if (m_sss_ctrl) {
                    m_sss_ctrl->setGeorefParams({});
                    m_sss_ctrl->reloadLayer(m_active_layer_id);
                }
            });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::onLineProps()
{
    const QString layer_name = (!m_active_layer_id.empty() && currentProject())
        ? [&]() -> QString {
              auto* l = currentProject()->findLayer(m_active_layer_id);
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
                    m_sss_ctrl->reloadLayer(m_active_layer_id);
                }
            });
    connect(dlg, &SidescanCorrectionDialog::resetRequested,
            this, [this]() {
                if (m_sss_ctrl) {
                    m_sss_ctrl->setGeorefParams({});
                    m_sss_ctrl->reloadLayer(m_active_layer_id);
                }
            });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::onResetRaw()
{
    if (!m_sss_ctrl || m_active_layer_id.empty()) {
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
            tr("Remove all contacts from this project?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    m_undo_stack->push(new ClearContactsCommand(
        currentProject(),
        std::vector<core::Contact>(contacts.begin(), contacts.end()),
        [this]() {  }));
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
