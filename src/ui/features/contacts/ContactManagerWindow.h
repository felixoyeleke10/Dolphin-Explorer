#pragma once
#include <QMainWindow>
#include <QList>
#include <QString>
#include <QVector>
#include <cstdint>
#include <set>
#include <vector>
#include "core/Contact.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;
class QListWidget;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QToolButton;
class QAction;
class QStackedWidget;

namespace dolphin::app { class Project; }

namespace dolphin::ui {

// Cross-modality contact manager, designed like Windows File Explorer: a nav
// tree (left), a content area that switches between a details table and a
// thumbnail grid (centre), and a preview/details pane (right), with an Explorer
// command bar and a status bar + view toggles.
class ContactManagerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ContactManagerWindow(QWidget* parent = nullptr);

    void setProject(app::Project* project);   // nullptr on close
    void refresh();
    void selectContact(uint64_t id);

signals:
    void contactActivated(uint64_t id);
    void removeContactRequested(uint64_t id);
    // Undoable edits routed to MainWindow's undo stack (rename / favourite / group).
    void contactsEditRequested(const QVector<core::Contact>& before,
                               const QVector<core::Contact>& after);
    // Undoable paste: add these contacts (project assigns fresh ids).
    void contactsAddRequested(const QVector<core::Contact>& contacts);
    // Undoable group-entity ops (create / rename / delete) + create-and-assign.
    void groupAddRequested(const QString& name);
    void groupRenameRequested(const QString& id, const QString& name);
    void groupRemoveRequested(const QString& id);
    void groupAddAndAssignRequested(const QString& name, const QVector<uint64_t>& contact_ids);

private:
    // A visited folder in the nav tree (survives tree rebuilds — identity, not pointer).
    struct NavLoc { int kind = 0; int mod = -1; QString line; };

    void     buildNavBar();       // back/forward/up/refresh + folder-link bar + search
    void     buildCommandBar();
    QWidget* buildPreviewPane();
    void     rebuildNav();

    // Folder navigation (Explorer-style).
    void navBack();
    void navForward();
    void navUp();
    void recordNavLocation();     // push current folder onto history
    void updateNavButtons();
    QTreeWidgetItem* findNavItem(int kind, int mod, const QString& line) const;
    void     populateForCurrentNode();
    void     applySearch();
    void     updateBreadcrumb();
    void     updateCommandState();
    void     updateStatus();
    void     updatePreview();
    void     setViewMode(int mode);   // 0 = details list, 1 = thumbnails

    uint64_t              currentRowId() const;
    std::vector<uint64_t> selectedIds() const;
    const core::Contact*  findContact(uint64_t id) const;

    void copySelection(bool cut);
    void pasteClipboard();
    void renameSelection();
    void deleteSelection();
    void toggleFavouriteSelection();
    void invertSelection();

    void exportContacts();   // CSV / PDF / Word export of the visible contacts

    // Recycle bin.
    bool isViewingRecycleBin() const;
    void restoreSelection();
    void purgeSelection();
    void emptyRecycleBin();

    // Custom contact groups (the "Group" section).
    void newGroup();
    void renameGroup(const std::string& id, const QString& current_name);
    void deleteGroup(const std::string& id, const QString& name);
    void assignSelectionToGroup(const std::string& group_id);
    void showNavContextMenu(const QPoint& global_pos, QTreeWidgetItem* item);

    app::Project* m_project = nullptr;

    std::set<int>   m_expanded_facets;         // which facet sections are expanded
    QTreeWidget*    m_nav        = nullptr;
    QLabel*         m_breadcrumb = nullptr;
    QLineEdit*      m_search     = nullptr;
    QStackedWidget* m_views      = nullptr;   // 0 = details table, 1 = thumbnails
    QTableWidget*   m_table      = nullptr;
    QListWidget*    m_thumbs     = nullptr;
    QLabel*         m_status     = nullptr;
    int             m_view_mode  = 0;

    QAction*     m_view_details_act = nullptr;
    QAction*     m_view_thumbs_act  = nullptr;
    QToolButton* m_btn_details      = nullptr;
    QToolButton* m_btn_thumbs       = nullptr;

    // Preview / details pane.
    QStackedWidget* m_pv_stack  = nullptr;    // 0 = placeholder, 1 = details
    QLabel* m_pv_title  = nullptr;
    QLabel* m_pv_image  = nullptr;            // square snapshot (hidden when absent)
    QLabel* m_pv_sensor = nullptr;
    QLabel* m_pv_conf   = nullptr;
    QLabel* m_pv_class  = nullptr;
    QLabel* m_pv_line   = nullptr;
    QLabel* m_pv_coords = nullptr;
    QLabel* m_pv_depth  = nullptr;
    QLabel* m_pv_range  = nullptr;
    QLabel* m_pv_notes  = nullptr;
    QLabel* m_pv_meta   = nullptr;

    QAction* m_act_back    = nullptr;
    QAction* m_act_fwd     = nullptr;
    QAction* m_act_up      = nullptr;
    QAction* m_act_refresh = nullptr;

    QAction* m_act_cut    = nullptr;
    QAction* m_act_copy   = nullptr;
    QAction* m_act_paste  = nullptr;
    QAction* m_act_rename = nullptr;
    QAction* m_act_delete = nullptr;
    QAction* m_act_fav    = nullptr;
    QAction* m_act_props  = nullptr;
    QAction* m_act_export = nullptr;

    QList<QTreeWidgetItem*> m_crumb_items;

    std::vector<NavLoc> m_nav_history;     // visited folders
    int  m_nav_pos       = -1;             // index into m_nav_history
    bool m_nav_replaying = false;          // guard: navigating via back/forward/up

    std::vector<core::Contact> m_clipboard;
    bool m_clip_cut = false;

    bool m_show_map_folder = true;
    bool m_confirm_delete  = false;
};

} // namespace dolphin::ui
