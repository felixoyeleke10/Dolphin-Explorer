#pragma once
#include "core/SpatialRef.h"
#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

namespace dolphin::ui {

// Modal dialog for creating a new project.
//
// Produces a manifest path of the form:
//   <location>/<name>/<name>.dlp
//
// Project::create() handles directory + data/ creation; this dialog
// only validates the path and returns it.
class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(QWidget* parent = nullptr);

    // Call after Accepted.
    QString          projectName()     const;
    QString          manifestPath()    const;
    core::SpatialRef crs()             const { return m_crs; }

private slots:
    void onNameChanged();
    void onBrowseFolder();
    void onChangeCrs();

private:
    void updatePreview();
    void validateAccept();
    QString sanitisedName() const;
    QString resolvedFolder() const;

    QLineEdit*   m_name_edit    = nullptr;
    QLineEdit*   m_folder_edit  = nullptr;
    QLabel*      m_crs_lbl      = nullptr;
    QLabel*      m_preview_lbl  = nullptr;
    QPushButton* m_create_btn   = nullptr;

    core::SpatialRef m_crs;
};

} // namespace dolphin::ui
