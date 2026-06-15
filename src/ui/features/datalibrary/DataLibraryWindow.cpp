// DataLibraryWindow.cpp — constructor, page switching, modality filter, project binding.

#include "ui/features/datalibrary/DataLibraryWindow.h"
#include "ui/shared/UiUtils.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/Contact.h"

#include <QIcon>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>

namespace dolphin::ui {

// -- Constructor ---------------------------------------------------------------

DataLibraryWindow::DataLibraryWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Data Library"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/database.svg")));
    setMinimumSize(1000, 500);
    resize(1300, 720);

    buildMenuBar();
    buildNavBar();

    // -- 3-way horizontal splitter: [Filters | Tables | Formats] -------------
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    splitter->addWidget(buildFilterPanel());    // left  — fixed ~200 px

    m_stack = new QStackedWidget;
    buildLayersPage();    // index PageLayers   = 0
    buildContactsPage();  // index PageContacts = 1
    buildIssuesPage();    // index PageIssues   = 2
    splitter->addWidget(m_stack);               // center — stretches

    splitter->addWidget(buildFormatsPanel());   // right  — fixed ~200 px
                                               // (m_layers_table already exists)
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({ 200, 900, 200 });

    setCentralWidget(splitter);
}

// -- Page switching ------------------------------------------------------------

void DataLibraryWindow::switchPage(Page page)
{
    m_current_page = page;
    m_stack->setCurrentIndex(static_cast<int>(page));
    m_tab_layers->setChecked(page == PageLayers);
    m_tab_contacts->setChecked(page == PageContacts);
    m_tab_issues->setChecked(page == PageIssues);
    m_modality_btn->setVisible(page == PageLayers);
    applyFilters();
}

// -- Modality filter -----------------------------------------------------------

void DataLibraryWindow::setModalityFilter(int modality)
{
    m_modality_filter = modality;
    refreshLayers();
    updateTabCounts();
    updateStatusBar();
}

// -- Project binding -----------------------------------------------------------

void DataLibraryWindow::setProject(app::Project* project)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);

    m_project = project;

    if (m_project) {
        connect(m_project, &app::Project::layerPending,
                this, [this](app::DataLayer*) { refreshAll(); });
        connect(m_project, &app::Project::layerReady,
                this, [this](app::DataLayer*) { refreshAll(); });
        connect(m_project, &app::Project::layerRemoved,
                this, [this](const std::string&) { refreshAll(); });
        connect(m_project, &app::Project::layersReordered,
                this, [this]() { refreshLayers(); });
        connect(m_project, &app::Project::contactAdded,
                this, [this](const core::Contact&) { refreshContacts(); updateTabCounts(); });
        connect(m_project, &app::Project::contactRemoved,
                this, [this](uint64_t) { refreshContacts(); updateTabCounts(); });
        connect(m_project, &app::Project::modified,
                this, &DataLibraryWindow::updateWindowTitle);
    }

    updateWindowTitle();
    refreshAll();
}

} // namespace dolphin::ui
