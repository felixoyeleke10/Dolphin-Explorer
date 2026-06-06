// SBPMetadataWindow.cpp — constructor, chart control, field config, selection, keyboard, context menu.
#include "ui/features/metadata/SBPMetadataWindow.h"
#include "ui/features/metadata/SSSMetadataWindow.h"   // SSSMetadataPlotWidget, SSSPlotSeries
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <limits>

namespace dolphin::ui {

static constexpr int kInitW = 1200;
static constexpr int kInitH = 740;

// -- Default chart colours (one per field) ------------------------------------
static const QColor kDefaultColors[SBPNavModel::kFieldCount] = {
    {0x88,0x88,0x88}, {0x68,0x88,0x68},   // #, trace id
    {0x88,0x88,0x88}, {0x88,0x88,0x98},   // time, nav time
    {0x48,0xa8,0xe8}, {0x48,0xe8,0xa8},   // lat, lon
    {0x28,0x88,0xc8}, {0x28,0xc8,0x88},   // fish lat, fish lon
    {0x18,0x68,0xa8}, {0x18,0xa8,0x68},   // vessel lat, vessel lon
    {0xc8,0x88,0x38}, {0xc8,0xa8,0x38},   // heading, sensor hdg
    {0xa8,0x88,0x58},                      // ship hdg
    {0x58,0xc8,0x68},                      // speed
    {0xe8,0x88,0x48},                      // altitude
    {0xe8,0x58,0x58}, {0x58,0x88,0xe8},   // roll, pitch
    {0xe8,0xc8,0x48},                      // heave
    {0x48,0xe8,0xe8}, {0xa8,0xa8,0x48},   // frequency, sample rate
    {0x88,0x48,0x48}, {0x48,0x88,0x48},   // tow depth, record time
    {0x78,0x58,0xa8},                      // sample count
    {0x88,0x48,0x88}, {0x58,0xa8,0x78},   // btm sample, btm depth
    {0xa8,0x78,0x48},                      // total depth
};

// -----------------------------------------------------------------------------
//  SBPMetadataWindow — construction
// -----------------------------------------------------------------------------
SBPMetadataWindow::SBPMetadataWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("SBP Survey Data");
    resize(kInitW, kInitH);

    m_field_cfg.resize(SBPNavModel::kFieldCount);
    for (int i = 0; i < SBPNavModel::kFieldCount; ++i)
        m_field_cfg[i].color = kDefaultColors[i];

    m_model = new SBPNavModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole + 1);

    buildUi();
}

// -----------------------------------------------------------------------------
//  Chart toggle / undock
// -----------------------------------------------------------------------------
void SBPMetadataWindow::onToggleChart(bool show)
{
    if (!m_chart_pane) return;
    if (m_chart_floating) {
        if (show) m_chart_pane->show(); else m_chart_pane->hide();
        return;
    }
    m_chart_pane->setVisible(show);
}

void SBPMetadataWindow::onUndockChart()
{
    if (!m_chart_pane || !m_outer_vsplit) return;

    if (!m_chart_floating) {
        const QSize sz = m_chart_pane->size().expandedTo(QSize(600, 220));
        m_chart_pane->setParent(nullptr);
        m_chart_pane->setWindowTitle("Chart — SBP Survey Data");
        m_chart_pane->setWindowFlags(Qt::Window | Qt::Tool);
        m_chart_pane->setAttribute(Qt::WA_DeleteOnClose, false);
        m_chart_pane->resize(sz);
        m_chart_pane->show();
        m_chart_floating = true;
        if (m_btn_undock_chart)
            m_btn_undock_chart->setToolTip("Dock chart back into panel");
        if (m_btn_toggle_chart) {
            QSignalBlocker sb(m_btn_toggle_chart);
            m_btn_toggle_chart->setChecked(true);
        }
    } else {
        m_chart_pane->hide();
        m_outer_vsplit->insertWidget(1, m_chart_pane);
        m_outer_vsplit->setSizes({480, 180});
        m_chart_pane->show();
        m_chart_floating = false;
        if (m_btn_undock_chart)
            m_btn_undock_chart->setToolTip("Pop out chart  /  Dock back");
        if (m_btn_toggle_chart) {
            QSignalBlocker sb(m_btn_toggle_chart);
            m_btn_toggle_chart->setChecked(true);
        }
    }
}

