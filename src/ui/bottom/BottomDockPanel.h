#pragma once
#include "ui/bottom/DiagnosticsHub.h"
#include <QWidget>
#include <array>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;

namespace dolphin::ui { class TerminalWidget; }

namespace dolphin::ui {

// Bottom dock panel with Problems / Output / Jobs / Terminal tabs.
// Docks at the bottom of the main area; collapsible; height is user-resizable.
//
// Usage:
//   panel = new BottomDockPanel(hub, parent);
//   // hub posts automatically feed the panel tabs via signal connections.
class BottomDockPanel : public QWidget {
    Q_OBJECT
public:
    explicit BottomDockPanel(DiagnosticsHub* hub, QWidget* parent = nullptr);

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

    // Forwards to the Terminal tab's working directory.
    void setWorkingDirectory(const QString& dir);

    void saveState() const;
    void restoreState();

    static constexpr int kHandleH       = 4;    // resize grip height
    static constexpr int kHeaderH       = 30;   // tab bar height
    static constexpr int kMinContentH   = 80;
    static constexpr int kDefaultH      = 180;

signals:
    void layerFocusRequested(const QString& layer_id);
    void collapsedChanged(bool collapsed);
    void closeRequested();

private slots:
    void onProblemPosted(const dolphin::ui::DiagnosticsHub::Problem& p);
    void onProblemsCleared(const QString& layer_id);
    void onOutputLogged(const QString& msg);
    void onJobChanged(uint32_t id);

private:
    void buildLayout();
    void buildHeader(QWidget* parent);
    void buildProblemsTab(QWidget* parent);
    void buildOutputTab(QWidget* parent);
    void buildJobsTab(QWidget* parent);
    void buildTerminalTab(QWidget* parent);

    void switchTab(int idx);
    void updateBadge(int tab_idx, int count, bool is_error = false);
    void removeProblemsEmptyState();
    void updateProblemsEmptyState();
    void rebuildJobsTab();
    void populateFromHub();

    DiagnosticsHub*   m_hub;

    bool              m_collapsed    = false;
    int               m_content_h    = kDefaultH;

    // Header widgets
    QWidget*          m_header       = nullptr;
    static constexpr int kTabCount   = 4;
    std::array<QToolButton*, kTabCount> m_tab_btns{};
    std::array<QLabel*,      kTabCount> m_badges{};
    QToolButton*      m_collapse_btn = nullptr;
    QToolButton*      m_close_btn    = nullptr;

    // Resize handle (top edge of panel)
    QWidget*          m_handle       = nullptr;

    // Content
    QStackedWidget*   m_stack        = nullptr;

    // Tab pages
    QListWidget*      m_prob_list    = nullptr;
    QPlainTextEdit*   m_out_edit     = nullptr;
    QListWidget*      m_job_list     = nullptr;
    TerminalWidget*   m_terminal     = nullptr;

    int               m_active_tab   = 1;  // default: Output
};

} // namespace dolphin::ui
