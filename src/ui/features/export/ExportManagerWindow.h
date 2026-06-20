#pragma once
#include <QMainWindow>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class QButtonGroup;
class QWidget;
class QHBoxLayout;

namespace dolphin::ui {

// Project-wide export hub, laid out like the Contact Manager: a left nav tree of
// export targets, a centre configuration pane (format + Export), and a right
// preview/summary pane. Pure UI — it emits an intent signal per export; MainWindow
// owns the project and performs the work. Not-yet-available targets are disabled.
class ExportManagerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ExportManagerWindow(QWidget* parent = nullptr);

signals:
    void exportContactsCsvRequested();
    void exportContactsPdfRequested();
    void exportContactsWordRequested();
    void exportScreenshotRequested();
    void exportRastersRequested();          // all raster layers -> GeoTIFF

private:
    QWidget* buildNavPane();
    QWidget* buildCentrePane();
    QWidget* buildPreviewPane();
    void     selectTarget(int index);   // populate centre + preview for a nav item
    void     doExport();

    QTreeWidget*  m_nav      = nullptr;
    QLabel*       m_title    = nullptr;
    QLabel*       m_desc     = nullptr;
    QHBoxLayout*  m_formats  = nullptr;   // segmented format buttons
    QButtonGroup* m_fmt_grp  = nullptr;
    QPushButton*  m_export   = nullptr;

    QLabel* m_pv_icon   = nullptr;
    QLabel* m_pv_title  = nullptr;
    QLabel* m_pv_detail = nullptr;

    int m_current = -1;
};

} // namespace dolphin::ui
