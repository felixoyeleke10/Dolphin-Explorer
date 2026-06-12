#include "ui/mainwindow/NewProjectDialog.h"
#include "ui/shared/dialogs/CrsPickerDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"
#include "geo/EpsgDatabase.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace dolphin::ui {

NewProjectDialog::NewProjectDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Project"));
    setModal(true);
    setFixedWidth(480);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::kSpacing6, Theme::kSpacing6,
                             Theme::kSpacing6, Theme::kSpacing4);
    root->setSpacing(Theme::kSpacing5);

    // -- Name ------------------------------------------------------------------
    auto* name_sect = new QWidget(this);
    auto* name_vl   = makeCompactLayout<QVBoxLayout>(name_sect, Theme::kSpacing1);

    auto* name_lbl = new QLabel(tr("Project Name"), name_sect);
    name_lbl->setObjectName("dlgFieldLabel");
    name_vl->addWidget(name_lbl);

    m_name_edit = new QLineEdit(name_sect);
    m_name_edit->setObjectName("dlgLineEdit");
    m_name_edit->setFixedHeight(Theme::kInputH + 4);
    m_name_edit->setPlaceholderText(tr("e.g. MySurvey"));
    m_name_edit->setText(
        "Survey_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss"));
    m_name_edit->selectAll();
    name_vl->addWidget(m_name_edit);

    root->addWidget(name_sect);

    // -- Location --------------------------------------------------------------
    auto* loc_sect = new QWidget(this);
    auto* loc_vl   = makeCompactLayout<QVBoxLayout>(loc_sect, Theme::kSpacing1);

    auto* loc_lbl = new QLabel(tr("Save Location"), loc_sect);
    loc_lbl->setObjectName("dlgFieldLabel");
    loc_vl->addWidget(loc_lbl);

    auto* loc_row = makeCompactLayout<QHBoxLayout>(nullptr, Theme::kSpacing2);

    m_folder_edit = new QLineEdit(loc_sect);
    m_folder_edit->setObjectName("dlgLineEdit");
    m_folder_edit->setFixedHeight(Theme::kInputH + 4);
    const QString default_folder =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + "/Dolphin";
    m_folder_edit->setText(QDir::toNativeSeparators(default_folder));

    auto* browse_btn = new QPushButton(tr("Browse…"), loc_sect);
    browse_btn->setObjectName("dlgBtnBrowse");
    browse_btn->setFixedHeight(Theme::kInputH + 4);

    loc_row->addWidget(m_folder_edit, 1);
    loc_row->addWidget(browse_btn);
    loc_vl->addLayout(loc_row);

    root->addWidget(loc_sect);

    // -- CRS -------------------------------------------------------------------
    auto* crs_sect = new QWidget(this);
    auto* crs_vl   = makeCompactLayout<QVBoxLayout>(crs_sect, Theme::kSpacing1);

    auto* crs_hdr_row = makeCompactLayout<QHBoxLayout>();

    auto* crs_lbl_hdr = new QLabel(tr("Coordinate System"), crs_sect);
    crs_lbl_hdr->setObjectName("dlgFieldLabel");
    crs_hdr_row->addWidget(crs_lbl_hdr, 1);

    auto* crs_opt = new QLabel(tr("optional"), crs_sect);
    crs_opt->setObjectName("dlgLabelMeta");
    crs_hdr_row->addWidget(crs_opt);
    crs_vl->addLayout(crs_hdr_row);

    auto* crs_row = makeCompactLayout<QHBoxLayout>(nullptr, Theme::kSpacing2);

    m_crs_lbl = new QLabel(tr("Not set — can be assigned after import"), crs_sect);
    m_crs_lbl->setObjectName("newProjCrsLabel");
    crs_row->addWidget(m_crs_lbl, 1);

    auto* crs_btn = new QPushButton(tr("Change…"), crs_sect);
    crs_btn->setObjectName("dlgBtnBrowse");
    crs_btn->setFixedHeight(Theme::kInputH + 4);
    crs_row->addWidget(crs_btn);
    crs_vl->addLayout(crs_row);

    root->addWidget(crs_sect);

    // -- Path preview ----------------------------------------------------------
    auto* preview_frame = new QFrame(this);
    preview_frame->setObjectName("newProjPreviewBox");

    auto* preview_vl = new QVBoxLayout(preview_frame);
    preview_vl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3,
                                   Theme::kSpacing4, Theme::kSpacing3);
    preview_vl->setSpacing(2);

    auto* preview_caption = new QLabel(tr("Project will be created at"), preview_frame);
    preview_caption->setObjectName("dlgLabelMeta");
    preview_vl->addWidget(preview_caption);

    m_preview_lbl = new QLabel(preview_frame);
    m_preview_lbl->setObjectName("newProjPreviewPath");
    m_preview_lbl->setWordWrap(true);
    preview_vl->addWidget(m_preview_lbl);

    root->addWidget(preview_frame);

    // -- Buttons ---------------------------------------------------------------
    auto* btn_row = makeCompactLayout<QHBoxLayout>();
    btn_row->addStretch();

    auto* cancel_btn = new QPushButton(tr("Cancel"), this);
    cancel_btn->setObjectName("dlgBtnSecondary");
    cancel_btn->setFixedHeight(Theme::kDialogBtnH);

    m_create_btn = new QPushButton(tr("Create Project"), this);
    m_create_btn->setObjectName("dlgBtnPrimary");
    m_create_btn->setFixedHeight(Theme::kDialogBtnH);
    m_create_btn->setDefault(true);

    btn_row->addWidget(cancel_btn);
    btn_row->addSpacing(Theme::kSpacing2);
    btn_row->addWidget(m_create_btn);
    root->addLayout(btn_row);

    // -- Wiring ----------------------------------------------------------------
    connect(m_name_edit,  &QLineEdit::textChanged, this, &NewProjectDialog::onNameChanged);
    connect(m_folder_edit,&QLineEdit::textChanged, this, &NewProjectDialog::updatePreview);
    connect(browse_btn,   &QPushButton::clicked,   this, &NewProjectDialog::onBrowseFolder);
    connect(crs_btn,      &QPushButton::clicked,   this, &NewProjectDialog::onChangeCrs);
    connect(cancel_btn,   &QPushButton::clicked,   this, &QDialog::reject);
    connect(m_create_btn, &QPushButton::clicked,   this, &QDialog::accept);

    updatePreview();
    validateAccept();
    m_name_edit->setFocus();
}

