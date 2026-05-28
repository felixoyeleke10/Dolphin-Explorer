// NodeGraphWindow.cpp - host window for the visual node graph editor.
#include "ui/views/nodegraph/NodeGraphWindow.h"
#include "ui/views/nodegraph/NodePaletteView.h"
#include "ui/views/nodegraph/NodeGraphView.h"
#include "ui/panels/NodeInspectorPanel.h"
#include "pipeline/NodeRegistry.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMouseEvent>
#include <QPoint>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include "app/workers/Worker.h"

namespace dolphin::ui {

NodeGraphWindow::NodeGraphWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Node Graph"));
    setMinimumSize(900, 560);
    resize(1280, 720);
    setObjectName("nodeGraphWindow");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(buildTopBar(this));

    auto* hdiv = new QFrame(this);
    hdiv->setObjectName("nodeDivider");
    hdiv->setFixedHeight(1);
    root->addWidget(hdiv);

    root->addWidget(buildWorkerTabBar(this));

    auto* tab_div = new QFrame(this);
    tab_div->setObjectName("nodeDivider");
    tab_div->setFixedHeight(1);
    root->addWidget(tab_div);

    auto* body = new QWidget(this);
    auto* body_layout = new QHBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(0);
    root->addWidget(body, 1);

    auto* palette_w = new QWidget(body);
    palette_w->setFixedWidth(192);
    palette_w->setObjectName("nodeGraphPalette");
    buildPalette(palette_w);
    body_layout->addWidget(palette_w);

    auto* div1 = new QFrame(body);
    div1->setObjectName("nodeDivider");
    div1->setFixedWidth(1);
    body_layout->addWidget(div1);

    m_canvas = new NodeGraphView(body);
    body_layout->addWidget(m_canvas, 1);

    connect(m_canvas, &NodeGraphView::nodeSelected,
            this, &NodeGraphWindow::onNodeSelected);
    connect(m_canvas, &NodeGraphView::graphModified,
            this, &NodeGraphWindow::onGraphModified);

    auto* div2 = new QFrame(body);
    div2->setObjectName("nodeDivider");
    div2->setFixedWidth(1);
    body_layout->addWidget(div2);

    m_inspector = new NodeInspectorPanel(body);
    m_inspector->setFixedWidth(240);
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
    bar->setFixedHeight(38);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(8);

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
    zoom_out->setFixedWidth(28);
    zoom_out->setToolTip(tr("Zoom out  (Ctrl+−)"));
    connect(zoom_out, &QToolButton::clicked, [this]() {
        if (m_canvas) m_canvas->zoomBy(1.0/1.25, QPointF(m_canvas->width()/2.0, m_canvas->height()/2.0));
    });
    layout->addWidget(zoom_out);

    auto* zoom_in = new QToolButton(bar);
    zoom_in->setText("+");
    zoom_in->setFixedWidth(28);
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
    sep->setFixedWidth(1);
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
    wrap->setFixedHeight(32);
    wrap->setStyleSheet(
        "QWidget#workerTabBar { background: #161618; }"
        "QTabBar { background: transparent; }"
        "QTabBar::tab {"
        "  background: transparent;"
        "  color: #636366;"
        "  padding: 0 16px;"
        "  height: 31px;"
        "  border: none;"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "}"
        "QTabBar::tab:selected { color: #f2f2f7; border-bottom: 2px solid #0a84ff; }"
        "QTabBar::tab:hover:!selected { color: #8e8e93; }");

