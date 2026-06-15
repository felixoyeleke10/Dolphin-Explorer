// DataLibraryWindow.Layout.cpp — menu bar, nav bar, filter/format panels, page builders.

#include "ui/features/datalibrary/DataLibraryWindow.h"
#include "ui/shared/UiUtils.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QRadioButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

// -- Menu bar ------------------------------------------------------------------

void DataLibraryWindow::buildMenuBar()
{
    auto* file = menuBar()->addMenu(tr("File"));
    auto* close = new QAction(tr("Close"), this);
    close->setShortcut(QKeySequence::Close);
    connect(close, &QAction::triggered, this, &QWidget::close);
    file->addAction(close);

    auto* view = menuBar()->addMenu(tr("View"));
    auto* refresh = new QAction(tr("Refresh"), this);
    refresh->setShortcut(QKeySequence::Refresh);
    connect(refresh, &QAction::triggered, this, &DataLibraryWindow::refreshAll);
    view->addAction(refresh);
    view->addSeparator();

    auto* compact = new QAction(tr("Compact Rows"), this);
    compact->setCheckable(true);
    connect(compact, &QAction::toggled, this, [this](bool on) {
        m_compact_rows = on;
        refreshAll();
    });
    view->addAction(compact);
}

// -- Navigation toolbar --------------------------------------------------------
// Tab buttons + modality switcher dropdown.

void DataLibraryWindow::buildNavBar()
{
    auto* tb = addToolBar(tr("Navigation"));
    tb->setObjectName("dataLibraryNavBar");
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto* grp = new QButtonGroup(tb);
    grp->setExclusive(true);

    auto addTab = [&](QToolButton*& btn, const char* label) {
        btn = new QToolButton(tb);
        btn->setText(tr(label));
        btn->setCheckable(true);
        grp->addButton(btn);
        tb->addWidget(btn);
    };
    addTab(m_tab_layers,   "Layers");
    addTab(m_tab_contacts, "Contacts");
    addTab(m_tab_issues,   "Issues");
    m_tab_layers->setChecked(true);

    connect(m_tab_layers,   &QToolButton::clicked, this, [this]() { switchPage(PageLayers);   });
    connect(m_tab_contacts, &QToolButton::clicked, this, [this]() { switchPage(PageContacts); });
    connect(m_tab_issues,   &QToolButton::clicked, this, [this]() { switchPage(PageIssues);   });

    tb->addSeparator();

    // Modality switcher
    m_modality_btn = new QToolButton(tb);
    m_modality_btn->setText(tr("All Types"));
    m_modality_btn->setPopupMode(QToolButton::InstantPopup);
    m_modality_btn->setToolTip(tr("Filter by data type"));

    auto* mod_menu = new QMenu(m_modality_btn);

    struct { app::Modality mod; const char* label; } kMods[] = {
        { app::Modality::Sidescan,     "Side Scan"    },
        { app::Modality::SubBottom,    "Sub-Bottom"   },
        { app::Modality::Magnetometer, "Magnetometer" },
        { app::Modality::Multibeam,    "Multibeam"    },
    };

    auto* act_all = mod_menu->addAction(tr("All Types"));
    act_all->setCheckable(true);
    act_all->setChecked(true);
    connect(act_all, &QAction::triggered, this, [this, mod_menu, act_all]() {
        for (auto* a : mod_menu->actions()) a->setChecked(false);
        act_all->setChecked(true);
        m_modality_btn->setText(tr("All Types"));
        setModalityFilter(-1);
    });

    mod_menu->addSeparator();

    for (auto [mod, label] : kMods) {
        auto* act = mod_menu->addAction(tr(label));
        act->setCheckable(true);
        const int mod_int = static_cast<int>(mod);
        connect(act, &QAction::triggered, this, [this, mod_menu, act, label, mod_int]() {
            for (auto* a : mod_menu->actions()) a->setChecked(false);
            act->setChecked(true);
            m_modality_btn->setText(tr(label));
            setModalityFilter(mod_int);
        });
    }

    m_modality_btn->setMenu(mod_menu);
    tb->addWidget(m_modality_btn);
}

