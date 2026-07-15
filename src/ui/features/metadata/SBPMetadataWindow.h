#pragma once
#include <QAbstractTableModel>
#include <QColor>
#include <QWidget>
#include <QVector>
#include <string>
#include <vector>
#include "core/SubBottomTrace.h"

class QCheckBox;
class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QSortFilterProxyModel;
class QSpinBox;
class QSplitter;
class QTableView;
class QToolButton;

namespace dolphin::app {
class Project;
}

namespace dolphin::ui {

class SSSMetadataPlotWidget;   // defined in SSSMetadataWindow.h; used for the chart pane

// -----------------------------------------------------------------------------
//  Per-field display configuration (mirrors SSSFieldConfig)
// -----------------------------------------------------------------------------
struct SBPFieldConfig {
    bool   visible   = true;
    bool   show_plot = false;
    QColor color;
    int    thickness = 1;
    bool   add_dots  = false;
    int    precision = -1;
};

// -----------------------------------------------------------------------------
//  SBPNavModel — virtual table model backed by SubBottomTrace nav data.
// -----------------------------------------------------------------------------
class SBPNavModel : public QAbstractTableModel {
    Q_OBJECT
public:
    static constexpr int kFieldCount = 26;
    struct FieldDef { const char* name; const char* unit; int default_prec; };
    static const FieldDef kFieldDefs[kFieldCount];

    explicit SBPNavModel(QObject* parent = nullptr);

    void setTraces(std::vector<core::SubBottomTrace> traces);
    void setVisibleFields(const QVector<int>& field_indices);
    void setFieldPrecision(int field_idx, int precision);
    void setCoordinatesProjected(bool projected);

    // Raw double values for one field across all rows (for chart rendering).
    QVector<double> fieldValues(int field_idx) const;

    int      rowCount   (const QModelIndex& parent = {}) const override;
    int      columnCount(const QModelIndex& parent = {}) const override;
    QVariant data       (const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData (int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;

private:
    double  rawValue   (int trace_idx, int field_idx) const;
    QString formatValue(double v, int field_idx) const;

    std::vector<core::SubBottomTrace> m_traces;
    QVector<int>  m_visible;     // field_idx per visible column
    QVector<int>  m_precision;   // per-field precision override (-1 = default)
    bool m_coords_projected = false;
};

// -----------------------------------------------------------------------------
//  SBPMetadataWindow — per-trace SBP nav and acquisition spreadsheet + charts.
//
//  Features (parity with SSSMetadataWindow):
//    • Per-trace virtual table (QAbstractTableModel + proxy sort)
//    • Cell-level multi-select, Ctrl+A, Ctrl+C tab-delimited clipboard
//    • Selection status bar: Count / Sum / Avg / Min / Max
//    • Right-click context menu: copy / export
//    • Export to CSV (all or selection)
//    • Left panel: field checklist + per-field plot config
//    • Chart panel: Line / Scatter / Histogram with axis selectors + zoom/pan
// -----------------------------------------------------------------------------
class SBPMetadataWindow : public QWidget {
    Q_OBJECT
public:
    explicit SBPMetadataWindow(QWidget* parent = nullptr);

    void setProject(app::Project*       project,
                    const std::string&  active_layer_id = {});

protected:
    void keyPressEvent(QKeyEvent* ev) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    // Layer / load
    void onLineSelectionChanged();

    // Field panel
    void onSearchTextChanged(const QString& text);
    void onFieldListItemChanged();
    void onFieldListCurrentChanged(int row);

    // Per-field config
    void onShowInPlotToggled(bool checked);
    void onColorButtonClicked();
    void onThicknessChanged(int v);
    void onDotsToggled(bool checked);
    void onPrecisionChanged(int v);

    // Chart toolbar
    void onToggleChart(bool show);
    void onUndockChart();
    void onChartTypeChanged(int idx);
    void onChartXChanged(int idx);
    void onChartYChanged(int idx);
    void onChartBinsChanged(int v);

    // Export / clipboard
    void onExportAll();
    void onExportSelection();
    void onCopyAll();
    void onCopySelection();

    // Table
    void onSelectionChanged();
    void showTableContextMenu(const QPoint& pos);

private:
    void buildUi();
    void buildFieldPanel(QWidget* parent);
    void buildChartToolbar(QWidget* parent);

    void startLoad();
    void onTracesLoaded(std::vector<core::SubBottomTrace> traces);
    void updateLineButtonLabel();

    void applyFieldVisibility();
    void updateFieldConfig(int fi);
    void saveFieldConfig(int fi);
    void updatePlot();
    void updateChart();

    QString buildTabText(bool selection_only, bool with_headers) const;
    void    exportToCsv (bool selection_only);

    // -- Project / data --------------------------------------------------------
    app::Project*        m_project        = nullptr;
    std::string          m_active_layer_id;
    int                  m_load_gen       = 0;
    std::vector<core::SubBottomTrace> m_all_traces;

    QVector<SBPFieldConfig> m_field_cfg;

    // -- Model / view ----------------------------------------------------------
    SBPNavModel*           m_model  = nullptr;
    QSortFilterProxyModel* m_proxy  = nullptr;
    QTableView*            m_table  = nullptr;
    SSSMetadataPlotWidget* m_plot   = nullptr;

    // -- Chart dock/toggle state -----------------------------------------------
    QSplitter*   m_outer_vsplit     = nullptr;
    QWidget*     m_chart_pane       = nullptr;
    QToolButton* m_btn_toggle_chart = nullptr;
    QToolButton* m_btn_undock_chart = nullptr;
    bool         m_chart_floating   = false;

    // -- Top bar ---------------------------------------------------------------
    QToolButton* m_line_btn    = nullptr;
    QMenu*       m_line_menu   = nullptr;
    QLabel*      m_load_status = nullptr;

    // -- Selection status bar (below table) -----------------------------------
    QLabel* m_sel_status = nullptr;

    // -- Left panel — field list -----------------------------------------------
    QListWidget* m_field_list    = nullptr;

    // -- Left panel — per-field config -----------------------------------------
    QLabel*      m_cfg_name      = nullptr;
    QCheckBox*   m_cfg_plot_cb   = nullptr;
    QToolButton* m_cfg_color_btn = nullptr;
    QSpinBox*    m_cfg_thick_sp  = nullptr;
    QCheckBox*   m_cfg_dots_cb   = nullptr;
    QSpinBox*    m_cfg_prec_sp   = nullptr;
    QWidget*     m_cfg_panel     = nullptr;
    int          m_selected_field = -1;
    bool         m_updating_cfg   = false;

    // -- Chart toolbar ---------------------------------------------------------
    QComboBox* m_chart_type_cb  = nullptr;
    QLabel*    m_chart_x_lbl    = nullptr;
    QComboBox* m_chart_x_cb     = nullptr;
    QComboBox* m_chart_y_cb     = nullptr;
    QLabel*    m_chart_bins_lbl = nullptr;
    QSpinBox*  m_chart_bins_sp  = nullptr;
};

} // namespace dolphin::ui