    auto* layout = new QHBoxLayout(wrap);
    layout->setContentsMargins(8, 0, 8, 0);
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
                // No user-defined workers — show the project pipeline as a single tab.
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
    auto* layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* search = new QLineEdit(parent);
    search->setPlaceholderText(tr("Search nodes..."));
    search->setClearButtonEnabled(true);
    search->setObjectName("nodePaletteSearch");
    search->setContentsMargins(8, 6, 8, 6);
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
        if (!proto)
            continue;
        const pipeline::NodeSchema schema = proto->schema();
        const QString cat = QString::fromStdString(schema.category);
        const QString label = QString::fromStdString(schema.label);
        if (!by_category.contains(cat))
            category_order.append(cat);
        by_category[cat].append({cat, label, QString::fromStdString(type_id)});
    }

    QVector<NodePaletteView::NodeItem> palette_nodes;
    for (const auto& cat : category_order) {
        for (const auto& node : by_category[cat])
            palette_nodes.append(node);
    }

    m_palette->setNodes(palette_nodes);

    connect(search, &QLineEdit::textChanged, this, &NodeGraphWindow::filterPalette);
    connect(m_palette, &NodePaletteView::nodePicked,
            this, &NodeGraphWindow::armPalettePlacement);

    connect(m_palette, &NodePaletteView::nodeActivated, [this](const QString& type_id) {
        if (!ensurePlacementTarget())
            return;

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
    if (type_id.isEmpty())
        return;

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

void NodeGraphWindow::beginPaletteDrag(const QString& type_id,
                                       const QPoint& global_pos)
{
    if (type_id.isEmpty())
        return;

    if (!ensurePlacementTarget()) {
        if (m_inspector) {
            const bool has_project = m_project != nullptr;
            m_inspector->showStatus(
                has_project ? tr("Select a graph target first") : tr("No project available"),
                has_project
                    ? tr("Choose Project Pipeline or a layer from the top bar before dragging nodes into the graph.")
                    : tr("Create or open a project first."));
        }
        return;
    }

    QString label = type_id;
    if (auto proto = pipeline::NodeRegistry::instance().create(type_id.toStdString()))
        label = QString::fromStdString(proto->label());

    cancelPaletteDrag();
    m_palette_drag_type_id = type_id;
    m_palette_drag_live = true;
    qApp->installEventFilter(this);
    if (m_inspector) {
        m_inspector->showStatus(tr("Dragging %1").arg(label),
                                tr("Move over the graph and release to insert the node."));
    }
    updatePaletteDrag(global_pos);
}

void NodeGraphWindow::cancelPaletteDrag()
{
    if (m_palette_drag_live)
        qApp->removeEventFilter(this);

    m_palette_drag_live = false;
    m_palette_drag_type_id.clear();
    m_palette_drop_valid = false;
    m_palette_drop_pos = QPoint{};

    unsetCursor();
    if (m_canvas)
        m_canvas->clearPlacementPreview();
    if (m_palette)
        m_palette->resetInteractionState();
}

void NodeGraphWindow::updatePaletteDrag(const QPoint& global_pos)
{
    if (!m_palette_drag_live || m_palette_drag_type_id.isEmpty() || !m_canvas)
        return;

    const QPoint local_pos = m_canvas->mapFromGlobal(global_pos);
    if (!m_canvas->rect().contains(local_pos)) {
        m_palette_drop_valid = false;
        m_canvas->clearPlacementPreview();
        return;
    }

    setCursor(Qt::CrossCursor);
    m_palette_drop_valid = true;
    m_palette_drop_pos = local_pos;
    m_canvas->setPlacementPreview(m_palette_drag_type_id.toStdString(),
                                  QPointF(local_pos));
    m_canvas->setFocus();
}

void NodeGraphWindow::finishPaletteDrag(bool commit, const QPoint& global_pos)
{
    Q_UNUSED(global_pos);

    const QString type_id = m_palette_drag_type_id;
    const bool can_commit = commit
        && !type_id.isEmpty()
        && m_canvas
        && m_palette_drop_valid;

    const QPoint drop_pos = m_palette_drop_pos;

    cancelPaletteDrag();

    if (!can_commit)
        return;

    m_canvas->addNodeAtWidget(type_id.toStdString(), QPointF(drop_pos));
    m_canvas->setFocus();
}

bool NodeGraphWindow::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    if (!m_palette_drag_live)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseMove: {
        auto* mouse_ev = static_cast<QMouseEvent*>(event);
        updatePaletteDrag(mouse_ev->globalPosition().toPoint());
        return false;
    }

    case QEvent::MouseButtonRelease: {
        auto* mouse_ev = static_cast<QMouseEvent*>(event);
        finishPaletteDrag(mouse_ev->button() == Qt::LeftButton,
                          mouse_ev->globalPosition().toPoint());
        return true;
    }

    case QEvent::KeyPress: {
        auto* key_ev = static_cast<QKeyEvent*>(event);
        if (key_ev->key() != Qt::Key_Escape)
            return false;
        cancelPaletteDrag();
        return true;
    }

    default:
        return QWidget::eventFilter(watched, event);
    }
}

