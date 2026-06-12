// NodeGraphWindow.Layout.cpp — constructor, top bar, worker tabs, palette, placement arm.
#include "ui/features/nodegraph/NodeGraphWindow.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"
#include "ui/features/nodegraph/NodePaletteView.h"
#include "ui/features/nodegraph/NodeGraphView.h"
#include "ui/features/nodegraph/NodeInspectorPanel.h"
#include "pipeline/NodeRegistry.h"
#include "app/project/Project.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kPaletteW = 192;  // node palette sidebar width
static constexpr int kMinW     = 900;
static constexpr int kMinH     = 560;
static constexpr int kInitW    = 1280;
static constexpr int kInitH    = 720;

NodeGraphWindow::NodeGraphWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Node Graph"));
    setMinimumSize(kMinW, kMinH);
    resize(kInitW, kInitH);
    setObjectName("nodeGraphWindow");

    auto* root = makeCompactLayout<QVBoxLayout>(this);

    root->addWidget(buildTopBar(this));

    auto* hdiv = new QFrame(this);
    hdiv->setObjectName("nodeDivider");
    hdiv->setFixedHeight(Theme::kSepSz);
    root->addWidget(hdiv);

    root->addWidget(buildWorkerTabBar(this));

    auto* tab_div = new QFrame(this);
    tab_div->setObjectName("nodeDivider");
    tab_div->setFixedHeight(Theme::kSepSz);
    root->addWidget(tab_div);

    auto* body = new QWidget(this);
    auto* body_layout = makeCompactLayout<QHBoxLayout>(body);
    root->addWidget(body, 1);

    auto* palette_w = new QWidget(body);
    palette_w->setFixedWidth(kPaletteW);
    palette_w->setObjectName("nodeGraphPalette");
    buildPalette(palette_w);
    body_layout->addWidget(palette_w);

    auto* div1 = new QFrame(body);
    div1->setObjectName("nodeDivider");
    div1->setFixedWidth(Theme::kSepSz);
    body_layout->addWidget(div1);

    m_canvas = new NodeGraphView(body);
    body_layout->addWidget(m_canvas, 1);

    connect(m_canvas, &NodeGraphView::nodeSelected,
            this, &NodeGraphWindow::onNodeSelected);
    connect(m_canvas, &NodeGraphView::graphModified,
            this, &NodeGraphWindow::onGraphModified);

    auto* div2 = new QFrame(body);
    div2->setObjectName("nodeDivider");
    div2->setFixedWidth(Theme::kSepSz);
    body_layout->addWidget(div2);

    m_inspector = new NodeInspectorPanel(body);
    m_inspector->setFixedWidth(Theme::kPropertiesPanelW);
    m_inspector->setObjectName("nodeInspector");
    body_layout->addWidget(m_inspector);

    connect(m_inspector, &NodeInspectorPanel::paramChanged,
            this, &NodeGraphWindow::onGraphModified);
    connect(m_inspector, &NodeInspectorPanel::importRequested,
            this, &NodeGraphWindow::importRequested);
}

