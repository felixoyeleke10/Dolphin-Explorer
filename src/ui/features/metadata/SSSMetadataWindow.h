#pragma once
#include <QAbstractTableModel>
#include <QColor>
#include <QWidget>
#include <QVector>
#include <QStringList>
#include <string>
#include <vector>
#include "core/SidescanPing.h"

class QAction;
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
class QTimer;
class QToolButton;

namespace dolphin::app {
class DataLayer;
class Project;
}

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Per-field display configuration
// -----------------------------------------------------------------------------
struct SSSFieldConfig {
    bool   visible   = true;
    bool   show_plot = false;
    QColor color;
    int    thickness = 1;
    bool   add_dots  = false;
    int    precision = -1;  // -1 = field default
};

// -----------------------------------------------------------------------------
//  SSSNavModel — virtual QAbstractTableModel backed by nav-only SidescanPings.
// -----------------------------------------------------------------------------
class SSSNavModel : public QAbstractTableModel {
    Q_OBJECT
public:
    static constexpr int kFieldCount = 39;

    struct FieldDef {
        const char* name;
        const char* unit;
        int         default_prec;
    };
    static const FieldDef kFieldDefs[kFieldCount];

    explicit SSSNavModel(QObject* parent = nullptr);

    void setPings(std::vector<core::SidescanPing> pings);
    void setVisibleFields(const QVector<int>& field_indices);
    void setFieldPrecision(int field_idx, int precision);
    void setCoordinatesProjected(bool projected);

    // Raw double values for one field across all rows (for chart rendering).
    QVector<double> fieldValues(int field_idx) const;

