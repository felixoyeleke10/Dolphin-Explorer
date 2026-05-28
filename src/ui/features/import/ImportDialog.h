#pragma once
#include "ui/features/import/ImportController.h"  // FileImportAction
#include "core/Artifact.h"
#include "core/SpatialRef.h"

#include <QDialog>
#include <QList>
#include <QString>
#include <vector>

namespace dolphin::app { class Project; }
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QVBoxLayout;

namespace dolphin::ui {

// ── ImportDialogResult ────────────────────────────────────────────────────────
struct ImportDialogResult {
    enum class ProjectTarget { Current, New };

    ProjectTarget         target           = ProjectTarget::Current;
    QString               new_project_name;
    QString               new_project_folder;
    core::SpatialRef      source_crs;          // empty = auto-detect
    QList<FileImportAction> files;
    bool accepted = false;
};

// ── ImportDialog ─────────────────────────────────────────────────────────────
// Shown after the user has selected sensor types in ImportSetupDialog and
// picked files via the system file picker.
//
// module_filter: the sensor selection from ImportSetupDialog.
//   - Empty = all modules; stamped onto every FileImportAction.
//   - Controls which sections are shown (e.g. FREQUENCY BANDS only for SSS).
class ImportDialog : public QDialog {
    Q_OBJECT
public:
    ImportDialog(const QStringList&                     paths,
                 app::Project*                          current_project,
                 const std::vector<core::ArtifactType>& module_filter  = {},
                 const core::SpatialRef&                suggested_crs  = {},
                 QWidget*                               parent         = nullptr);

    const ImportDialogResult& result() const { return m_result; }

signals:
    void importConfirmed(const ImportDialogResult& result);

private slots:
    void onProjectTargetChanged();
    void onNameEdited(const QString& name);
    void onBrowseFolder();
    void onAddFiles();
    void onAccept();
    void refreshSummary();

private:
    void buildProjectSection  (QVBoxLayout* root);
    void buildCrsSection      (QVBoxLayout* root);
    void buildFreqBandsSection(QVBoxLayout* root);
    void buildFileTable       (QVBoxLayout* root);
    void addFileRow         (const QString& path, int row);
    void updateFolderLabel  ();
    void updateCrsPreview   ();

    static constexpr int kColName   = 0;
    static constexpr int kColFormat = 1;
    static constexpr int kColSize   = 2;
    static constexpr int kColAction = 3;

    app::Project*           m_current_project = nullptr;
    std::vector<core::ArtifactType> m_module_filter;  // from ImportSetupDialog

    // Project section
    QRadioButton*  m_radio_current = nullptr;
    QLabel*        m_current_name  = nullptr;
    QRadioButton*  m_radio_new     = nullptr;
    QFrame*        m_new_proj_form = nullptr;
    QLineEdit*     m_name_edit     = nullptr;
    QLabel*        m_folder_label  = nullptr;
    QString        m_browse_folder;

    // CRS section
    core::SpatialRef m_selected_crs;
    bool             m_any_projected      = false;
    QLabel*          m_crs_preview        = nullptr;
    QLabel*          m_crs_required_label = nullptr;
    QPushButton*     m_crs_pick_btn       = nullptr;

    // Frequency band section (SSS only — null when not built)
    QCheckBox*     m_hf_check = nullptr;
    QCheckBox*     m_lf_check = nullptr;

    // File table
    QTableWidget*            m_table   = nullptr;
    QList<FileImportAction>  m_actions;

    // Footer
    QLabel*      m_summary_label = nullptr;
    QPushButton* m_btn_import    = nullptr;

    ImportDialogResult m_result;
};

} // namespace dolphin::ui
