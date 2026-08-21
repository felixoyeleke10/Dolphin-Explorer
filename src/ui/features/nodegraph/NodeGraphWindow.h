#pragma once
#include <QWidget>
#include <QString>
#include <string>

class QCloseEvent;
class QEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPoint;
class QScrollArea;
class QTabBar;
class QToolButton;

namespace dolphin::app { class DataLayer; class Project; }
namespace dolphin::app::workers { class Worker; }
namespace dolphin::pipeline { class NodeGraph; }

namespace dolphin::ui {

class NodeGraphView;
class NodePaletteView;
class NodeInspectorPanel;

// -----------------------------------------------------------------------------
//  NodeGraphWindow — top-level window hosting the visual node graph editor.
//
//  Layout (vertical):
//    [ Top bar: Layer combo | Auto-Layout | Frame All | Run Worker ]
//    [ Processing | QC | Report ]  ← worker tab bar
//    [ Palette (160px) | NodeGraphView (stretch) | Inspector (260px) ]
//
//  Each tab shows the graph of one project worker (Processing / QC / Reporting).
//  The layer combo selects which layer to run against.
// -----------------------------------------------------------------------------
class NodeGraphWindow : public QWidget {
    Q_OBJECT
public:
    explicit NodeGraphWindow(QWidget* parent = nullptr);

    // Bind to a project (and optionally a pre-selected layer run target).
    void setLayer(dolphin::app::DataLayer* layer,
                  dolphin::app::Project*   project);

    // Refresh the layer combo after layers are added/removed/reordered,
    // without resetting the processing graph or worker tabs.
    void refreshLayerList();

signals:
    void graphModified();   // forwarded to MainWindow → marks project dirty
    void runRequested();    // MainWindow should call onRunSelectedLayer
    void revertRequested(const std::string& layer_id);
    void importRequested();
    void layerSelectionRequested(const std::string& layer_id);

protected:
    void closeEvent(QCloseEvent* ev) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onLayerComboChanged(int index);
    void onWorkerTabChanged(int index);
    void onNodeSelected(const std::string& node_id);
    void onGraphModified();
    void onRun();

private:
    QWidget* buildTopBar(QWidget* parent);
    QWidget* buildWorkerTabBar(QWidget* parent);
    void     buildPalette(QWidget* parent);
    void     filterPalette(const QString& text);
    void     rebuildWorkerTabs();
    bool     ensurePlacementTarget();
    void     armPalettePlacement(const QString& type_id);
    void     beginPaletteDrag(const QString& type_id,
                              const QPoint&  global_pos);
    void     cancelPaletteDrag();
    void     updatePaletteDrag(const QPoint& global_pos);
    void     finishPaletteDrag(bool commit, const QPoint& global_pos);

    NodeGraphView*      m_canvas    = nullptr;
    NodePaletteView*    m_palette   = nullptr;
    NodeInspectorPanel* m_inspector = nullptr;

    dolphin::app::DataLayer*      m_layer   = nullptr;
    dolphin::app::Project*        m_project = nullptr;
    dolphin::pipeline::NodeGraph* m_graph   = nullptr;
    int                           m_active_worker_tab = 0;

    QTabBar*     m_worker_tabs  = nullptr;
    QComboBox*   m_layer_combo  = nullptr;
    QToolButton* m_run_btn      = nullptr;
    QToolButton* m_revert_btn   = nullptr;
    QString      m_palette_drag_type_id;
    bool         m_palette_drag_live  = false;
    bool         m_palette_drop_valid = false;
    QPoint       m_palette_drop_pos;
};

} // namespace dolphin::ui
