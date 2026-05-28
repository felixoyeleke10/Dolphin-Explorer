#include "ui/features/nodegraph/NodePaletteView.h"
#include "ui/shell/Theme.h"

#include <QApplication>
#include <QEvent>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <algorithm>

namespace dolphin::ui {

namespace {

constexpr int kOuterPad   = 6;
constexpr int kHeaderH    = 22;
constexpr int kRowH       = 26;
constexpr int kRowGap     = 2;
constexpr int kSectionGap = 6;

const QColor kSelectionBg (10, 132, 255, 45);  // accent hue at low opacity — selected row
const QColor kHoverBg     (255, 255, 255,  8);  // very subtle white tint — hovered row

} // namespace

NodePaletteView::NodePaletteView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void NodePaletteView::setNodes(const QVector<NodeItem>& items)
{
    m_nodes = items;
    rebuildEntries();
}

void NodePaletteView::setFilterText(const QString& text)
{
    const QString needle = text.trimmed();
    if (needle == m_filter_text)
        return;

    m_filter_text = needle;
    rebuildEntries();
}

void NodePaletteView::setPlacementEnabled(bool enabled)
{
    if (m_placement_enabled == enabled)
        return;

    m_placement_enabled = enabled;
    update();
}

void NodePaletteView::resetInteractionState()
{
    m_drag_active = false;
    m_pressed_index = -1;
    update();
}

QSize NodePaletteView::sizeHint() const
{
    return QSize(160, std::max(140, m_content_height));
}

void NodePaletteView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(Theme::kBgPanel));

    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry& entry = m_entries[i];

        if (entry.header) {
            // Section label + hairline below
            const QRect hr = entry.rect;
            QFont f; f.setPixelSize(10); f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
            f.setWeight(QFont::Bold);
            p.setFont(f);
            p.setPen(QColor(Theme::kTextMuted));
            p.drawText(hr.adjusted(10, 0, -10, 0), Qt::AlignLeft | Qt::AlignVCenter,
                       entry.category.toUpper());

            p.setPen(QPen(QColor(Theme::kBorder), 1));
            p.drawLine(hr.left() + 10, hr.bottom(),
                       hr.right() - 10, hr.bottom());
            continue;
        }

        const bool hov = (i == m_hover_index);
        const bool sel = (i == m_selected_index);

        // Row background
        const QRect rr = entry.rect.adjusted(6, 1, -6, -1);
        if (sel) {
            p.setPen(Qt::NoPen);
            p.setBrush(kSelectionBg);
            p.drawRoundedRect(rr, 4, 4);
        } else if (hov) {
            p.setPen(Qt::NoPen);
            p.setBrush(kHoverBg);
            p.drawRoundedRect(rr, 4, 4);
        }

        // Left accent dot
        const int dot_x = rr.left() + 10;
        const int dot_y = rr.center().y();
        p.setPen(Qt::NoPen);
        p.setBrush(sel ? QColor(Theme::kAccentSoft) : QColor(Theme::kBorderMenu));
        p.drawEllipse(QPoint(dot_x, dot_y), 3, 3);

        // Label
        QFont lf; lf.setPixelSize(11); lf.setWeight(sel ? QFont::DemiBold : QFont::Normal);
        p.setFont(lf);
        QColor tc = sel ? QColor(Theme::kTextPrimary) : QColor(Theme::kTextSecond);
        if (!m_placement_enabled) tc.setAlpha(120);
        p.setPen(tc);
        p.drawText(rr.adjusted(20, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter, entry.label);
    }
}

void NodePaletteView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rebuildEntries();
}

void NodePaletteView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_pressed_index = hitEntryIndex(event->position().toPoint());
    if (!isNodeEntry(m_pressed_index)) {
        m_pressed_index = -1;
        return;
    }

    m_selected_index = m_pressed_index;
    m_press_pos = event->position().toPoint();
    emit nodePicked(m_entries[m_selected_index].type_id);
    update();
}

void NodePaletteView::mouseMoveEvent(QMouseEvent* event)
{
    const int hover = hitEntryIndex(event->position().toPoint());
    if (hover != m_hover_index) {
        m_hover_index = hover;
        update();
    }

    if (!m_placement_enabled || !isNodeEntry(m_pressed_index))
        return;

    const int drag_distance = (event->position().toPoint() - m_press_pos).manhattanLength();
    if (!m_drag_active) {
        if (drag_distance < QApplication::startDragDistance())
            return;

        m_drag_active = true;
        emit nodeDragStarted(m_entries[m_pressed_index].type_id,
                             event->globalPosition().toPoint());
    }
}

void NodePaletteView::mouseReleaseEvent(QMouseEvent* event)
{
    m_drag_active = false;
    m_pressed_index = -1;
    QWidget::mouseReleaseEvent(event);
}

void NodePaletteView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    const int index = hitEntryIndex(event->position().toPoint());
    if (!isNodeEntry(index))
        return;

    m_selected_index = index;
    update();
    emit nodeActivated(m_entries[index].type_id);
}

void NodePaletteView::leaveEvent(QEvent* event)
{
    m_hover_index = -1;
    update();
    QWidget::leaveEvent(event);
}

void NodePaletteView::rebuildEntries()
{
    const int content_w = std::max(120, width());
    QVector<Entry> entries;
    int y = kOuterPad;

    QVector<QString> category_order;
    QMap<QString, QVector<NodeItem>> grouped;
    for (const NodeItem& node : m_nodes) {
        if (!grouped.contains(node.category))
            category_order.append(node.category);
        grouped[node.category].append(node);
    }

    for (const QString& category : category_order) {
        QVector<NodeItem> visible_nodes;
        for (const NodeItem& node : grouped[category]) {
            const bool match = m_filter_text.isEmpty()
                || node.label.contains(m_filter_text, Qt::CaseInsensitive)
                || node.type_id.contains(m_filter_text, Qt::CaseInsensitive)
                || node.category.contains(m_filter_text, Qt::CaseInsensitive);
            if (match)
                visible_nodes.append(node);
        }

        if (visible_nodes.isEmpty())
            continue;

        Entry header;
        header.header = true;
        header.category = category;
        header.rect = QRect(0, y, content_w, kHeaderH);
        entries.append(header);
        y += kHeaderH;

        for (const NodeItem& node : visible_nodes) {
            Entry row;
            row.header = false;
            row.category = category;
            row.label = node.label;
            row.type_id = node.type_id;
            row.rect = QRect(0, y, content_w, kRowH);
            entries.append(row);
            y += kRowH + kRowGap;
        }

        y += kSectionGap;
    }

    m_entries = entries;
    m_content_height = y + kOuterPad;

    if (!isNodeEntry(m_selected_index))
        m_selected_index = -1;
    if (!isNodeEntry(m_hover_index))
        m_hover_index = -1;
    if (!isNodeEntry(m_pressed_index))
        m_pressed_index = -1;

    setMinimumHeight(std::max(140, m_content_height));
    updateGeometry();
    update();
}

int NodePaletteView::hitEntryIndex(const QPoint& pos) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (!m_entries[i].header && m_entries[i].rect.contains(pos))
            return i;
    }
    return -1;
}

bool NodePaletteView::isNodeEntry(int index) const
{
    return index >= 0
        && index < m_entries.size()
        && !m_entries[index].header
        && !m_entries[index].type_id.isEmpty();
}

} // namespace dolphin::ui