// -- Left panel — Filters ------------------------------------------------------

QWidget* DataLibraryWindow::buildFilterPanel()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(200);

    auto* panel = new QWidget;
    auto* vlay  = new QVBoxLayout(panel);
    vlay->setContentsMargins(8, 8, 8, 8);
    vlay->setSpacing(4);

    // -- Header ---------------------------------------------------------------
    {
        auto* row   = new QHBoxLayout;
        auto* title = new QLabel(tr("Filters"), panel);
        QFont f = title->font(); f.setBold(true); title->setFont(f);
        row->addWidget(title);
        row->addStretch();
        auto* rst = new QToolButton(panel);
        rst->setText(QString(QChar(0x2715)));
        rst->setToolTip(tr("Clear all filters"));
        rst->setAutoRaise(true);
        connect(rst, &QToolButton::clicked, this, [this]() {
            if (m_text_chk)        m_text_chk->setChecked(true);
            if (m_filter_text)     m_filter_text->clear();
            if (m_search_in_name)  m_search_in_name->setChecked(true);
            if (m_search_in_src)   m_search_in_src->setChecked(true);
            if (m_state_ready)     m_state_ready->setChecked(true);
            if (m_state_failed)    m_state_failed->setChecked(true);
            if (m_state_pending)   m_state_pending->setChecked(true);
        });
        row->addWidget(rst);
        vlay->addLayout(row);
    }

    // -- Match ANY / ALL -------------------------------------------------------
    {
        auto* grp      = new QButtonGroup(panel);
        m_match_any    = new QRadioButton(tr("Match ANY set of rules"), panel);
        auto* match_all = new QRadioButton(tr("Match ALL sets of rules"), panel);
        match_all->setChecked(true);
        grp->addButton(m_match_any);
        grp->addButton(match_all);
        connect(m_match_any, &QRadioButton::toggled,
                this, &DataLibraryWindow::applyFilters);
        vlay->addWidget(m_match_any);
        vlay->addWidget(match_all);
    }

    auto addSep = [&]() {
        vlay->addSpacing(4);
        auto* sep = new QFrame(panel); sep->setFrameShape(QFrame::HLine);
        vlay->addWidget(sep);
        vlay->addSpacing(2);
    };

    // -- Text section ----------------------------------------------------------
    addSep();
    {
        m_text_chk = new QCheckBox(tr("Text"), panel);
        m_text_chk->setChecked(true);
        QFont f = m_text_chk->font(); f.setBold(true); m_text_chk->setFont(f);
        connect(m_text_chk, &QCheckBox::toggled,
                this, &DataLibraryWindow::applyFilters);
        vlay->addWidget(m_text_chk);

        m_filter_text = new QLineEdit(panel);
        m_filter_text->setPlaceholderText(tr("Search..."));
        m_filter_text->setClearButtonEnabled(true);
        connect(m_filter_text, &QLineEdit::textChanged,
                this, [this](const QString&) { applyFilters(); });
        vlay->addWidget(m_filter_text);

        auto* sub = new QHBoxLayout;
        sub->setContentsMargins(2, 0, 0, 0);
        m_search_in_name = new QCheckBox(tr("Name"),   panel);
        m_search_in_src  = new QCheckBox(tr("Source"), panel);
        m_search_in_name->setChecked(true);
        m_search_in_src->setChecked(true);
        connect(m_search_in_name, &QCheckBox::toggled,
                this, &DataLibraryWindow::applyFilters);
        connect(m_search_in_src,  &QCheckBox::toggled,
                this, &DataLibraryWindow::applyFilters);
        sub->addWidget(m_search_in_name);
        sub->addWidget(m_search_in_src);
        sub->addStretch();
        vlay->addLayout(sub);
    }

    // -- Status section --------------------------------------------------------
    addSep();
    {
        auto* lbl = new QLabel(tr("Status"), panel);
        QFont f = lbl->font(); f.setBold(true); lbl->setFont(f);
        vlay->addWidget(lbl);

        auto addStatus = [&](QCheckBox*& chk, const char* label) {
            chk = new QCheckBox(tr(label), panel);
            chk->setChecked(true);
            connect(chk, &QCheckBox::toggled,
                    this, &DataLibraryWindow::applyFilters);
            vlay->addWidget(chk);
        };
        addStatus(m_state_ready,   "Ready");
        addStatus(m_state_failed,  "Failed");
        addStatus(m_state_pending, "Pending / Indexing");
    }

    vlay->addStretch();
    scroll->setWidget(panel);
    return scroll;
}

