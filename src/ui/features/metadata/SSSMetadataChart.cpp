// SSSMetadataChart.cpp — chart toolbar construction, toggle/dock, chart update slots.
#include "ui/features/metadata/SSSMetadataWindow.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QEvent>
#include <QSplitter>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>

namespace dolphin::ui {

static constexpr int kBinsSpinW = 52;  // histogram bins spinbox width

void SSSMetadataWindow::buildChartToolbar(QWidget* parent)
{
    auto* bar = new QWidget(parent);
    bar->setFixedHeight(Theme::kDialogBtnH);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(Theme::kSpacing2, 2, Theme::kSpacing2, 2);
    bl->setSpacing(Theme::kSpacing2);

    bl->addWidget(new QLabel("Chart:", bar));
    m_chart_type_cb = new QComboBox(bar);
    m_chart_type_cb->addItems({"Line", "Scatter", "Histogram"});
    bl->addWidget(m_chart_type_cb);

    bl->addWidget(new QLabel("|", bar));

    m_chart_x_lbl = new QLabel("X:", bar);
    bl->addWidget(m_chart_x_lbl);
    m_chart_x_cb = new QComboBox(bar);
    m_chart_x_cb->setMinimumWidth(100);
    bl->addWidget(m_chart_x_cb);

    bl->addWidget(new QLabel("Y:", bar));
    m_chart_y_cb = new QComboBox(bar);
    m_chart_y_cb->setMinimumWidth(100);
    bl->addWidget(m_chart_y_cb);

    m_chart_bins_lbl = new QLabel("Bins:", bar);
    bl->addWidget(m_chart_bins_lbl);
    m_chart_bins_sp = new QSpinBox(bar);
    m_chart_bins_sp->setRange(5, 200);
    m_chart_bins_sp->setValue(30);
    m_chart_bins_sp->setFixedWidth(kBinsSpinW);
    bl->addWidget(m_chart_bins_sp);

    m_btn_undock_chart = new QToolButton(bar);
    m_btn_undock_chart->setToolTip("Pop out chart  /  Dock back");
    m_btn_undock_chart->setFixedSize(Theme::kSmallBtnSz, Theme::kSmallBtnSz);
    m_btn_undock_chart->setText("⬜");
    m_btn_undock_chart->setObjectName("metaUndockBtn");
    connect(m_btn_undock_chart, &QToolButton::clicked,
            this, &SSSMetadataWindow::onUndockChart);
    bl->addWidget(m_btn_undock_chart);
    bl->addStretch(1);

    for (int i = 0; i < SSSNavModel::kFieldCount; ++i) {
        QString name = SSSNavModel::kFieldDefs[i].name;
        m_chart_x_cb->addItem(name, i);
        m_chart_y_cb->addItem(name, i);
    }
    m_chart_x_cb->setCurrentIndex(0);
    m_chart_y_cb->setCurrentIndex(15);   // Heading

    parent->layout()->addWidget(bar);

    connect(m_chart_type_cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SSSMetadataWindow::onChartTypeChanged);
    connect(m_chart_x_cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SSSMetadataWindow::onChartXChanged);
    connect(m_chart_y_cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SSSMetadataWindow::onChartYChanged);
    connect(m_chart_bins_sp, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SSSMetadataWindow::onChartBinsChanged);

    onChartTypeChanged(0);
}

void SSSMetadataWindow::onToggleChart(bool show)
{
    if (!m_chart_pane) return;
    if (m_chart_floating) {
        if (show) m_chart_pane->show();
        else      m_chart_pane->hide();
        return;
    }
    m_chart_pane->setVisible(show);
}

void SSSMetadataWindow::onUndockChart()
{
    if (!m_chart_pane || !m_outer_vsplit) return;

    if (!m_chart_floating) {
        const QSize sz = m_chart_pane->size().expandedTo(QSize(600, 220));
        m_chart_pane->setParent(nullptr);
        m_chart_pane->setWindowTitle("Chart — SSS Survey Data");
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

bool SSSMetadataWindow::eventFilter(QObject* obj, QEvent* ev)
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

void SSSMetadataWindow::onChartTypeChanged(int idx)
{
    const bool is_scatter   = (idx == 1);
    const bool is_histogram = (idx == 2);
    const bool is_line      = (idx == 0);

    m_chart_x_lbl->setVisible(is_scatter);
    m_chart_x_cb ->setVisible(is_scatter);
    m_chart_bins_lbl->setVisible(is_histogram);
    m_chart_bins_sp ->setVisible(is_histogram);
    Q_UNUSED(is_line);

    updateChart();
}

void SSSMetadataWindow::onChartXChanged(int) { updateChart(); }
void SSSMetadataWindow::onChartYChanged(int) { updateChart(); }
void SSSMetadataWindow::onChartBinsChanged(int) { updateChart(); }

void SSSMetadataWindow::updatePlot()
{
    QVector<SSSPlotSeries> series;
    for (int fi = 0; fi < SSSNavModel::kFieldCount; ++fi) {
        if (!m_field_cfg[fi].show_plot) continue;
        SSSPlotSeries s;
        s.label     = QString::fromUtf8(SSSNavModel::kFieldDefs[fi].name);
        s.color     = m_field_cfg[fi].color;
        s.thickness = m_field_cfg[fi].thickness;
        s.add_dots  = m_field_cfg[fi].add_dots;
        s.values    = m_model->fieldValues(fi);
        series.append(s);
    }
    if (m_chart_type_cb && m_chart_type_cb->currentIndex() == 0)
        m_plot->setLineSeries(series);
}

void SSSMetadataWindow::updateChart()
{
    if (!m_chart_type_cb || !m_plot) return;
    const int type = m_chart_type_cb->currentIndex();

    if (type == 0) {
        updatePlot();
        return;
    }

    const int fi_y = m_chart_y_cb->currentData().toInt();

    if (type == 1) {
        const int fi_x = m_chart_x_cb->currentData().toInt();
        m_plot->setScatterData(
            m_model->fieldValues(fi_x),
            m_model->fieldValues(fi_y),
            QString::fromUtf8(SSSNavModel::kFieldDefs[fi_x].name),
            QString::fromUtf8(SSSNavModel::kFieldDefs[fi_y].name),
            m_field_cfg[fi_y].color);
    } else {
        m_plot->setHistogramData(
            m_model->fieldValues(fi_y),
            m_chart_bins_sp->value(),
            QString::fromUtf8(SSSNavModel::kFieldDefs[fi_y].name),
            m_field_cfg[fi_y].color);
    }
}

} // namespace dolphin::ui
