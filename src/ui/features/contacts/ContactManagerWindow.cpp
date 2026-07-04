// ContactManagerWindow.cpp — construction + project binding. The window is split
// across aspect files: .Layout (toolbars/preview), .View (nav tree, list/cards,
// preview), .Commands (selection, clipboard, groups, export). Shared model
// constants + delegates live in ContactVisuals.{h,cpp}.
#include "ui/features/contacts/ContactManagerWindow.h"
#include "ui/features/contacts/ContactVisuals.h"
#include "ui/features/contacts/ContactEditorDialog.h"
#include "ui/shell/Theme.h"
#include "app/project/Project.h"

#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVector>

namespace dolphin::ui {

using namespace dolphin::ui::cmvis;

ContactManagerWindow::ContactManagerWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Contact Manager"));
    setObjectName("contactManagerWindow");
    resize(1080, 640);

    buildNavBar();          // row 1: back/forward/up/refresh + folder-link bar + search
    addToolBarBreak();
    buildCommandBar();      // row 2: cut/copy/paste/rename/delete/sort/view/favourite…

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("contactSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    // -- Left: navigation tree (All Contacts + faceted sections) --------------
    m_nav = new QTreeWidget(splitter);
    m_nav->setObjectName("contactNavTree");
    m_nav->setHeaderHidden(true);
    m_nav->setColumnCount(1);
    m_nav->setMinimumWidth(220);
    m_nav->setIndentation(14);
    m_nav->setUniformRowHeights(true);
    splitter->addWidget(m_nav);

    // -- Centre: stacked details/thumbnail views ------------------------------
    auto* centre = new QWidget(splitter);
    centre->setObjectName("contactCentre");
    auto* centre_col = new QVBoxLayout(centre);
    centre_col->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    centre_col->setSpacing(Theme::kSpacing2);

    m_views = new QStackedWidget(centre);

    // Details (table) view.
    m_table = new QTableWidget(centre);
    m_table->setObjectName("contactTable");
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({
        tr("Label"), tr("Sensor"), tr("Line / Source"), tr("Class"),
        tr("Confidence"), tr("Lat"), tr("Lon"), tr("Depth (m)"), tr("Range (m)")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(ColSource, QHeaderView::Stretch);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(false);
    m_table->setSortingEnabled(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(28);
    auto* chip_delegate = new ChipDelegate(m_table);
    m_table->setItemDelegateForColumn(ColSensor, chip_delegate);
    m_table->setItemDelegateForColumn(ColConf,   chip_delegate);
    m_views->addWidget(m_table);

    // Thumbnails (icon) view.
    m_thumbs = new QListWidget(centre);
    m_thumbs->setObjectName("contactThumbs");
    m_thumbs->setViewMode(QListView::IconMode);
    m_thumbs->setResizeMode(QListView::Adjust);
    m_thumbs->setMovement(QListView::Static);
    m_thumbs->setUniformItemSizes(true);
    m_thumbs->setWordWrap(true);
    m_thumbs->setSpacing(8);
    m_thumbs->setIconSize(QSize(72, 72));
    m_thumbs->setGridSize(QSize(116, 112));
    m_thumbs->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_thumbs->setFrameShape(QFrame::NoFrame);
    m_views->addWidget(m_thumbs);

    centre_col->addWidget(m_views, /*stretch=*/1);
    splitter->addWidget(centre);

    // -- Right: preview pane --------------------------------------------------
    splitter->addWidget(buildPreviewPane());

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({230, 600, 250});
    setCentralWidget(splitter);

    // -- Status bar (count + view toggles) ------------------------------------
    m_status = new QLabel(this);
    m_status->setObjectName("contactStatus");
    statusBar()->addWidget(m_status);
    statusBar()->setSizeGripEnabled(false);

    auto mkViewBtn = [this](const QString& glyph, const QString& tip, int mode) {
        auto* b = new QToolButton(this);
        b->setObjectName("contactViewBtn");
        b->setText(glyph);
        b->setCheckable(true);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [this, mode]() { setViewMode(mode); });
        statusBar()->addPermanentWidget(b);
        return b;
    };
    m_btn_details = mkViewBtn(QStringLiteral("☰"), tr("Details"),    0);
    m_btn_thumbs  = mkViewBtn(QStringLiteral("▦"), tr("Thumbnails"), 1);
    m_btn_details->setChecked(true);

    // -- Wiring ---------------------------------------------------------------
    connect(m_nav, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) {
                populateForCurrentNode();
                updateBreadcrumb();
                recordNavLocation();
                updateNavButtons();
            });
    connect(m_nav, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) {
        if (it->data(0, RoleKind).toInt() == NavFacetHeader)
            m_expanded_facets.insert(it->data(0, RoleFacet).toInt());
    });
    connect(m_nav, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* it) {
        if (it->data(0, RoleKind).toInt() == NavFacetHeader)
            m_expanded_facets.erase(it->data(0, RoleFacet).toInt());
    });
    m_nav->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_nav, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showNavContextMenu(m_nav->viewport()->mapToGlobal(pos), m_nav->itemAt(pos));
    });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) { applySearch(); });

    auto onSelChanged = [this]() { updateCommandState(); updateStatus(); updatePreview(); };
    connect(m_table, &QTableWidget::itemSelectionChanged, this, onSelChanged);
    connect(m_thumbs, &QListWidget::itemSelectionChanged, this, onSelChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        const uint64_t id = currentRowId(); if (id) openContactEditor(id); });
    connect(m_thumbs, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (it) openContactEditor(it->data(Qt::UserRole).toULongLong()); });

    auto showCtxMenu = [this](const QPoint& globalPos) {
        const bool has = !selectedIds().empty();

        // Recycle bin gets its own menu: Restore / Delete Forever / Empty.
        if (isViewingRecycleBin()) {
            QMenu menu(this);
            auto* restore = menu.addAction(tr("Restore"));
            restore->setEnabled(has);
            connect(restore, &QAction::triggered, this, [this]() { restoreSelection(); });
            auto* purge = menu.addAction(tr("Delete Forever"));
            purge->setEnabled(has);
            connect(purge, &QAction::triggered, this, [this]() { purgeSelection(); });
            menu.addSeparator();
            auto* empty = menu.addAction(tr("Empty Recycle Bin"));
            empty->setEnabled(m_project && !m_project->recycledContacts().empty());
            connect(empty, &QAction::triggered, this, [this]() { emptyRecycleBin(); });
            menu.exec(globalPos);
            return;
        }

        QMenu menu(this);
        auto* go = menu.addAction(tr("Go to on Map"));
        go->setEnabled(currentRowId() != 0);
        connect(go, &QAction::triggered, this, [this]() {
            const uint64_t id = currentRowId(); if (id) emit contactActivated(id); });
        menu.addSeparator();
        auto* cut  = menu.addAction(tr("Cut"));   cut->setEnabled(has);
        connect(cut,  &QAction::triggered, this, [this]() { copySelection(true); });
        auto* copy = menu.addAction(tr("Copy"));  copy->setEnabled(has);
        connect(copy, &QAction::triggered, this, [this]() { copySelection(false); });
        auto* paste = menu.addAction(tr("Paste")); paste->setEnabled(!m_clipboard.empty());
        connect(paste, &QAction::triggered, this, [this]() { pasteClipboard(); });
        auto* ren = menu.addAction(tr("Rename")); ren->setEnabled(selectedIds().size() == 1);
        connect(ren, &QAction::triggered, this, [this]() { renameSelection(); });
        auto* fav = menu.addAction(tr("Toggle Favourite")); fav->setEnabled(has);
        connect(fav, &QAction::triggered, this, [this]() { toggleFavouriteSelection(); });

        // Add to Group ▸
        auto* grp_menu = menu.addMenu(tr("Add to Group"));
        grp_menu->setEnabled(has && m_project);
        auto* newg = grp_menu->addAction(tr("New Group…"));
        connect(newg, &QAction::triggered, this, [this]() {
            if (!m_project) return;
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("New Group"),
                tr("Group name:"), QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            QVector<uint64_t> sel;
            for (uint64_t id : selectedIds()) sel.push_back(id);
            emit groupAddAndAssignRequested(name.trimmed(), sel);   // one undoable macro
        });
        if (m_project && !m_project->contactGroups().empty()) {
            grp_menu->addSeparator();
            for (const auto& g : m_project->contactGroups()) {
                const std::string id = g.id;
                auto* a = grp_menu->addAction(QString::fromStdString(g.name));
                connect(a, &QAction::triggered, this, [this, id]() { assignSelectionToGroup(id); });
            }
        }
        grp_menu->addSeparator();
        auto* rem = grp_menu->addAction(tr("Remove from Group"));
        connect(rem, &QAction::triggered, this, [this]() { assignSelectionToGroup(std::string()); });

        auto* rm = menu.addAction(tr("Delete")); rm->setEnabled(has);
        connect(rm, &QAction::triggered, this, [this]() { deleteSelection(); });
        menu.exec(globalPos);
    };
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this,
            [this, showCtxMenu](const QPoint& pos) { showCtxMenu(m_table->viewport()->mapToGlobal(pos)); });
    m_thumbs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_thumbs, &QListWidget::customContextMenuRequested, this,
            [this, showCtxMenu](const QPoint& pos) { showCtxMenu(m_thumbs->viewport()->mapToGlobal(pos)); });

    updateCommandState();
    updateStatus();
    updatePreview();
}

void ContactManagerWindow::setProject(app::Project* project)
{
    m_project = project;
    refresh();
}

void ContactManagerWindow::refresh()
{
    rebuildNav();
    populateForCurrentNode();
    updateBreadcrumb();
    updateCommandState();
    updateStatus();
    updatePreview();
    if (m_editor) m_editor->refresh(m_project);
}

} // namespace dolphin::ui