// -- Right panel — Formats (column visibility) ---------------------------------

QWidget* DataLibraryWindow::buildFormatsPanel()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(200);

    auto* panel = new QWidget;
    auto* vlay  = new QVBoxLayout(panel);
    vlay->setContentsMargins(8, 8, 8, 8);
    vlay->setSpacing(4);

    // Header
    auto* title = new QLabel(tr("Formats"), panel);
    QFont f = title->font(); f.setBold(true); title->setFont(f);
    vlay->addWidget(title);

    // Search field
    auto* search = new QLineEdit(panel);
    search->setPlaceholderText(tr("Search field..."));
    search->setClearButtonEnabled(true);
    vlay->addWidget(search);

    // Column checklist (populated from m_layers_table, which is already built)
    m_col_list = new QListWidget(panel);
    m_col_list->setFrameShape(QFrame::NoFrame);
    m_col_list->setUniformItemSizes(true);

    for (int c = 0; c < m_layers_table->columnCount(); ++c) {
        const QString name = m_layers_table->horizontalHeaderItem(c)
            ? m_layers_table->horizontalHeaderItem(c)->text().toUpper()
            : QString::number(c);
        auto* item = new QListWidgetItem(name, m_col_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, c);
    }
    vlay->addWidget(m_col_list);

    connect(search, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString needle = text.trimmed().toLower();
        for (int i = 0; i < m_col_list->count(); ++i) {
            auto* item = m_col_list->item(i);
            item->setHidden(!needle.isEmpty()
                            && !item->text().toLower().contains(needle));
        }
    });
    connect(m_col_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        m_layers_table->setColumnHidden(item->data(Qt::UserRole).toInt(),
                                        item->checkState() != Qt::Checked);
    });

    // -- Bottom icon buttons ---------------------------------------------------
    auto* sep = new QFrame(panel); sep->setFrameShape(QFrame::HLine);
    vlay->addWidget(sep);

    auto* btn_row = new QHBoxLayout;
    btn_row->setContentsMargins(0, 2, 0, 2);
    btn_row->setSpacing(2);

    auto makeTb = [&](QChar icon, const char* tip) {
        auto* b = new QToolButton(panel);
        b->setText(QString(icon));
        b->setToolTip(tr(tip));
        b->setAutoRaise(true);
        btn_row->addWidget(b);
        return b;
    };
    auto* btn_all   = makeTb(QChar(0x2611), "Show all columns");
    auto* btn_none  = makeTb(QChar(0x2610), "Hide all columns");
    auto* btn_reset = makeTb(QChar(0x21ba), "Reset to defaults");
    btn_row->addStretch();
    vlay->addLayout(btn_row);

    connect(btn_all, &QToolButton::clicked, this, [this]() {
        for (int i = 0; i < m_col_list->count(); ++i)
            m_col_list->item(i)->setCheckState(Qt::Checked);
    });
    connect(btn_none, &QToolButton::clicked, this, [this]() {
        for (int i = 1; i < m_col_list->count(); ++i)
            m_col_list->item(i)->setCheckState(Qt::Unchecked);
    });
    connect(btn_reset, &QToolButton::clicked, this, [this]() {
        for (int i = 0; i < m_col_list->count(); ++i)
            m_col_list->item(i)->setCheckState(Qt::Checked);
    });

    vlay->addStretch();
    scroll->setWidget(panel);
    return scroll;
}

// -- Page builders -------------------------------------------------------------

