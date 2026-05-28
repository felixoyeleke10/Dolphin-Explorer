#pragma once
#include <QWidget>
#include "app/project/Project.h"

class QPropertyAnimation;
class QSvgRenderer;

namespace dolphin::ui {

class LineListPanel;

// Floating layer-picker bar parented to the main viewport area (not in layout).
// Collapsed: a compact pill — shows icon + "LAYERS" + chevron.
// Expanded:  grows downward to reveal the full LineListPanel.
// Call reposition() after parent resize or left-panel toggle to keep it clear.
class LayerPickerWidget : public QWidget {
    Q_OBJECT
public:
    explicit LayerPickerWidget(QWidget* parent = nullptr);

    void setProject(app::Project* project);
    void refresh();
    void setLayerVisibility(const std::string& id, bool visible);
    void updateLayerLabel(const std::string& id, const std::string& label);
    void refreshContacts();

    // Force the picker open (e.g. when a file is imported).
    // No-op if already expanded.
    void expand();

    // Re-anchor. left_x = pixel offset from left edge of parent that is now
    // occupied (activity bar + open overlay panel). Animates smoothly.
    void reposition(const QSize& parent_size, int left_x = 0);

signals:
    void layerSelected(const std::string& layer_id);
    void sourceSelected(const std::string& source_id);
    void contactSelected(uint64_t contact_id);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private slots:
    void toggle();

private:
    static constexpr int kCollapsedH = 32;
    static constexpr int kExpandedH  = 340;
    static constexpr int kWidth      = 244;
    static constexpr int kMargin     = 10;
    static constexpr int kRadius     = 9;

    bool                m_expanded   = false;
    QWidget*            m_header     = nullptr;
    LineListPanel*      m_list       = nullptr;
    QPropertyAnimation* m_size_anim  = nullptr;   // animates height
    QPropertyAnimation* m_pos_anim   = nullptr;   // animates x when panel shifts
    QSvgRenderer*       m_icon_svg   = nullptr;
    QWidget*            m_chevron    = nullptr;
};

} // namespace dolphin::ui
