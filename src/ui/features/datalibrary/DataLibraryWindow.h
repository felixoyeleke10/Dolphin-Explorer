#pragma once
#include "app/layers/LayerUtils.h"
#include <QMainWindow>
#include <cstdint>
#include <string>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QRadioButton;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QVBoxLayout;

namespace dolphin::app {
class Project;
}

namespace dolphin::ui {

class DataLibraryWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DataLibraryWindow(QWidget* parent = nullptr);
    void setProject(app::Project* project);

signals:
    void layerActivated(const std::string& layer_id);
    void contactActivated(uint64_t contact_id);

private:
    enum Page { PageLayers = 0, PageContacts = 1, PageIssues = 2 };

    // Widget construction (called once from constructor)
    void     buildMenuBar();
    void     buildNavBar();
    QWidget* buildFilterPanel();
    QWidget* buildFormatsPanel();
    void     buildLayersPage();
    void     buildContactsPage();
    void     buildIssuesPage();

    // Data refresh
    void refreshLayers();
    void refreshContacts();
    void refreshIssues();
    void refreshAll();

    // UI state
    void updateWindowTitle();
    void updateStatusBar();
    void updateTabCounts();
    void applyFilters();
    void switchPage(Page page);
    void setModalityFilter(int modality);   // -1 = All Types

    // -- Core -----------------------------------------------------------------
    app::Project*   m_project         = nullptr;
    QStackedWidget* m_stack           = nullptr;
    Page            m_current_page    = PageLayers;
    int             m_modality_filter = -1;
    bool            m_compact_rows    = false;

    // -- Toolbar ---------------------------------------------------------------
    QToolButton*    m_tab_layers      = nullptr;
    QToolButton*    m_tab_contacts    = nullptr;
    QToolButton*    m_tab_issues      = nullptr;
    QToolButton*    m_modality_btn    = nullptr;

    // -- Left panel — filters -------------------------------------------------
    QRadioButton*   m_match_any       = nullptr;
    QCheckBox*      m_text_chk        = nullptr;
    QLineEdit*      m_filter_text     = nullptr;
    QCheckBox*      m_search_in_name  = nullptr;
    QCheckBox*      m_search_in_src   = nullptr;
    QCheckBox*      m_state_ready     = nullptr;
    QCheckBox*      m_state_failed    = nullptr;
    QCheckBox*      m_state_pending   = nullptr;

    // -- Center — tables -------------------------------------------------------
    QTableWidget*   m_layers_table    = nullptr;
    QTableWidget*   m_contacts_table  = nullptr;
    QTableWidget*   m_issues_table    = nullptr;

    // -- Right panel — column visibility --------------------------------------
    QListWidget*    m_col_list        = nullptr;
};

} // namespace dolphin::ui