void NodeGraphWindow::setLayer(dolphin::app::DataLayer* layer,
                               dolphin::app::Project* project)
{
    cancelPaletteDrag();
    m_project = project;
    m_layer   = layer;
    m_graph   = nullptr;

    // ── Layer combo (run target) ─────────────────────────────────────────────
    if (m_layer_combo) {
        QSignalBlocker blocker(m_layer_combo);
        m_layer_combo->clear();

        if (project) {
            m_layer_combo->addItem(tr("(no layer selected)"), QString{});
            for (const auto& candidate : project->layers()) {
                const QString lbl = candidate->label.empty()
                    ? QString::fromStdString(candidate->id)
                    : QString::fromStdString(candidate->label);
                m_layer_combo->addItem(lbl, QString::fromStdString(candidate->id));
            }
            m_layer_combo->setEnabled(true);

            if (layer) {
                const int idx = m_layer_combo->findData(QString::fromStdString(layer->id));
                if (idx >= 0) m_layer_combo->setCurrentIndex(idx);
            } else {
                m_layer_combo->setCurrentIndex(0);
            }
        } else {
            m_layer_combo->addItem(tr("No project"));
            m_layer_combo->setEnabled(false);
        }
    }

    // ── Worker tabs ──────────────────────────────────────────────────────────
    rebuildWorkerTabs();

    // ── No project ───────────────────────────────────────────────────────────
    if (!project || !m_graph) {
        m_run_btn->setEnabled(false);
        if (m_palette) {
            m_palette->setPlacementEnabled(false);
            m_palette->setToolTip(tr("Create or open a project first"));
        }
        if (m_canvas)  m_canvas->setGraph(nullptr);
        if (m_inspector) {
            m_inspector->showStatus(
                tr("No project"),
                tr("Create or open a project to build a processing graph."));
        }
        return;
    }

    // ── Ready ────────────────────────────────────────────────────────────────
    m_run_btn->setEnabled(layer != nullptr);
    if (m_palette) {
        m_palette->setPlacementEnabled(true);
        m_palette->setToolTip(tr("Drag into the graph or double-click to add"));
    }
    if (m_canvas)
        m_canvas->setGraph(m_graph);

    if (m_inspector)
        m_inspector->showStatus(tr("Graph ready"),
            tr("Drag nodes from the palette or right-click to add."));
}

void NodeGraphWindow::onLayerComboChanged(int index)
{
    if (!m_layer_combo || index < 0 || !m_project) return;

    const QString layer_id = m_layer_combo->itemData(index).toString();
    m_layer = layer_id.isEmpty() ? nullptr : m_project->findLayer(layer_id.toStdString());
    m_run_btn->setEnabled(m_layer != nullptr);

    if (m_layer)
        emit layerSelectionRequested(m_layer->id);
}

void NodeGraphWindow::onWorkerTabChanged(int index)
{
    if (!m_project || index < 0) return;

    m_active_worker_tab = index;
    auto& workers = m_project->workers();
    m_graph = workers.empty()
        ? &m_project->processing_graph
        : (index < (int)workers.size() ? &workers[index].graph : nullptr);

    if (m_canvas)    m_canvas->setGraph(m_graph);
    if (m_inspector) m_inspector->clearNode();
}

void NodeGraphWindow::onNodeSelected(const std::string& node_id)
{
    if (!m_graph || node_id.empty()) {
        m_inspector->clearNode();
        return;
    }
    m_inspector->showNode(m_graph, node_id);
}

void NodeGraphWindow::onGraphModified()
{
    if (m_canvas) m_canvas->update();
    emit graphModified();
}

void NodeGraphWindow::onRun()
{
    emit runRequested();
}

void NodeGraphWindow::closeEvent(QCloseEvent* ev)
{
    cancelPaletteDrag();
    hide();
    ev->ignore();
}

} // namespace dolphin::ui