bool SBPMetadataWindow::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_chart_pane) {
        if (ev->type() == QEvent::Hide && m_btn_toggle_chart) {
            QSignalBlocker sb(m_btn_toggle_chart);
            m_btn_toggle_chart->setChecked(false);
        } else if (ev->type() == QEvent::Show && m_btn_toggle_chart) {
            QSignalBlocker sb(m_btn_toggle_chart);
            m_btn_toggle_chart->setChecked(true);
        }
    }
    return QWidget::eventFilter(obj, ev);
}

// -----------------------------------------------------------------------------
//  Chart type / axis selectors
// -----------------------------------------------------------------------------
void SBPMetadataWindow::onChartTypeChanged(int idx)
{
    const bool is_scatter   = (idx == 1);
    const bool is_histogram = (idx == 2);

    m_chart_x_lbl->setVisible(is_scatter);
    m_chart_x_cb ->setVisible(is_scatter);
    m_chart_bins_lbl->setVisible(is_histogram);
    m_chart_bins_sp ->setVisible(is_histogram);

    updateChart();
}

void SBPMetadataWindow::onChartXChanged(int)     { updateChart(); }
void SBPMetadataWindow::onChartYChanged(int)     { updateChart(); }
void SBPMetadataWindow::onChartBinsChanged(int)  { updateChart(); }

// -----------------------------------------------------------------------------
//  Chart data update
// -----------------------------------------------------------------------------
void SBPMetadataWindow::updatePlot()
{
    QVector<SSSPlotSeries> series;
    for (int fi = 0; fi < SBPNavModel::kFieldCount; ++fi) {
        if (!m_field_cfg[fi].show_plot) continue;
        SSSPlotSeries s;
        s.label     = QString::fromUtf8(SBPNavModel::kFieldDefs[fi].name);
        s.color     = m_field_cfg[fi].color;
        s.thickness = m_field_cfg[fi].thickness;
        s.add_dots  = m_field_cfg[fi].add_dots;
        s.values    = m_model->fieldValues(fi);
        series.append(s);
    }
    if (m_chart_type_cb && m_chart_type_cb->currentIndex() == 0)
        m_plot->setLineSeries(series);
}

void SBPMetadataWindow::updateChart()
{
    if (!m_chart_type_cb || !m_plot) return;
    const int type = m_chart_type_cb->currentIndex();

    if (type == 0) { updatePlot(); return; }

    const int fi_y = m_chart_y_cb->currentData().toInt();

    if (type == 1) {
        const int fi_x = m_chart_x_cb->currentData().toInt();
        m_plot->setScatterData(
            m_model->fieldValues(fi_x),
            m_model->fieldValues(fi_y),
            QString::fromUtf8(SBPNavModel::kFieldDefs[fi_x].name),
            QString::fromUtf8(SBPNavModel::kFieldDefs[fi_y].name),
            m_field_cfg[fi_y].color);
    } else {
        m_plot->setHistogramData(
            m_model->fieldValues(fi_y),
            m_chart_bins_sp->value(),
            QString::fromUtf8(SBPNavModel::kFieldDefs[fi_y].name),
            m_field_cfg[fi_y].color);
    }
}

// -----------------------------------------------------------------------------
//  Field visibility and per-field config
// -----------------------------------------------------------------------------
void SBPMetadataWindow::onFieldListItemChanged()
{
    applyFieldVisibility();
}

void SBPMetadataWindow::applyFieldVisibility()
{
    QVector<int> visible;
    for (int i = 0; i < m_field_list->count(); ++i) {
        const auto* item = m_field_list->item(i);
        const int fi = item->data(Qt::UserRole).toInt();
        m_field_cfg[fi].visible = (item->checkState() == Qt::Checked);
        if (m_field_cfg[fi].visible) visible.append(fi);
    }
    m_model->setVisibleFields(visible);
}

void SBPMetadataWindow::onSearchTextChanged(const QString& text)
{
    const QString lc = text.toLower();
    for (int i = 0; i < m_field_list->count(); ++i) {
        auto* item = m_field_list->item(i);
        item->setHidden(!lc.isEmpty() && !item->text().toLower().contains(lc));
    }
}

