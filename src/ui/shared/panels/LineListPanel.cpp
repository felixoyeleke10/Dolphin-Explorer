// LineListPanel.cpp — widget setup, event handling, and filter logic.
// Tree construction lives in LineListPanel.Tree.cpp.
// Context menu and reorder logic lives in LineListPanel.ContextMenu.cpp.
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/panels/LineListPanel_p.h"
#include "ui/shared/widgets/EmptyStateWidget.h"
#include "app/layers/DataLayer.h"
#include "ui/shell/Theme.h"
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

using namespace dolphin::ui::detail;

namespace dolphin::ui {

namespace {

class LayerTreeWidget final : public QTreeWidget {
public:
    explicit LayerTreeWidget(QWidget* parent = nullptr) : QTreeWidget(parent) {}
protected:
    void startDrag(Qt::DropActions actions) override {
        m_drag_item = currentItem();
        QTreeWidget::startDrag(actions);
        m_drag_item = nullptr;
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        if (canAcceptDrop(event->position().toPoint())) QTreeWidget::dragMoveEvent(event);
        else event->ignore();
    }
    void dropEvent(QDropEvent* event) override {
        if (canAcceptDrop(event->position().toPoint())) QTreeWidget::dropEvent(event);
        else event->ignore();
    }
private:
    bool canAcceptDrop(const QPoint& pos) const {
        if (!m_drag_item) return false;
        if (itemTypeOf(m_drag_item) != ItemType::Layer) return false;
        auto* src_parent = m_drag_item->parent();
        if (!src_parent) return false;
        const auto pt = itemTypeOf(src_parent);
        if (pt != ItemType::Modality && pt != ItemType::LayerGroup) return false;
        auto* target = itemAt(pos);
        if (!target) return false;
        const auto tt = itemTypeOf(target);
        if (tt == ItemType::Layer)
            return target->parent() == src_parent;
        if (tt == ItemType::Modality || tt == ItemType::LayerGroup)
            return target == src_parent;
        return false;
    }
    QTreeWidgetItem* m_drag_item = nullptr;
};

} // namespace

LineListPanel::LineListPanel(QWidget* parent, ContentMode mode)
    : QWidget(parent)
    , m_mode(mode)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing2);
    layout->setSpacing(Theme::kSpacing2);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(
        m_mode == ContentMode::LayersOnly
            ? tr("Search layers, contacts...")
            : tr("Search..."));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_tree = new LayerTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setAnimated(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setUniformRowHeights(false);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setDragEnabled(true);
    m_tree->viewport()->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree, 1);

    // -- Empty state (shown when no project is open) ---------------------------
    auto* es = new EmptyStateWidget(this);
    m_empty_state = es;
    connect(es->addAction(tr("New Project…")),  &QPushButton::clicked,
            this, &LineListPanel::newProjectRequested);
    connect(es->addAction(tr("Import Files…")), &QPushButton::clicked,
            this, &LineListPanel::importFilesRequested);

    layout->addWidget(m_empty_state);
    // Initial state: no project open — show empty state, hide tree.
    m_tree->hide();

    connect(m_tree, &QTreeWidget::itemClicked,
            this, &LineListPanel::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &LineListPanel::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged,
            this, &LineListPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &LineListPanel::onSelectionChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &LineListPanel::onContextMenuRequested);
    connect(m_search, &QLineEdit::textChanged,
            this, &LineListPanel::applyFilter);
    connect(m_tree->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex&, int, int, const QModelIndex&, int) {
                syncLayerOrderFromTree();
            });

    m_tree->installEventFilter(this);
}

void LineListPanel::setProject(app::Project* project)
{
    m_project = project;
    refresh();
}

void LineListPanel::refresh()
{
    const bool has_project = (m_project != nullptr);
    m_empty_state->setVisible(!has_project);
    m_tree->setVisible(has_project);

    if (!has_project) return;

    m_rebuilding = true;
    {
        QSignalBlocker sb(m_tree);
        m_tree->clear();
        buildTree();
        m_tree->expandAll();
    }
    m_rebuilding = false;
    applyFilter(m_search->text());
    if (!m_active_layer_id.empty())
        setActiveLayer(m_active_layer_id);
}

void LineListPanel::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_rebuilding) return;
    if (column != 0 || !item) return;
    if (itemTypeOf(item) != ItemType::Layer) return;
    const std::string layer_id = item->data(0, kRoleId).toString().toStdString();
    const bool visible = (item->checkState(0) == Qt::Checked);
    // Do NOT set layer->visible here — onLayerVisibilityChanged reads the old state
    // from the layer to build the undo command. Mutating first would make old == new.
    emit layerVisibilityChanged(layer_id, visible);
}

void LineListPanel::setActiveLayer(const std::string& layer_id)
{
    m_active_layer_id = layer_id;
    const QString qid = QString::fromStdString(layer_id);
    QSignalBlocker sb(m_tree);
    m_tree->clearSelection();
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if (itemTypeOf(*it) == ItemType::Layer &&
            (*it)->data(0, kRoleId).toString() == qid) {
            (*it)->setSelected(true);
            m_tree->setCurrentItem(*it, 0, QItemSelectionModel::NoUpdate);
            return;
        }
        ++it;
    }
}