QWidget* NodeGraphWindow::buildTopBar(QWidget* parent)
{
    auto* bar = new QFrame(parent);
    bar->setObjectName("nodeGraphTopBar");
    bar->setFixedHeight(Theme::kPanelHdrH);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(Theme::kSpacing3);

    auto* layer_label = new QLabel(tr("Layer"), bar);
    layer_label->setObjectName("nodeLayerLabel");
    layout->addWidget(layer_label);

    m_layer_combo = new QComboBox(bar);
    m_layer_combo->setMinimumWidth(200);
    m_layer_combo->setMaximumWidth(320);
    m_layer_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_layer_combo->addItem(tr("No project"));
    m_layer_combo->setEnabled(false);
    connect(m_layer_combo, &QComboBox::currentIndexChanged,
            this, &NodeGraphWindow::onLayerComboChanged);
    layout->addWidget(m_layer_combo);

    layout->addStretch();

    auto* zoom_out = new QToolButton(bar);
    zoom_out->setText("−");
    zoom_out->setFixedWidth(Theme::kMediumBtnSz);
    zoom_out->setToolTip(tr("Zoom out  (Ctrl+−)"));
    connect(zoom_out, &QToolButton::clicked, [this]() {
        if (m_canvas) m_canvas->zoomBy(1.0/1.25, QPointF(m_canvas->width()/2.0, m_canvas->height()/2.0));
    });
    layout->addWidget(zoom_out);

    auto* zoom_in = new QToolButton(bar);
    zoom_in->setText("+");
    zoom_in->setFixedWidth(Theme::kMediumBtnSz);
    zoom_in->setToolTip(tr("Zoom in  (Ctrl++)"));
    connect(zoom_in, &QToolButton::clicked, [this]() {
        if (m_canvas) m_canvas->zoomBy(1.25, QPointF(m_canvas->width()/2.0, m_canvas->height()/2.0));
    });
    layout->addWidget(zoom_in);

    auto* frame_btn = new QToolButton(bar);
    frame_btn->setText(tr("Frame"));
    frame_btn->setToolTip(tr("Fit all nodes in view  (F)"));
    connect(frame_btn, &QToolButton::clicked, [this]() {
        if (m_canvas) m_canvas->frameAll();
    });
    layout->addWidget(frame_btn);

    auto* layout_btn = new QToolButton(bar);
    layout_btn->setText(tr("Auto Layout"));
    layout_btn->setToolTip(tr("Arrange nodes automatically"));
    connect(layout_btn, &QToolButton::clicked, [this]() {
        if (m_canvas) m_canvas->autoLayout();
    });
    layout->addWidget(layout_btn);

    auto* sep = new QFrame(bar);
    sep->setObjectName("nodeDivider");
    sep->setFixedWidth(Theme::kSepSz);
    layout->addWidget(sep);

    m_run_btn = new QToolButton(bar);
    m_run_btn->setText(tr("Run"));
    m_run_btn->setObjectName("runBtn");
    m_run_btn->setEnabled(false);
    m_run_btn->setToolTip(tr("Execute this graph on the selected layer"));
    connect(m_run_btn, &QToolButton::clicked, this, &NodeGraphWindow::onRun);
    layout->addWidget(m_run_btn);

    return bar;
}

QWidget* NodeGraphWindow::buildWorkerTabBar(QWidget* parent)
{
    auto* wrap = new QWidget(parent);
    wrap->setObjectName("workerTabBar");
    wrap->setFixedHeight(Theme::kDialogBtnH);

    auto* layout = new QHBoxLayout(wrap);
    layout->setContentsMargins(Theme::kSpacing3, 0, Theme::kSpacing3, 0);
    layout->setSpacing(0);

    m_worker_tabs = new QTabBar(wrap);
    m_worker_tabs->setExpanding(false);
    m_worker_tabs->setDrawBase(false);
    connect(m_worker_tabs, &QTabBar::currentChanged,
            this, &NodeGraphWindow::onWorkerTabChanged);
    layout->addWidget(m_worker_tabs);
    layout->addStretch();

    return wrap;
}

void NodeGraphWindow::rebuildWorkerTabs()
{
    if (!m_worker_tabs) return;

    {
        QSignalBlocker blocker(m_worker_tabs);
        while (m_worker_tabs->count() > 0)
            m_worker_tabs->removeTab(0);

        if (m_project) {
            if (m_project->workers().empty()) {
                m_worker_tabs->addTab(tr("Pipeline"));
            } else {
                for (const auto& w : m_project->workers())
                    m_worker_tabs->addTab(QString::fromStdString(w.label));
            }
        }
    }

    if (!m_project || m_worker_tabs->count() == 0) {
        m_graph = nullptr;
        return;
    }

    m_active_worker_tab = std::clamp(m_active_worker_tab, 0, m_worker_tabs->count() - 1);
    m_worker_tabs->setCurrentIndex(m_active_worker_tab);

    auto& workers = m_project->workers();
    m_graph = workers.empty()
        ? &m_project->processing_graph
        : &workers[m_active_worker_tab].graph;
}