// -- Public accessors ----------------------------------------------------------

QString NewProjectDialog::projectName() const
{
    return sanitisedName();
}

QString NewProjectDialog::manifestPath() const
{
    const QString name = sanitisedName();
    if (name.isEmpty()) return {};
    return QDir::cleanPath(resolvedFolder() + "/" + name + "/" + name + ".dlp");
}

// -- Slots ---------------------------------------------------------------------

void NewProjectDialog::onNameChanged()
{
    updatePreview();
    validateAccept();
}

void NewProjectDialog::onBrowseFolder()
{
    const QString current = resolvedFolder();
    const QString chosen  = QFileDialog::getExistingDirectory(
        this, tr("Select Save Location"), current);
    if (!chosen.isEmpty())
        m_folder_edit->setText(QDir::toNativeSeparators(chosen));
}

void NewProjectDialog::onChangeCrs()
{
    CrsPickerDialog dlg(m_crs, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_crs = dlg.selectedRef();
        m_crs_lbl->setText(m_crs.empty()
            ? tr("Not set — can be assigned after import")
            : QString::fromStdString(geo::epsgDisplayName(m_crs)));
    }
}

// -- Private -------------------------------------------------------------------

void NewProjectDialog::updatePreview()
{
    const QString name   = sanitisedName();
    const QString folder = resolvedFolder();

    if (name.isEmpty() || folder.isEmpty()) {
        m_preview_lbl->setText(tr("—"));
        return;
    }

    // Abbreviate home directory to ~
    QString display = QDir::cleanPath(folder + "/" + name + "/" + name + ".dlp");
    const QString home = QDir::homePath();
    if (display.startsWith(home))
        display = "~" + display.mid(home.size());

    m_preview_lbl->setText(QDir::toNativeSeparators(display));
}

void NewProjectDialog::validateAccept()
{
    const QString name = sanitisedName();
    m_create_btn->setEnabled(!name.isEmpty());
}

QString NewProjectDialog::sanitisedName() const
{
    QString n = m_name_edit->text().trimmed();
    // Strip characters that are invalid in a directory/file name
    n.remove(QRegularExpression(R"([\\/:*?"<>|])"));
    return n;
}

QString NewProjectDialog::resolvedFolder() const
{
    return QDir::fromNativeSeparators(m_folder_edit->text().trimmed());
}

} // namespace dolphin::ui