void DataLibraryWindow::buildLayersPage()
{
    auto* page = new QWidget(m_stack);
    auto* lay  = makeCompactLayout<QVBoxLayout>(page);

    // Columns: Name | Format | State | Source File | Pings | Duration |
    //          Start UTC | SOL | EOL | Contacts | Bottom Track | Size
    m_layers_table = new QTableWidget(page);
    m_layers_table->setObjectName("databaseTable");
    m_layers_table->setColumnCount(12);
    m_layers_table->setHorizontalHeaderLabels({
        tr("Name"),          tr("Format"),
        tr("State"),         tr("Source File"),
        tr("Pings"),         tr("Duration"),
        tr("Start UTC"),     tr("SOL"),
        tr("EOL"),           tr("Contacts"),
        tr("Bottom Track"),  tr("Size")
    });
    m_layers_table->horizontalHeader()->setStretchLastSection(false);
    m_layers_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_layers_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_layers_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_layers_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_layers_table->setAlternatingRowColors(false);
    m_layers_table->verticalHeader()->setVisible(false);
    m_layers_table->setFrameShape(QFrame::NoFrame);
    m_layers_table->setSortingEnabled(true);

    lay->addWidget(m_layers_table);
    m_stack->addWidget(page);

    connect(m_layers_table, &QTableWidget::cellDoubleClicked,
            this, [this](int row, int) {
        auto* item = m_layers_table->item(row, 0);
        if (item) emit layerActivated(item->data(Qt::UserRole).toString().toStdString());
    });
}

void DataLibraryWindow::buildContactsPage()
{
    auto* page = new QWidget(m_stack);
    auto* lay  = makeCompactLayout<QVBoxLayout>(page);

    m_contacts_table = new QTableWidget(page);
    m_contacts_table->setObjectName("databaseTable");
    m_contacts_table->setColumnCount(9);
    m_contacts_table->setHorizontalHeaderLabels({
        tr("#"), tr("Label"), tr("Lat"), tr("Lon"),
        tr("Class"), tr("Confidence"), tr("Range (m)"), tr("Depth (m)"), tr("Line")
    });
    auto* hdr = m_contacts_table->horizontalHeader();
    hdr->setSectionResizeMode(1, QHeaderView::Stretch);
    hdr->setSectionResizeMode(4, QHeaderView::Stretch);
    hdr->setSectionResizeMode(8, QHeaderView::Stretch);
    m_contacts_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_contacts_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_contacts_table->setAlternatingRowColors(false);
    m_contacts_table->verticalHeader()->setVisible(false);
    m_contacts_table->setFrameShape(QFrame::NoFrame);

    lay->addWidget(m_contacts_table);
    m_stack->addWidget(page);

    connect(m_contacts_table, &QTableWidget::cellDoubleClicked,
            this, [this](int row, int) {
        auto* item = m_contacts_table->item(row, 0);
        if (item) emit contactActivated(item->data(Qt::UserRole).toULongLong());
    });
}

void DataLibraryWindow::buildIssuesPage()
{
    auto* page = new QWidget(m_stack);
    auto* lay  = makeCompactLayout<QVBoxLayout>(page);

    m_issues_table = new QTableWidget(page);
    m_issues_table->setObjectName("databaseTable");
    m_issues_table->setColumnCount(5);
    m_issues_table->setHorizontalHeaderLabels({
        tr("Severity"), tr("Category"), tr("Item"),
        tr("Problem"),  tr("Suggested Fix")
    });
    auto* hdr = m_issues_table->horizontalHeader();
    hdr->setSectionResizeMode(2, QHeaderView::Stretch);
    hdr->setSectionResizeMode(3, QHeaderView::Stretch);
    hdr->setSectionResizeMode(4, QHeaderView::Stretch);
    m_issues_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_issues_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_issues_table->setAlternatingRowColors(false);
    m_issues_table->verticalHeader()->setVisible(false);
    m_issues_table->setFrameShape(QFrame::NoFrame);

    lay->addWidget(m_issues_table);
    m_stack->addWidget(page);

    connect(m_issues_table, &QTableWidget::cellDoubleClicked,
            this, [this](int row, int) {
        auto* item = m_issues_table->item(row, 2);
        if (!item) return;
        const QString lid = item->data(Qt::UserRole).toString();
        if (!lid.isEmpty()) emit layerActivated(lid.toStdString());
    });
}

} // namespace dolphin::ui
