#pragma once

#include <QObject>
#include <QPointer>
#include <functional>
#include <string>
#include <vector>

class QWidget;

namespace dolphin::app {
class DataLayer;
class Project;
}

namespace dolphin::ui {

class MapViewportHost;

// Owns export dialogs, file IO, the lazy Export Manager window, and export
// result reporting. MainWindow supplies only the current-project and shell
// composition dependencies.
class ExportController : public QObject {
    Q_OBJECT
public:
    using ProjectProvider = std::function<app::Project*()>;

    explicit ExportController(ProjectProvider project_provider,
                              QWidget* capture_widget,
                              MapViewportHost* viewport_host,
                              QWidget* dialog_parent,
                              QObject* parent = nullptr);

    void exportLayers(const std::vector<std::string>& layer_ids,
                      const QString& format);

public slots:
    void exportContactsCsv();
    void exportGeoTiff();
    void exportScreenshot();
    void openManager();

signals:
    void statusMessage(const QString& message);
    void activityRecorded(const QString& description);

private:
    app::Project* project() const;
    void exportContactsReport(bool docx);
    bool exportRasterLayer(app::DataLayer* layer, const QString& path);

    ProjectProvider    m_project_provider;
    QWidget*           m_capture_widget = nullptr;
    MapViewportHost*   m_viewport_host = nullptr;
    QWidget*           m_dialog_parent = nullptr;
    QPointer<QWidget>  m_window;
};

} // namespace dolphin::ui