    int rowCount   (const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data      (const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    double  rawValue   (int ping_idx, int field_idx) const;
    QString formatValue(double v, int field_idx) const;

    std::vector<core::SidescanPing> m_pings;
    QVector<int>                    m_visible;    // field_idx per visible column
    QVector<int>                    m_precision;  // per-field override (-1 = default)
    bool                            m_coords_projected = false;
};

// -----------------------------------------------------------------------------
//  Chart data structs
// -----------------------------------------------------------------------------
struct SSSPlotSeries {
    QString          label;
    QColor           color;
    int              thickness = 1;
    bool             add_dots  = false;
    QVector<double>  values;
};

enum class SSSChartMode { Line, Scatter, Histogram };

// -----------------------------------------------------------------------------
//  SSSMetadataPlotWidget — three chart modes: Line / Scatter / Histogram.
//
//  Line:      one or more series (Y vs ping index).
//  Scatter:   two fields as X/Y, each ping as a dot.  Hover not yet wired.
//  Histogram: frequency distribution of one field, configurable bin count.
// -----------------------------------------------------------------------------
class SSSMetadataPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit SSSMetadataPlotWidget(QWidget* parent = nullptr);

    void setLineSeries    (const QVector<SSSPlotSeries>& series);
    void setScatterData   (const QVector<double>& xs, const QVector<double>& ys,
                           const QString& x_label, const QString& y_label,
                           QColor color = {0x48, 0xa8, 0xe8});
    void setHistogramData (const QVector<double>& values, int bins,
                           const QString& field_label,
                           QColor color = {0x48, 0xa8, 0xe8});
    void clear();

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent (QWheelEvent*) override;
    void mousePressEvent  (QMouseEvent*) override;
    void mouseMoveEvent   (QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    struct PlotMetrics {
        QRect  area;   // drawable pixel rect
        double xmin, xmax, ymin, ymax;
        auto toPixel(double x, double y) const -> QPointF;
        auto fromPixelX(double px) const -> double;
    };

    void paintBackground(QPainter& p, const QRect& plot) const;
    void paintGrid      (QPainter& p, const QRect& plot, int nx, int ny) const;
    void paintAxes      (QPainter& p, const QRect& plot) const;
    void paintXTicks    (QPainter& p, const PlotMetrics& m, int nx) const;
    void paintYTicks    (QPainter& p, const PlotMetrics& m, int ny) const;
    void paintLine      (QPainter& p, const PlotMetrics& m) const;
    void paintScatter   (QPainter& p, const PlotMetrics& m) const;
    void paintHistogram (QPainter& p, const PlotMetrics& m) const;
    void paintLegend    (QPainter& p, const QRect& plot) const;
    void paintEmptyHint (QPainter& p, const QRect& plot) const;

    PlotMetrics computeMetrics(const QRect& plot) const;

    SSSChartMode           m_mode     = SSSChartMode::Line;
    QVector<SSSPlotSeries> m_series;
    QVector<double>        m_xs, m_ys;
    QString                m_x_label, m_y_label;
    QVector<double>        m_hist_values;
    int                    m_hist_bins   = 20;
    QString                m_hist_label;
    QColor                 m_dot_color   = {0x48, 0xa8, 0xe8};

    // Pan/zoom state
    double  m_zoom   = 1.0;
    double  m_pan_x  = 0.0;
    QPointF m_drag_start;
    bool    m_dragging = false;
};

// -----------------------------------------------------------------------------
//  SSSMetadataWindow — Excel-inspired per-ping SSS data spreadsheet + charts.
//
//  Features:
//    • Per-ping virtual table (QAbstractTableModel + proxy sort)
//    • Cell-level multi-select, Ctrl+A, Ctrl+C tab-delimited clipboard
//    • Selection status bar: Count / Sum / Avg / Min / Max
//    • Right-click context menu: copy / export
//    • Export to CSV (all or selection)
//    • Right panel: field checklist + per-field plot config
//    • Chart panel: Line / Scatter / Histogram with axis selectors + zoom/pan
// -----------------------------------------------------------------------------
class SSSMetadataWindow : public QWidget {
    Q_OBJECT
public:
    explicit SSSMetadataWindow(QWidget* parent = nullptr);

    void setProject(app::Project*        project,
                    const std::string&   active_layer_id = {});

    void setVisiblePingRange(int first_ping, int count);

protected:
    void keyPressEvent(QKeyEvent* ev) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    // Layer / load
    void onLineSelectionChanged();
    void onShowOnlyVisibleToggled(bool checked);
    void onToggleChart(bool show);
    void onUndockChart();

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
    void onChartTypeChanged(int idx);
    void onChartXChanged(int idx);
    void onChartYChanged(int idx);
    void onChartBinsChanged(int v);

    // Export / clipboard
    void onExportAll();
    void onExportSelection();
    void onCopyAll();
    void onCopySelection();

    // Table selection
    void onSelectionChanged();

    // Table context menu
    void showTableContextMenu(const QPoint& pos);

private:
    void buildUi();
    void buildFieldPanel(QWidget* parent);
    void buildChartToolbar(QWidget* parent);

    void startLoad();
    void onPingsLoaded(std::vector<core::SidescanPing> pings);
    void applyVisibleFilter();
    void updateLineButtonLabel();

    void applyFieldVisibility();
    void updatePlot();
    void updateChart();
    void updateSelectionStatus();
    void updateFieldConfig(int fi);
    void saveFieldConfig(int fi);

    QString buildTabText(bool selection_only, bool with_headers) const;
    void    exportToCsv (bool selection_only);

    // -- Project / data --------------------------------------------------------
    app::Project*        m_project       = nullptr;
    std::string          m_active_layer_id;
    int                  m_load_gen      = 0;
    QTimer*              m_load_debounce = nullptr;
    int                  m_visible_first = 0;
    int                  m_visible_count = -1;
    std::vector<core::SidescanPing> m_all_pings;

    QVector<SSSFieldConfig> m_field_cfg;

    // -- Model / view ---------------------------------------------------------
    SSSNavModel*           m_model  = nullptr;
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
    QToolButton* m_line_btn      = nullptr;
    QMenu*       m_line_menu     = nullptr;
    QCheckBox*   m_visible_only_cb = nullptr;
    QLabel*      m_load_status   = nullptr;

    // -- Selection status bar (below table) -----------------------------------
    QLabel*    m_sel_status      = nullptr;

    // -- Right panel — field list ----------------------------------------------
    QListWidget* m_field_list    = nullptr;

    // -- Right panel — per-field config ---------------------------------------
    QLabel*      m_cfg_name      = nullptr;
    QCheckBox*   m_cfg_plot_cb   = nullptr;
    QToolButton* m_cfg_color_btn = nullptr;
    QSpinBox*    m_cfg_thick_sp  = nullptr;
    QCheckBox*   m_cfg_dots_cb   = nullptr;
    QSpinBox*    m_cfg_prec_sp   = nullptr;
    QWidget*     m_cfg_panel     = nullptr;

    // -- Chart toolbar ---------------------------------------------------------
    QComboBox* m_chart_type_cb  = nullptr;
    QLabel*    m_chart_x_lbl    = nullptr;
    QComboBox* m_chart_x_cb     = nullptr;
    QComboBox* m_chart_y_cb     = nullptr;
    QLabel*    m_chart_bins_lbl = nullptr;
    QSpinBox*  m_chart_bins_sp  = nullptr;

    int  m_selected_field = -1;
    bool m_updating_cfg   = false;
};

} // namespace dolphin::ui
