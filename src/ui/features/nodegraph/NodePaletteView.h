#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class QEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

namespace dolphin::ui {

class NodePaletteView : public QWidget {
    Q_OBJECT
public:
    struct NodeItem {
        QString category;
        QString label;
        QString type_id;
    };

    explicit NodePaletteView(QWidget* parent = nullptr);

    void  setNodes(const QVector<NodeItem>& items);
    void  setFilterText(const QString& text);
    void  setPlacementEnabled(bool enabled);
    void  resetInteractionState();
    QSize sizeHint() const override;

signals:
    void nodePicked(const QString& type_id);
    void nodeActivated(const QString& type_id);
    void nodeDragStarted(const QString& type_id, const QPoint& global_pos);
    void nodeDragMoved(const QPoint& global_pos);
    void nodeDragFinished(bool commit, const QPoint& global_pos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Entry {
        bool    header = false;
        QString category;
        QString label;
        QString type_id;
        QRect   rect;
    };

    void rebuildEntries();
    int  hitEntryIndex(const QPoint& pos) const;
    bool isNodeEntry(int index) const;

    QVector<NodeItem> m_nodes;
    QVector<Entry>    m_entries;
    QString           m_filter_text;
    bool              m_placement_enabled = true;
    int               m_content_height = 0;
    int               m_hover_index = -1;
    int               m_selected_index = -1;
    int               m_pressed_index = -1;
    bool              m_drag_active = false;
    QPoint            m_press_pos;
};

} // namespace dolphin::ui
