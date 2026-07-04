// ContactManagerWindow.Layout.cpp — the navigation toolbar, command toolbar, and
// preview pane construction.
#include "ui/features/contacts/ContactManagerWindow.h"
#include "ui/features/contacts/ContactVisuals.h"
#include "ui/shell/Theme.h"

#include <QActionGroup>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace dolphin::ui {

using namespace dolphin::ui::cmvis;

void ContactManagerWindow::buildNavBar()
{
    auto* bar = addToolBar(tr("Navigation"));
    bar->setObjectName("contactNavBar");
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);   // arrow glyphs as icons

    m_act_back = bar->addAction(QStringLiteral("←"));
    m_act_back->setToolTip(tr("Back"));
    m_act_back->setShortcut(QKeySequence::Back);
    connect(m_act_back, &QAction::triggered, this, [this]() { navBack(); });

    m_act_fwd = bar->addAction(QStringLiteral("→"));
    m_act_fwd->setToolTip(tr("Forward"));
    m_act_fwd->setShortcut(QKeySequence::Forward);
    connect(m_act_fwd, &QAction::triggered, this, [this]() { navForward(); });

    m_act_up = bar->addAction(QStringLiteral("↑"));
    m_act_up->setToolTip(tr("Up to parent folder"));
    m_act_up->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    connect(m_act_up, &QAction::triggered, this, [this]() { navUp(); });

    m_act_refresh = bar->addAction(QStringLiteral("⟳"));
    m_act_refresh->setToolTip(tr("Refresh"));
    m_act_refresh->setShortcut(QKeySequence::Refresh);
    connect(m_act_refresh, &QAction::triggered, this, [this]() { refresh(); });

    bar->addSeparator();

    // Wide folder-link (breadcrumb) bar — expands to fill the row.
    m_breadcrumb = new QLabel(tr("Contacts"), bar);
    m_breadcrumb->setObjectName("contactBreadcrumb");
    m_breadcrumb->setTextFormat(Qt::RichText);
    m_breadcrumb->setOpenExternalLinks(false);
    m_breadcrumb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(m_breadcrumb, &QLabel::linkActivated, this, [this](const QString& href) {
        const int i = href.toInt();
        if (i >= 0 && i < m_crumb_items.size() && m_crumb_items[i])
            m_nav->setCurrentItem(m_crumb_items[i]);
    });
    bar->addWidget(m_breadcrumb);

    m_search = new QLineEdit(bar);
    m_search->setObjectName("contactSearch");
    m_search->setPlaceholderText(tr("Search contacts…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(280);
    bar->addWidget(m_search);
}

void ContactManagerWindow::buildCommandBar()
{
    auto* bar = addToolBar(tr("Commands"));
    bar->setObjectName("contactCmdBar");
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->setIconSize(QSize(16, 16));

    m_act_cut = bar->addAction(tr("Cut"));
    m_act_cut->setShortcut(QKeySequence::Cut);
    connect(m_act_cut, &QAction::triggered, this, [this]() { copySelection(true); });

    m_act_copy = bar->addAction(Theme::icon(":/icons/copy.svg"), tr("Copy"));
    m_act_copy->setShortcut(QKeySequence::Copy);
    connect(m_act_copy, &QAction::triggered, this, [this]() { copySelection(false); });

    m_act_paste = bar->addAction(tr("Paste"));
    m_act_paste->setShortcut(QKeySequence::Paste);
    connect(m_act_paste, &QAction::triggered, this, [this]() { pasteClipboard(); });

    bar->addSeparator();

    m_act_rename = bar->addAction(Theme::icon(":/icons/renumber.svg"), tr("Rename"));
    m_act_rename->setShortcut(QKeySequence(Qt::Key_F2));
    connect(m_act_rename, &QAction::triggered, this, [this]() { renameSelection(); });

    m_act_delete = bar->addAction(Theme::icon(":/icons/recycle_bin.svg"), tr("Delete"));
    m_act_delete->setShortcut(QKeySequence::Delete);
    connect(m_act_delete, &QAction::triggered, this, [this]() { deleteSelection(); });

    bar->addSeparator();

    auto* sort_btn = new QToolButton(bar);
    sort_btn->setText(tr("Sort"));
    sort_btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    sort_btn->setPopupMode(QToolButton::InstantPopup);
    auto* sort_menu = new QMenu(sort_btn);
    auto* order_grp = new QActionGroup(sort_menu);
    auto* asc  = sort_menu->addAction(tr("Ascending"));   asc->setCheckable(true);  asc->setChecked(true);
    auto* desc = sort_menu->addAction(tr("Descending"));  desc->setCheckable(true);
    order_grp->addAction(asc); order_grp->addAction(desc);
    sort_menu->addSeparator();
    const std::pair<QString, int> sort_cols[] = {
        {tr("Label"), ColLabel}, {tr("Sensor"), ColSensor}, {tr("Line / Source"), ColSource},
        {tr("Class"), ColClass}, {tr("Confidence"), ColConf},
        {tr("Depth"), ColDepth}, {tr("Range"), ColRange},
    };
    for (const auto& [name, col] : sort_cols) {
        auto* a = sort_menu->addAction(name);
        connect(a, &QAction::triggered, this, [this, col, asc]() {
            m_table->sortItems(col, asc->isChecked() ? Qt::AscendingOrder : Qt::DescendingOrder);
        });
    }
    sort_btn->setMenu(sort_menu);
    bar->addWidget(sort_btn);

    auto* view_btn = new QToolButton(bar);
    view_btn->setText(tr("View"));
    view_btn->setIcon(Theme::icon(":/icons/layout_customize.svg"));
    view_btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    view_btn->setPopupMode(QToolButton::InstantPopup);
    auto* view_menu = new QMenu(view_btn);
    view_menu->addSection(tr("Layout"));
    auto* layout_grp = new QActionGroup(view_menu);
    m_view_details_act = view_menu->addAction(tr("Details"));
    m_view_details_act->setCheckable(true);
    m_view_details_act->setChecked(true);
    connect(m_view_details_act, &QAction::triggered, this, [this]() { setViewMode(0); });
    m_view_thumbs_act = view_menu->addAction(tr("Thumbnails"));
    m_view_thumbs_act->setCheckable(true);
    connect(m_view_thumbs_act, &QAction::triggered, this, [this]() { setViewMode(1); });
    layout_grp->addAction(m_view_details_act);
    layout_grp->addAction(m_view_thumbs_act);
    view_menu->addSection(tr("Columns"));
    const std::pair<QString, int> view_cols[] = {
        {tr("Sensor"), ColSensor}, {tr("Line / Source"), ColSource}, {tr("Class"), ColClass},
        {tr("Confidence"), ColConf}, {tr("Lat"), ColLat}, {tr("Lon"), ColLon},
        {tr("Depth"), ColDepth}, {tr("Range"), ColRange},
    };
    for (const auto& [name, col] : view_cols) {
        auto* a = view_menu->addAction(name);
        a->setCheckable(true);
        a->setChecked(true);
        connect(a, &QAction::toggled, this, [this, col](bool on) { m_table->setColumnHidden(col, !on); });
    }
    view_btn->setMenu(view_menu);
    bar->addWidget(view_btn);

    m_act_fav = bar->addAction(tr("★ Favourite"));
    connect(m_act_fav, &QAction::triggered, this, [this]() { toggleFavouriteSelection(); });

    auto* spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    m_act_export = bar->addAction(Theme::icon(":/icons/export_csv.svg"), tr("Export"));
    m_act_export->setToolTip(tr("Export the listed contacts as CSV, PDF, or Word"));
    connect(m_act_export, &QAction::triggered, this, [this]() { exportContacts(); });

    auto* more_btn = new QToolButton(bar);
    more_btn->setIcon(Theme::icon(":/icons/more.svg"));
    more_btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    more_btn->setToolTip(tr("More commands"));
    more_btn->setPopupMode(QToolButton::InstantPopup);
    auto* more = new QMenu(more_btn);
    auto* sel_all = more->addAction(tr("Select All"));
    sel_all->setShortcut(QKeySequence::SelectAll);
    connect(sel_all, &QAction::triggered, this, [this]() {
        if (m_view_mode == 1) m_thumbs->selectAll(); else m_table->selectAll(); });
    auto* sel_none = more->addAction(tr("Select None"));
    connect(sel_none, &QAction::triggered, this, [this]() {
        if (m_view_mode == 1) m_thumbs->clearSelection(); else m_table->clearSelection(); });
    auto* sel_inv = more->addAction(tr("Invert Selection"));
    connect(sel_inv, &QAction::triggered, this, [this]() { invertSelection(); });
    more->addSeparator();
    m_act_props = more->addAction(Theme::icon(":/icons/properties.svg"), tr("Properties"));
    connect(m_act_props, &QAction::triggered, this, [this]() {
        const uint64_t id = currentRowId(); if (id) openContactEditor(id); });
    more->addSeparator();
    auto* opt_menu = more->addMenu(tr("Options"));
    auto* opt_map = opt_menu->addAction(tr("Show \"Map / unlinked\" folder"));
    opt_map->setCheckable(true); opt_map->setChecked(m_show_map_folder);
    connect(opt_map, &QAction::toggled, this, [this](bool on) { m_show_map_folder = on; rebuildNav(); });
    auto* opt_confirm = opt_menu->addAction(tr("Confirm before delete"));
    opt_confirm->setCheckable(true); opt_confirm->setChecked(m_confirm_delete);
    connect(opt_confirm, &QAction::toggled, this, [this](bool on) { m_confirm_delete = on; });
    more_btn->setMenu(more);
    bar->addWidget(more_btn);
}

QWidget* ContactManagerWindow::buildPreviewPane()
{
    auto* pane = new QWidget(this);
    pane->setObjectName("contactPreview");
    pane->setMinimumWidth(230);
    auto* col = new QVBoxLayout(pane);
    col->setContentsMargins(Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4);
    col->setSpacing(Theme::kSpacing3);

    m_pv_stack = new QStackedWidget(pane);

    auto* empty = new QLabel(tr("Select a contact to see details"), m_pv_stack);
    empty->setObjectName("contactPreviewEmpty");
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    m_pv_stack->addWidget(empty);

    auto* details = new QWidget(m_pv_stack);
    auto* d = new QVBoxLayout(details);
    d->setContentsMargins(0, 0, 0, 0);
    d->setSpacing(Theme::kSpacing3);

    m_pv_title = new QLabel(details);
    m_pv_title->setObjectName("contactPreviewTitle");
    m_pv_title->setWordWrap(true);
    d->addWidget(m_pv_title);

    // Square snapshot of the contact area (grabbed at pick time). Hidden when the
    // contact has no persisted snapshot.
    m_pv_image = new QLabel(details);
    m_pv_image->setObjectName("contactPreviewImage");
    m_pv_image->setAlignment(Qt::AlignCenter);
    m_pv_image->setFixedHeight(180);
    m_pv_image->setVisible(false);
    d->addWidget(m_pv_image);

    auto* chips = new QHBoxLayout();
    chips->setSpacing(Theme::kSpacing2);
    m_pv_sensor = new QLabel(details); m_pv_sensor->setObjectName("contactChip");
    m_pv_conf   = new QLabel(details); m_pv_conf->setObjectName("contactChip");
    chips->addWidget(m_pv_sensor);
    chips->addWidget(m_pv_conf);
    chips->addStretch(1);
    d->addLayout(chips);

    auto* sep = new QFrame(details);
    sep->setObjectName("contactPreviewSep");
    sep->setFrameShape(QFrame::HLine);
    d->addWidget(sep);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(Theme::kSpacing4);
    form->setVerticalSpacing(Theme::kSpacing2);
    auto mkKey = [&](const QString& t) { auto* l = new QLabel(t, details); l->setObjectName("contactKey"); return l; };
    auto mkVal = [&]() {
        auto* l = new QLabel(details); l->setObjectName("contactVal"); l->setWordWrap(true);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse); return l;
    };
    m_pv_class  = mkVal();
    m_pv_line   = mkVal();
    m_pv_coords = mkVal();
    m_pv_depth  = mkVal();
    m_pv_range  = mkVal();
    form->addRow(mkKey(tr("Class")),    m_pv_class);
    form->addRow(mkKey(tr("Line")),     m_pv_line);
    form->addRow(mkKey(tr("Position")), m_pv_coords);
    form->addRow(mkKey(tr("Depth")),    m_pv_depth);
    form->addRow(mkKey(tr("Range")),    m_pv_range);
    d->addLayout(form);

    auto* notes_hdr = new QLabel(tr("Notes"), details);
    notes_hdr->setObjectName("contactKey");
    d->addWidget(notes_hdr);
    m_pv_notes = new QLabel(details);
    m_pv_notes->setObjectName("contactVal");
    m_pv_notes->setWordWrap(true);
    m_pv_notes->setTextInteractionFlags(Qt::TextSelectableByMouse);
    d->addWidget(m_pv_notes);

    d->addStretch(1);

    m_pv_meta = new QLabel(details);
    m_pv_meta->setObjectName("contactMeta");
    m_pv_meta->setWordWrap(true);
    d->addWidget(m_pv_meta);

    auto* goto_btn = new QPushButton(tr("Go to on Map"), details);
    goto_btn->setObjectName("dlgBtnSecondary");
    connect(goto_btn, &QPushButton::clicked, this, [this]() {
        const uint64_t id = currentRowId(); if (id) emit contactActivated(id); });
    d->addWidget(goto_btn);

    m_pv_stack->addWidget(details);
    col->addWidget(m_pv_stack);
    return pane;
}

} // namespace dolphin::ui
