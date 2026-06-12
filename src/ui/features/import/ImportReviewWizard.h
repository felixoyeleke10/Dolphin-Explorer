#pragma once
#include "ui/features/import/ImportController.h"   // FileImportAction
#include "ui/features/import/ImportDialog.h"         // ImportDialogResult
#include "io/ProbeResult.h"
#include "core/Artifact.h"
#include "core/SpatialRef.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QList>
#include <QString>
#include <vector>

class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QScrollArea;
class QTabWidget;
class QVBoxLayout;
class QWidget;

namespace dolphin::app { class Project; }

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  ImportReviewWizard
//
//  Replaces the old ImportSetupDialog + QFileDialog + ImportDialog chain.
//  The wizard opens first (with an empty file list); the user adds files via
//  the "Add Files" button or drag-and-drop.  Each added file is probed on a
//  background thread.  Once all probes complete, the Summary and CRS tabs
//  populate, then the user clicks Import.
//
//  CRS handling: one batch-level CRS (m_project_crs) is applied to all files
//  that require CRS review (needs_crs_review=true, i.e. projected coordinates
//  without a declared EPSG).  Geographic files resolve without it.  Mixed
//  imports where files live in different UTM zones are not currently supported
//  in a single wizard session; the CRS tab shows a warning in that case.
// -----------------------------------------------------------------------------
class ImportReviewWizard : public QDialog {
    Q_OBJECT
public:
    explicit ImportReviewWizard(app::Project*           current_project,
                                const core::SpatialRef& suggested_crs = {},
                                QWidget*                parent        = nullptr);

    const ImportDialogResult& result() const { return m_result; }

    // Pre-populate the file list before show() is called (e.g. drag-drop from OS).
    // module_filter is empty when called from drag-drop (all sensor types accepted).
    void addFiles(const QStringList& paths,
                  std::vector<core::ArtifactType> module_filter = {});

    // Set the sensor-type filter before show(); applied to every "Add Files" click.
    void setModuleFilter(std::vector<core::ArtifactType> filter);


signals:
    void importConfirmed(const ImportDialogResult& result);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private slots:
    void onAddFiles();
    void onProjectTargetChanged();
    void onBrowseFolder();
    void onAccept();

private:
    // -- File entry ------------------------------------------------------------
    struct FileEntry {
        QString          path;
        QString          file_name;
        bool             probing           = false;
        bool             done              = false;
        bool             modality_mismatch = false; // probe result doesn't match wizard sensor filter
        io::ProbeResult  result;
        std::vector<core::ArtifactType> module_filter;  // empty = all sensors
        QFutureWatcher<io::ProbeResult>* watcher = nullptr;
        // Set after probe completes — drives the status badge.
        FileImportAction::Kind classify_kind = FileImportAction::Kind::ImportNew;

        // Row widgets in the file list
        QLabel* name_label   = nullptr;
        QLabel* detail_label = nullptr;
        QLabel* status_label = nullptr;
    };

    void startProbe(int idx);
    void onProbeFinished(int idx);
    void updateFileRow(int idx);
    bool fileMatchesSensorFilter(const io::ProbeResult& r) const;
    void updateSensorHeader();
    void rebuildSummaryTab();
    void rebuildNavTab();
    void rebuildCrsTab();
    void rebuildHeadingTab();
    void rebuildChannelsTab();
    void rebuildHeaderTab();
    void updateTabVisibility();
    void updateImportButton();
    QString modalityString(const io::ProbeResult& r) const;

    // -- Adaptive tab registry -------------------------------------------------
    // Each TabKind encodes its own show/hide predicate in updateTabVisibility().
    // To add a new sensor-specific tab: add a kind here, call makeTab() once in
    // buildTabSection(), add a case in updateTabVisibility(), add a rebuild method.
    enum class TabKind { Summary, Nav, Crs, Heading, Channels, Header };
    struct TabSpec { TabKind kind; int idx = -1; };
    QList<TabSpec> m_tab_specs;

    // -- Data ------------------------------------------------------------------
    QList<FileEntry>   m_entries;
    core::SpatialRef   m_suggested_crs;   // seed value from MainWindow (last used)
    core::SpatialRef   m_project_crs;     // user-confirmed CRS for this import batch
    ImportDialogResult m_result;
    std::vector<core::ArtifactType> m_module_filter;  // set by ImportSetupDialog; empty = all

    // -- File list section -----------------------------------------------------
    QScrollArea* m_file_scroll  = nullptr;
    QWidget*     m_file_content = nullptr;
    QVBoxLayout* m_file_layout  = nullptr;
    QLabel*      m_drop_hint    = nullptr;

    // -- Tab widget ------------------------------------------------------------
    QTabWidget*  m_tabs            = nullptr;
    QWidget*     m_summary_content = nullptr;
    QVBoxLayout* m_summary_layout  = nullptr;
    QWidget*     m_nav_content     = nullptr;
    QVBoxLayout* m_nav_layout      = nullptr;
    QWidget*     m_crs_content     = nullptr;
    QVBoxLayout* m_crs_layout      = nullptr;
    QLabel*      m_crs_value_label = nullptr;   // shows current m_project_crs
    QWidget*     m_hdg_content     = nullptr;
    QVBoxLayout* m_hdg_layout      = nullptr;
    QWidget*     m_chn_content     = nullptr;
    QVBoxLayout* m_chn_layout      = nullptr;
    QWidget*     m_hdr_content     = nullptr;
    QVBoxLayout* m_hdr_layout      = nullptr;

    // -- Project section -------------------------------------------------------
    app::Project*  m_current_project = nullptr;
    QRadioButton*  m_radio_current   = nullptr;
    QLabel*        m_current_name    = nullptr;
    QRadioButton*  m_radio_new       = nullptr;
    QFrame*        m_new_proj_form   = nullptr;
    QLineEdit*     m_name_edit       = nullptr;
    QLabel*        m_folder_label    = nullptr;
    QString        m_browse_folder;

    // -- Header labels (updated by updateSensorHeader when filter is set) -----
    QLabel*      m_hdr_title    = nullptr;
    QLabel*      m_hdr_subtitle = nullptr;

    // -- Footer ----------------------------------------------------------------
    QLabel*      m_status_label    = nullptr;
    QPushButton* m_import_btn      = nullptr;
    bool         m_rebuild_pending = false;

    // -- Layout builders -------------------------------------------------------
    void buildFileSection   (QVBoxLayout* root);
    void buildTabSection    (QVBoxLayout* root);
    void buildProjectSection(QVBoxLayout* root);
};

} // namespace dolphin::ui
