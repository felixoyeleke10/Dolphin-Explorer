// ImportReviewWizard.Layout.cpp — section builders: file list, tabs, project form.
#include "ui/features/import/ImportReviewWizard.h"
#include "app/project/Project.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Layout helpers
// -----------------------------------------------------------------------------

void ImportReviewWizard::buildFileSection(QVBoxLayout* root)
{
    auto* sect = new QFrame(this);
    sect->setObjectName("dlgSection");
    auto* vl = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3, Theme::kSpacing4, Theme::kSpacing3);
    vl->setSpacing(Theme::kSpacing2);

    {
        auto* hl = new QHBoxLayout;
        auto* lbl = new QLabel(tr("Files"), sect);
        lbl->setObjectName("dlgSectionLabel");
        hl->addWidget(lbl, 1);

        auto* add_btn = new QPushButton(tr("Add Files..."), sect);
        add_btn->setObjectName("dlgBtnSecondary");
        add_btn->setFixedHeight(Theme::kFormBtnH);
        hl->addWidget(add_btn);
        connect(add_btn, &QPushButton::clicked, this, &ImportReviewWizard::onAddFiles);
        vl->addLayout(hl);
    }

    m_file_scroll = new QScrollArea(sect);
    m_file_scroll->setWidgetResizable(true);
    m_file_scroll->setFrameShape(QFrame::NoFrame);
    m_file_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_file_scroll->setMaximumHeight(180);
    m_file_scroll->setMinimumHeight(60);

    m_file_content = new QWidget;
    m_file_layout  = makeCompactLayout<QVBoxLayout>(m_file_content, 2);

    m_drop_hint = new QLabel(
        tr("Drop files here or click 'Add Files...'"), m_file_content);
    m_drop_hint->setAlignment(Qt::AlignCenter);
    m_drop_hint->setObjectName("dlgHint");
    m_file_layout->addWidget(m_drop_hint);
    m_file_layout->addStretch();

    m_file_scroll->setWidget(m_file_content);
    vl->addWidget(m_file_scroll);

    root->addWidget(sect);
}

void ImportReviewWizard::buildTabSection(QVBoxLayout* root)
{
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName("dlgTabs");
    m_tabs->setMinimumHeight(160);
    m_tabs->setMaximumHeight(220);

    // Helper: create a scroll+content+layout triple, register in m_tab_specs.
    auto makeTab = [&](TabKind kind, const QString& label,
                       QWidget*& content, QVBoxLayout*& layout) {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        content = new QWidget;
        layout  = new QVBoxLayout(content);
        layout->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
        layout->setSpacing(Theme::kSpacing2);
        layout->addStretch();
        scroll->setWidget(content);
        m_tab_specs.push_back({ kind, m_tabs->addTab(scroll, label) });
    };

    // -- Register tabs — order = display order -----------------------------
    // Visibility predicates live in updateTabVisibility(); add a case there
    // when introducing a new TabKind.
    makeTab(TabKind::Summary,  tr("Summary"),  m_summary_content, m_summary_layout);
    makeTab(TabKind::Nav,      tr("Nav"),      m_nav_content,     m_nav_layout);
    makeTab(TabKind::Crs,      tr("CRS"),      m_crs_content,     m_crs_layout);
    makeTab(TabKind::Heading,  tr("Heading"),  m_hdg_content,     m_hdg_layout);
    makeTab(TabKind::Channels, tr("Channels"), m_chn_content,     m_chn_layout);
    makeTab(TabKind::Header,   tr("Header"),   m_hdr_content,     m_hdr_layout);

    root->addWidget(m_tabs);

    rebuildSummaryTab();
    rebuildNavTab();
    rebuildCrsTab();
    rebuildHeadingTab();
    rebuildChannelsTab();
    rebuildHeaderTab();
    updateTabVisibility();
}

void ImportReviewWizard::buildProjectSection(QVBoxLayout* root)
{
    auto* sect = new QFrame(this);
    sect->setObjectName("dlgSection");
    auto* vl = new QVBoxLayout(sect);
    vl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3, Theme::kSpacing4, Theme::kSpacing3);
    vl->setSpacing(Theme::kSpacing1);

    auto* lbl = new QLabel(tr("Project"), sect);
    lbl->setObjectName("dlgSectionLabel");
    vl->addWidget(lbl);

    // "Add to current project" radio
    const QString current_name = m_current_project
        ? QString::fromStdString(m_current_project->name()) : QString();

    m_radio_current = new QRadioButton(sect);
    if (!current_name.isEmpty())
        m_radio_current->setText(tr("Add to current project  (%1)").arg(current_name));
    else
        m_radio_current->setText(tr("Add to current project"));
    m_radio_current->setEnabled(m_current_project != nullptr);

    auto* row1 = makeCompactLayout<QHBoxLayout>();
    row1->addWidget(m_radio_current);
    row1->addStretch();
    vl->addLayout(row1);

    // "Create new project" radio + inline form
    m_radio_new = new QRadioButton(tr("Create new project"), sect);
    auto* row2  = makeCompactLayout<QHBoxLayout>();
    row2->addWidget(m_radio_new);

    m_new_proj_form = new QFrame(sect);
    auto* fl = makeCompactLayout<QHBoxLayout>(m_new_proj_form, Theme::kSpacing2);

    m_name_edit = new QLineEdit(m_new_proj_form);
    m_name_edit->setFixedHeight(Theme::kFormBtnH);
    m_name_edit->setPlaceholderText(tr("Project name"));
    const QString default_name =
        "Survey_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_name_edit->setText(default_name);
    fl->addWidget(m_name_edit, 1);

    auto* browse_btn = new QPushButton(tr("Folder..."), m_new_proj_form);
    browse_btn->setObjectName("dlgBtnSecondary");
    browse_btn->setFixedHeight(Theme::kFormBtnH);
    fl->addWidget(browse_btn);

    m_folder_label = new QLabel(m_new_proj_form);
    m_folder_label->setObjectName("dlgFolderPath");
    m_browse_folder = QStandardPaths::writableLocation(
                          QStandardPaths::DocumentsLocation) + "/Dolphin";
    m_folder_label->setText(m_browse_folder);
    fl->addWidget(m_folder_label, 2);

    row2->addWidget(m_new_proj_form, 1);
    vl->addLayout(row2);

    root->addWidget(sect);

    // Default selection
    if (m_current_project)
        m_radio_current->setChecked(true);
    else
        m_radio_new->setChecked(true);

    connect(m_radio_current, &QRadioButton::toggled,
            this, &ImportReviewWizard::onProjectTargetChanged);
    connect(browse_btn, &QPushButton::clicked,
            this, &ImportReviewWizard::onBrowseFolder);

    onProjectTargetChanged();
}

// -----------------------------------------------------------------------------
//  Project section slots
// -----------------------------------------------------------------------------

void ImportReviewWizard::onProjectTargetChanged()
{
    if (m_new_proj_form)
        m_new_proj_form->setVisible(m_radio_new && m_radio_new->isChecked());
}

void ImportReviewWizard::onBrowseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Project Folder"), m_browse_folder);
    if (!dir.isEmpty()) {
        m_browse_folder = dir;
        m_folder_label->setText(dir);
    }
}

} // namespace dolphin::ui