void SBPMetadataWindow::onFieldListCurrentChanged(int row)
{
    if (row < 0) { m_selected_field = -1; m_cfg_panel->setEnabled(false); return; }
    const int fi = m_field_list->item(row)->data(Qt::UserRole).toInt();
    m_selected_field = fi;
    m_cfg_panel->setEnabled(true);
    updateFieldConfig(fi);
}

void SBPMetadataWindow::updateFieldConfig(int fi)
{
    m_updating_cfg = true;
    m_cfg_name->setText(QString::fromUtf8(SBPNavModel::kFieldDefs[fi].name));
    m_cfg_plot_cb->setChecked(m_field_cfg[fi].show_plot);
    m_cfg_color_btn->setStyleSheet(colorSwatchSheet(m_field_cfg[fi].color));
    m_cfg_thick_sp->setValue(m_field_cfg[fi].thickness);
    m_cfg_dots_cb->setChecked(m_field_cfg[fi].add_dots);
    const int prec = (m_field_cfg[fi].precision >= 0)
                     ? m_field_cfg[fi].precision
                     : SBPNavModel::kFieldDefs[fi].default_prec;
    m_cfg_prec_sp->setValue(std::max(0, prec));
    m_updating_cfg = false;
}

void SBPMetadataWindow::saveFieldConfig(int fi)
{
    m_model->setFieldPrecision(fi, m_field_cfg[fi].precision);
    for (int c = 0; c < m_model->columnCount(); ++c) {
        if (m_model->headerData(c, Qt::Horizontal, Qt::UserRole).toInt() == fi) {
            emit m_model->dataChanged(m_model->index(0, c),
                                      m_model->index(m_model->rowCount() - 1, c));
            break;
        }
    }
    updatePlot();
    updateChart();
}

void SBPMetadataWindow::onShowInPlotToggled(bool checked)
{
    if (m_updating_cfg || m_selected_field < 0) return;
    m_field_cfg[m_selected_field].show_plot = checked;
    updatePlot();
}

void SBPMetadataWindow::onColorButtonClicked()
{
    if (m_selected_field < 0) return;
    QColor c = QColorDialog::getColor(m_field_cfg[m_selected_field].color, this, "Field Color");
    if (!c.isValid()) return;
    m_field_cfg[m_selected_field].color = c;
    m_cfg_color_btn->setStyleSheet(colorSwatchSheet(c));
    updatePlot();
    updateChart();
}

void SBPMetadataWindow::onThicknessChanged(int v)
{
    if (m_updating_cfg || m_selected_field < 0) return;
    m_field_cfg[m_selected_field].thickness = v;
    updatePlot();
}

void SBPMetadataWindow::onDotsToggled(bool checked)
{
    if (m_updating_cfg || m_selected_field < 0) return;
    m_field_cfg[m_selected_field].add_dots = checked;
    updatePlot();
}

void SBPMetadataWindow::onPrecisionChanged(int v)
{
    if (m_updating_cfg || m_selected_field < 0) return;
    m_field_cfg[m_selected_field].precision = v;
    saveFieldConfig(m_selected_field);
}

// -----------------------------------------------------------------------------
//  Selection status bar
// -----------------------------------------------------------------------------
void SBPMetadataWindow::onSelectionChanged()
{
    const auto indices = m_table->selectionModel()->selectedIndexes();
    if (indices.isEmpty()) { m_sel_status->clear(); return; }

    double sum = 0.0;
    double mn  =  std::numeric_limits<double>::max();
    double mx  = -std::numeric_limits<double>::max();
    int numeric = 0;

    for (const auto& idx : indices) {
        const double v = idx.data(Qt::UserRole + 1).toDouble();
        bool ok;
        idx.data(Qt::DisplayRole).toString().toDouble(&ok);
        if (ok) {
            sum += v;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
            ++numeric;
        }
    }

    QString msg = QString("Selected: %1 cells").arg(indices.size());
    if (numeric > 0) {
        const double avg = sum / numeric;
        msg += QString("   |   Min: %1   Max: %2   Avg: %3   Sum: %4")
                   .arg(mn,  0, 'g', 5)
                   .arg(mx,  0, 'g', 5)
                   .arg(avg, 0, 'g', 5)
                   .arg(sum, 0, 'g', 6);
    }
    m_sel_status->setText(msg);
}

} // namespace dolphin::ui