void LineListPanel::setLayerVisibility(const std::string& id, bool visible)
{
    const QString qid = QString::fromStdString(id);
    QSignalBlocker sb(m_tree);
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if (itemTypeOf(*it) == ItemType::Layer &&
            (*it)->data(0, kRoleId).toString() == qid) {
            (*it)->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
            return;
        }
        ++it;
    }
}

void LineListPanel::updateLayerLabel(const std::string& id, const std::string& label)
{
    const QString qid = QString::fromStdString(id);
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if (itemTypeOf(*it) == ItemType::Layer &&
            (*it)->data(0, kRoleId).toString() == qid) {
            QSignalBlocker sb(m_tree);
            (*it)->setText(0, QString::fromStdString(label));
            applyFilter(m_search->text());
            return;
        }
        ++it;
    }
}

void LineListPanel::setSelectedLayers(const std::vector<std::string>& layer_ids)
{
    QSignalBlocker sb(m_tree);
    m_tree->clearSelection();
    for (const auto& id : layer_ids) {
        const QString qid = QString::fromStdString(id);
        QTreeWidgetItemIterator it(m_tree);
        while (*it) {
            if (itemTypeOf(*it) == ItemType::Layer &&
                (*it)->data(0, kRoleId).toString() == qid) {
                (*it)->setSelected(true);
                break;
            }
            ++it;
        }
    }
}

void LineListPanel::onItemClicked(QTreeWidgetItem* item, int)
{
    if (!item) return;
    if (m_tree->selectedItems().size() > 1) return;
    switch (itemTypeOf(item)) {
        case ItemType::Layer:
            emit layerSelected(item->data(0, kRoleId).toString().toStdString());
            break;
        case ItemType::Source:
            emit sourceSelected(item->data(0, kRoleId).toString().toStdString());
            break;
        case ItemType::Contact:
            emit contactSelected(item->data(0, kRoleId).toULongLong());
            break;
        default:
            break;
    }
}

void LineListPanel::onItemDoubleClicked(QTreeWidgetItem* item, int)
{
    if (!item || itemTypeOf(item) != ItemType::Layer) return;
    const std::string id = item->data(0, kRoleId).toString().toStdString();
    if (id.empty()) return;

    // Open the matching viewer for this line's modality (mirrors the context-menu
    // policy): sub-bottom → SBP viewer, sidescan → waterfall. Other modalities have
    // no dedicated viewer, so double-click just leaves the (single-click) selection.
    using M = app::Modality;
    const auto mod = static_cast<M>(item->data(0, kRoleModality).toInt());
    if (mod == M::SubBottom)      emit openInSubBottomRequested(id);
    else if (mod == M::Sidescan)  emit openInWaterfallRequested(id);
}

void LineListPanel::onSelectionChanged()
{
    std::vector<std::string> ids;
    for (QTreeWidgetItem* item : m_tree->selectedItems()) {
        if (itemTypeOf(item) == ItemType::Layer)
            ids.push_back(item->data(0, kRoleId).toString().toStdString());
    }
    emit layerMultiSelected(ids);
}

void LineListPanel::applyFilter(const QString& text)
{
    const QString needle = text.trimmed().toLower();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        updateVisibility(m_tree->topLevelItem(i), needle);
}

bool LineListPanel::updateVisibility(QTreeWidgetItem* item, const QString& needle)
{
    const bool selfMatch = needle.isEmpty()
        || item->text(0).toLower().contains(needle)
        || item->toolTip(0).toLower().contains(needle);
    bool childMatch = false;
    for (int i = 0; i < item->childCount(); ++i)
        childMatch |= updateVisibility(item->child(i), needle);
    const bool visible = selfMatch || childMatch;
    item->setHidden(!visible);
    const auto type = itemTypeOf(item);
    if (!needle.isEmpty()
            && (type == ItemType::Unknown || type == ItemType::Modality
                || type == ItemType::LayerGroup || type == ItemType::ContactGroup)
            && childMatch)
        item->setExpanded(true);
    return visible;
}

bool LineListPanel::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj != m_tree || ev->type() != QEvent::KeyPress)
        return QWidget::eventFilter(obj, ev);

    auto* ke = static_cast<QKeyEvent*>(ev);

    if (ke->key() == Qt::Key_Delete) {
        std::vector<std::string> ids;
        for (QTreeWidgetItem* item : m_tree->selectedItems())
            if (itemTypeOf(item) == ItemType::Layer)
                ids.push_back(item->data(0, kRoleId).toString().toStdString());
        if (ids.size() == 1)     emit removeLayerRequested(ids[0]);
        else if (ids.size() > 1) emit removeLayersRequested(ids);
        return !ids.empty();
    }

    if (ke->key() == Qt::Key_F2) {
        auto* cur = m_tree->currentItem();
        if (cur && itemTypeOf(cur) == ItemType::Layer) {
            emit renameLayerRequested(cur->data(0, kRoleId).toString().toStdString());
            return true;
        }
        return false;
    }

    if (ke->key() == Qt::Key_Space) {
        bool handled = false;
        for (QTreeWidgetItem* item : m_tree->selectedItems()) {
            if (itemTypeOf(item) != ItemType::Layer) continue;
            const Qt::CheckState next = (item->checkState(0) == Qt::Checked)
                ? Qt::Unchecked : Qt::Checked;
            item->setCheckState(0, next);
            handled = true;
        }
        return handled;
    }

    return QWidget::eventFilter(obj, ev);
}

} // namespace dolphin::ui
