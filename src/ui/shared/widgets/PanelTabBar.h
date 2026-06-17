#pragma once
#include <QWidget>

class QToolButton;
class QButtonGroup;
class QHBoxLayout;

namespace dolphin::ui {

// Shared horizontal tab strip for side-panel chrome.
//
// A fixed-height (Theme::kPanelHdrH) row of mutually-exclusive QToolButton tabs
// separated by 1px vertical dividers. Both the right (Properties) panel and the
// sensor/modality bar use this so the two strips can never drift apart.
//
// Object-name contract (styled centrally in AppStylePanels.cpp):
//   self     → "panelTabBar"
//   buttons  → "panelTab"
//   dividers → "panelTabSep"
class PanelTabBar : public QWidget {
    Q_OBJECT
public:
    explicit PanelTabBar(QWidget* parent = nullptr);

    // Append a tab. id must be unique within the bar. Returns the created button
    // so callers can keep a reference for enable/disable or checked-state control.
    QToolButton* addTab(const QString& label, int id);

    // Returns the button registered under id, or nullptr.
    QToolButton* button(int id) const;

    // Check the tab with the given id (does not emit tabChanged).
    void setCurrentId(int id);

    // Id of the currently checked tab, or -1 when none is checked.
    int currentId() const;

signals:
    // Emitted when the user activates a different tab (not on programmatic checks).
    void tabChanged(int id);

private:
    QHBoxLayout*  m_layout   = nullptr;
    QButtonGroup* m_group    = nullptr;
    bool          m_has_tabs = false;
};

} // namespace dolphin::ui
