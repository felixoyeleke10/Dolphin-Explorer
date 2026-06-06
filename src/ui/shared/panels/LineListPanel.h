#pragma once
#include <QWidget>
#include <set>
#include <string>
#include <vector>
#include "app/project/Project.h"

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  LineListPanel — project tree panel used in two modes:
//
//    Explorer mode:
//      PROJECT  — project name
//      FILES    — imported source files
//      SIDESCAN / SUB-BOTTOM / ... — layers grouped directly by modality
//      CONTACTS — point picks across the project
//      FEATURES — shape annotations, Phase 2 placeholder
//
//    Layers-only mode:
//      SIDESCAN / SUB-BOTTOM / ... — layers grouped directly by modality
//      CONTACTS — point picks across the project
//      FEATURES — shape annotations, Phase 2 placeholder
//
//  Emits:
//    layerSelected(layer_id)      — user clicked a layer row
//    sourceSelected(source_id)    — user clicked a source file row
//    contactSelected(contact_id)  — user clicked a contact row
// -----------------------------------------------------------------------------

class LineListPanel : public QWidget {
    Q_OBJECT
public:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    enum class ContentMode {
        Explorer,
        LayersOnly,
    };

    explicit LineListPanel(QWidget* parent = nullptr,
                           ContentMode mode = ContentMode::Explorer);

    void setProject(app::Project* project);
    void refresh();
    void setActiveLayer(const std::string& layer_id);
    // Programmatically sets the tree's selection to the given layer IDs.
    // Uses QSignalBlocker so it doesn't re-emit layerMultiSelected.
    void setSelectedLayers(const std::vector<std::string>& layer_ids);

    // Targeted updates — cheaper than refresh() when only one thing changed.
    void setLayerVisibility(const std::string& id, bool visible);
    void updateLayerLabel(const std::string& id, const std::string& label);
    void refreshContacts();
    void refreshLayer(const std::string& id);

signals:
    void layerSelected              (const std::string& layer_id);
    void layerMultiSelected         (const std::vector<std::string>& layer_ids);
    void sourceSelected             (const std::string& source_id);
    void contactSelected            (uint64_t contact_id);
    void layerVisibilityChanged     (const std::string& layer_id, bool visible);
    void navTrackVisibilityChanged  (const std::string& layer_id, bool visible);

    // Context menu — single-layer actions
    void openInWaterfallRequested   (const std::string& layer_id);
    void openInSubBottomRequested   (const std::string& layer_id);
    void runLayerRequested          (const std::string& layer_id);
    void removeLayerRequested       (const std::string& layer_id);
    void renameLayerRequested       (const std::string& layer_id);

    // Context menu — multi-layer actions (ids.size() >= 1)
    void runLayersRequested         (const std::vector<std::string>& layer_ids);
    void removeLayersRequested      (const std::vector<std::string>& layer_ids);
    void exportLayersRequested      (const std::vector<std::string>& layer_ids, const QString& format);
    void mergeLayersRequested       (const std::vector<std::string>& layer_ids);

    // Context menu — source and contact actions
    void removeContactRequested     (uint64_t contact_id);
    void exportContactsRequested    ();
    void revealInExplorerRequested  (const std::string& source_id);

    // Empty-state call-to-action buttons
    void newProjectRequested  ();
    void importFilesRequested ();
    void recentProjectRequested(const QString& path);

    // Organisation events — forwarded to MainWindow for activity log
    // kind matches MainWindow::ActivityKind int values (7=TagChange, 8=GroupChange)
    void activityLogged(const QString& description, int kind);

private slots:
    void onItemClicked          (QTreeWidgetItem* item, int column);
    void onItemChanged          (QTreeWidgetItem* item, int column);
    void onSelectionChanged     ();
    void onContextMenuRequested (const QPoint& pos);
    void applyFilter            (const QString& text);

private:
    void buildTree();
    void buildProjectSection();                          // Explorer mode: project root + nested sections
    void buildContactsSection(QTreeWidgetItem* parent);  // nullptr = tree root
    void buildFeaturesSection(QTreeWidgetItem* parent);
    void buildLayersSection(QTreeWidgetItem* parent);    // nullptr = tree root
    void syncLayerOrderFromTree();

    QTreeWidgetItem* makeSectionHeader(const QString& title);
    QTreeWidgetItem* makeEmptyPlaceholder(QTreeWidgetItem* parent, const QString& text);
    bool updateVisibility(QTreeWidgetItem* item, const QString& needle);
    // Moves layer_item within its modality group. delta: negative = up, positive = down.
    // Use -childCount() / +childCount() as sentinels for top / bottom.
    void moveLayerInTree(QTreeWidgetItem* layer_item, int delta);

    ContentMode   m_mode       = ContentMode::Explorer;
    app::Project* m_project    = nullptr;
    QLineEdit*    m_search     = nullptr;
    QTreeWidget*  m_tree       = nullptr;
    QWidget*      m_empty_state = nullptr;   // shown when no project is open
    bool          m_rebuilding = false;
    std::string   m_active_layer_id;

    std::set<std::string> m_visible_nav_tracks;
};

} // namespace dolphin::ui