void NodeGraphWindow::buildPalette(QWidget* parent)
{
    auto* layout = makeCompactLayout<QVBoxLayout>(parent);

    auto* search = new QLineEdit(parent);
    search->setPlaceholderText(tr("Search nodes..."));
    search->setClearButtonEnabled(true);
    search->setObjectName("nodePaletteSearch");
    search->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2, Theme::kSpacing3, Theme::kSpacing2);
    layout->addWidget(search);

    auto* palette_scroll = new QScrollArea(parent);
    palette_scroll->setFrameShape(QFrame::NoFrame);
    palette_scroll->setWidgetResizable(true);
    palette_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    palette_scroll->setObjectName("nodePaletteScroll");

    m_palette = new NodePaletteView(palette_scroll);
    m_palette->setObjectName("nodePaletteList");
    palette_scroll->setWidget(m_palette);

    const auto& registry = pipeline::NodeRegistry::instance();
    QMap<QString, QVector<NodePaletteView::NodeItem>> by_category;
    QVector<QString> category_order;

    for (const auto& type_id : registry.allTypeIds()) {
        auto proto = registry.create(type_id);
        if (!proto) continue;
        const pipeline::NodeSchema schema = proto->schema();
        const QString cat   = QString::fromStdString(schema.category);
        const QString label = QString::fromStdString(schema.label);
        if (!by_category.contains(cat))
            category_order.append(cat);
        by_category[cat].append({cat, label, QString::fromStdString(type_id)});
    }

    QVector<NodePaletteView::NodeItem> palette_nodes;
    for (const auto& cat : category_order)
        for (const auto& node : by_category[cat])
            palette_nodes.append(node);

    m_palette->setNodes(palette_nodes);

    connect(search, &QLineEdit::textChanged, this, &NodeGraphWindow::filterPalette);
    connect(m_palette, &NodePaletteView::nodePicked,
            this, &NodeGraphWindow::armPalettePlacement);

    connect(m_palette, &NodePaletteView::nodeActivated, [this](const QString& type_id) {
        if (!ensurePlacementTarget()) return;
        cancelPaletteDrag();
        if (!type_id.isEmpty())
            m_canvas->addNode(type_id.toStdString());
    });

    connect(m_palette, &NodePaletteView::nodeDragStarted,
            this, [this](const QString& type_id, const QPoint& global_pos) {
                beginPaletteDrag(type_id, global_pos);
            });

    layout->addWidget(palette_scroll, 1);
}

void NodeGraphWindow::filterPalette(const QString& text)
{
    if (m_palette)
        m_palette->setFilterText(text);
}

bool NodeGraphWindow::ensurePlacementTarget()
{
    return m_graph != nullptr && m_canvas != nullptr;
}

void NodeGraphWindow::armPalettePlacement(const QString& type_id)
{
    if (type_id.isEmpty()) return;

    if (!ensurePlacementTarget()) {
        if (m_inspector) {
            const bool has_project = m_project != nullptr;
            m_inspector->showStatus(
                has_project ? tr("Select a graph target first") : tr("No project available"),
                has_project
                    ? tr("Choose Project Pipeline or a layer from the top bar before placing nodes.")
                    : tr("Create or open a project first."));
        }
        return;
    }

    QString label = type_id;
    if (auto proto = pipeline::NodeRegistry::instance().create(type_id.toStdString()))
        label = QString::fromStdString(proto->label());

    cancelPaletteDrag();
    m_palette_drag_type_id = type_id;
    m_canvas->setPlacementPreview(type_id.toStdString(),
                                  QPointF(m_canvas->rect().center()));
    if (m_inspector) {
        m_inspector->showStatus(tr("Placing %1").arg(label),
                                tr("Click in the graph to place it, or drag it into position."));
    }
    m_canvas->setFocus();
}

} // namespace dolphin::ui
