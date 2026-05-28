// SBPMetadataWindow.Layout.cpp — UI construction: toolbar, field panel, chart toolbar.
#include "ui/features/metadata/SBPMetadataWindow.h"
#include "ui/features/metadata/SSSMetadataWindow.h"
#include "ui/shell/Theme.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kThickSpinW = 42;  // line-thickness spinbox width
static constexpr int kBinsSpinW  = 52;  // histogram bins spinbox width

// ─────────────────────────────────────────────────────────────────────────────
//  UI construction
// ─────────────────────────────────────────────────────────────────────────────
void SBPMetadataWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Icon toolbar ──────────────────────────────────────────────────────────
    {
        auto* toolbar = new QWidget(this);
        toolbar->setObjectName("metaToolbar");
        toolbar->setFixedHeight(Theme::kToolbarH);
        auto* tbl = new QHBoxLayout(toolbar);
        tbl->setContentsMargins(Theme::kSpacing2, 0, Theme::kSpacing2, 0);
        tbl->setSpacing(2);

        auto makeBtn = [&](const QString& icon, const QString& tip) {
            auto* btn = new QToolButton(toolbar);
            btn->setIcon(QIcon(icon));
            btn->setIconSize(QSize(16, 16));
            btn->setToolTip(tip);
            btn->setFixedSize(Theme::kMediumBtnSz, Theme::kMediumBtnSz);
            return btn;
        };

        // Export ▾ (popup menu)
        auto* btn_export = makeBtn(":/icons/export_csv.svg", "Export / Copy");
        btn_export->setPopupMode(QToolButton::InstantPopup);
        auto* exp_menu = new QMenu(btn_export);
        exp_menu->addAction("Export All to CSV…",             this, &SBPMetadataWindow::onExportAll);
        exp_menu->addAction("Export Selection to CSV…",       this, &SBPMetadataWindow::onExportSelection);
        exp_menu->addSeparator();
        exp_menu->addAction("Copy All (Tab-delimited)",       this, &SBPMetadataWindow::onCopyAll);
        exp_menu->addAction("Copy Selection (Tab-delimited)", this, &SBPMetadataWindow::onCopySelection);
        btn_export->setMenu(exp_menu);
        tbl->addWidget(btn_export);

        // Select All
        auto* btn_sel = makeBtn(":/icons/select_all.svg", "Select All  (Ctrl+A)");
        auto* act_sel = new QAction(this);
        act_sel->setShortcut(QKeySequence::SelectAll);
        addAction(act_sel);
        connect(btn_sel, &QToolButton::clicked, this, [this]{ m_table->selectAll(); });
        connect(act_sel, &QAction::triggered,   this, [this]{ m_table->selectAll(); });
        tbl->addWidget(btn_sel);

        // Copy selection
        auto* btn_copy = makeBtn(":/icons/copy.svg", "Copy Selection  (Ctrl+C)");
        auto* act_copy = new QAction(this);
        act_copy->setShortcut(QKeySequence::Copy);
        addAction(act_copy);
        connect(btn_copy, &QToolButton::clicked, this, &SBPMetadataWindow::onCopySelection);
        connect(act_copy, &QAction::triggered,   this, &SBPMetadataWindow::onCopySelection);
        tbl->addWidget(btn_copy);

        auto* sep1 = new QFrame(toolbar);
        sep1->setObjectName("metaSep");
        sep1->setFrameShape(QFrame::VLine);
        sep1->setFixedHeight(Theme::kToolbarSepH);
        tbl->addWidget(sep1);

        // Show All Fields
        auto* btn_show = makeBtn(":/icons/eye.svg", "Show All Fields");
        connect(btn_show, &QToolButton::clicked, this, [this]{
            for (int i = 0; i < m_field_list->count(); ++i)
                m_field_list->item(i)->setCheckState(Qt::Checked);
        });
        tbl->addWidget(btn_show);

        // Hide All Fields
        auto* btn_hide = makeBtn(":/icons/eye_off.svg", "Hide All Fields");
        connect(btn_hide, &QToolButton::clicked, this, [this]{
            for (int i = 0; i < m_field_list->count(); ++i)
                m_field_list->item(i)->setCheckState(Qt::Unchecked);
        });
        tbl->addWidget(btn_hide);

        tbl->addStretch(1);

        auto* sep2 = new QFrame(toolbar);
        sep2->setObjectName("metaSep");
        sep2->setFrameShape(QFrame::VLine);
        sep2->setFixedHeight(Theme::kToolbarSepH);
        tbl->addWidget(sep2);

        m_btn_toggle_chart = makeBtn(":/icons/analyze.svg", "Show / Hide Chart Panel");
        m_btn_toggle_chart->setCheckable(true);
        m_btn_toggle_chart->setChecked(true);
        connect(m_btn_toggle_chart, &QToolButton::toggled,
                this, &SBPMetadataWindow::onToggleChart);
        tbl->addWidget(m_btn_toggle_chart);

        root->addWidget(toolbar);
    }

    // ── Top bar: survey line selector ─────────────────────────────────────────
    {
        auto* bar = new QWidget(this);
        bar->setFixedHeight(Theme::kFilterBarH);
        auto* bl = new QHBoxLayout(bar);
        bl->setContentsMargins(Theme::kSpacing3, Theme::kSpacing1, Theme::kSpacing3, Theme::kSpacing1);
        bl->setSpacing(Theme::kSpacing3);

        bl->addWidget(new QLabel("Survey lines:", bar));

        m_line_menu = new QMenu(this);
        m_line_btn  = new QToolButton(bar);
        m_line_btn->setMenu(m_line_menu);
        m_line_btn->setPopupMode(QToolButton::InstantPopup);
        m_line_btn->setText("No lines ▾");
        m_line_btn->setMinimumWidth(160);
        m_line_btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        bl->addWidget(m_line_btn);

        m_load_status = new QLabel(bar);
        m_load_status->setObjectName("metaLoadStatus");
        bl->addWidget(m_load_status);
        bl->addStretch(1);
        root->addWidget(bar);
    }

    // ── Outer vertical splitter: [upper pane | chart pane] ───────────────────
    m_outer_vsplit = new QSplitter(Qt::Vertical, this);
    m_outer_vsplit->setHandleWidth(5);

    auto* upper = new QWidget(m_outer_vsplit);
    {
        auto* ul = new QVBoxLayout(upper);
        ul->setContentsMargins(0, 0, 0, 0);
        ul->setSpacing(0);

        // ── Horizontal splitter: [field panel | table] ────────────────────────
        auto* hsplit = new QSplitter(Qt::Horizontal, upper);
        hsplit->setHandleWidth(3);

        // Field panel (left)
        auto* lpanel = new QWidget(hsplit);
        lpanel->setMinimumWidth(180);
        lpanel->setMaximumWidth(240);
        buildFieldPanel(lpanel);

        // Table pane (right)
        auto* table_pane = new QWidget(hsplit);
        {
            auto* tl = new QVBoxLayout(table_pane);
            tl->setContentsMargins(0, 0, 0, 0);
            tl->setSpacing(0);

            m_table = new QTableView(table_pane);
            m_table->setModel(m_proxy);
            m_table->setSortingEnabled(true);
            m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
            m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
            m_table->setAlternatingRowColors(true);
            m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            m_table->setShowGrid(true);
            m_table->verticalHeader()->setDefaultSectionSize(18);
            m_table->horizontalHeader()->setDefaultSectionSize(90);
            m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            m_table->setContextMenuPolicy(Qt::CustomContextMenu);

            m_sel_status = new QLabel(table_pane);
            m_sel_status->setObjectName("metaSelStatus");
            m_sel_status->setFixedHeight(Theme::kStatusH);
            m_sel_status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            tl->addWidget(m_table, 1);
            tl->addWidget(m_sel_status);
        }

        hsplit->addWidget(lpanel);
        hsplit->addWidget(table_pane);
        hsplit->setStretchFactor(0, 0);
        hsplit->setStretchFactor(1, 1);

        ul->addWidget(hsplit, 1);
    }
    m_outer_vsplit->addWidget(upper);

    // ── Chart panel (bottom) ──────────────────────────────────────────────────
    m_chart_pane = new QWidget(m_outer_vsplit);
    m_chart_pane->setAttribute(Qt::WA_DeleteOnClose, false);
    m_chart_pane->installEventFilter(this);
    {
        auto* cl = new QVBoxLayout(m_chart_pane);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(0);
        buildChartToolbar(m_chart_pane);
        m_plot = new SSSMetadataPlotWidget(m_chart_pane);
        cl->addWidget(m_plot, 1);
    }
    m_outer_vsplit->addWidget(m_chart_pane);
    m_outer_vsplit->setStretchFactor(0, 3);
    m_outer_vsplit->setStretchFactor(1, 1);
    m_outer_vsplit->setSizes({520, 160});

    root->addWidget(m_outer_vsplit, 1);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_table, &QTableView::customContextMenuRequested,
            this, &SBPMetadataWindow::showTableContextMenu);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SBPMetadataWindow::onSelectionChanged);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Field panel construction
// ─────────────────────────────────────────────────────────────────────────────
void SBPMetadataWindow::buildFieldPanel(QWidget* parent)
{
    auto* vl = new QVBoxLayout(parent);
    vl->setContentsMargins(Theme::kSpacing2, Theme::kSpacing2, Theme::kSpacing2, Theme::kSpacing2);
    vl->setSpacing(Theme::kSpacing1);

    auto* hdr = new QLabel("Fields", parent);
    QFont f = hdr->font(); f.setBold(true); f.setPointSize(f.pointSize() + 1);
    hdr->setFont(f);
    vl->addWidget(hdr);

    auto* search = new QLineEdit(parent);
    search->setPlaceholderText("Search field…");
    vl->addWidget(search);

    m_field_list = new QListWidget(parent);
    for (int i = 0; i < SBPNavModel::kFieldCount; ++i) {
        QString label = SBPNavModel::kFieldDefs[i].name;
        if (SBPNavModel::kFieldDefs[i].unit[0] != '\0')
            label += QString(" (%1)").arg(SBPNavModel::kFieldDefs[i].unit);
        auto* item = new QListWidgetItem(label, m_field_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, i);
    }
    vl->addWidget(m_field_list, 1);

    auto* sep = new QFrame(parent);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    vl->addWidget(sep);

    // Per-field config
    m_cfg_panel = new QWidget(parent);
    auto* fl = new QFormLayout(m_cfg_panel);
    fl->setContentsMargins(0, 2, 0, 2);
    fl->setSpacing(Theme::kSpacing1);

    m_cfg_name = new QLabel("—", m_cfg_panel);
    QFont bf = m_cfg_name->font(); bf.setBold(true); m_cfg_name->setFont(bf);
    fl->addRow(m_cfg_name);

    m_cfg_plot_cb = new QCheckBox("Show in plot", m_cfg_panel);
    fl->addRow(m_cfg_plot_cb);

    auto* cw = new QWidget(m_cfg_panel);
    auto* crl = new QHBoxLayout(cw);
    crl->setContentsMargins(0, 0, 0, 0); crl->setSpacing(Theme::kSpacing1);
    m_cfg_color_btn = new QToolButton(cw);
    m_cfg_color_btn->setFixedSize(Theme::kSmallBtnSz, Theme::kSmallBtnSz);
    crl->addWidget(m_cfg_color_btn);
    crl->addWidget(new QLabel("Color", cw));
    crl->addStretch();
    crl->addWidget(new QLabel("Width:", cw));
    m_cfg_thick_sp = new QSpinBox(cw);
    m_cfg_thick_sp->setRange(1, 5);
    m_cfg_thick_sp->setFixedWidth(kThickSpinW);
    crl->addWidget(m_cfg_thick_sp);
    fl->addRow(cw);

    m_cfg_dots_cb = new QCheckBox("Add dots", m_cfg_panel);
    fl->addRow(m_cfg_dots_cb);

    m_cfg_prec_sp = new QSpinBox(m_cfg_panel);
    m_cfg_prec_sp->setRange(0, 9);
    fl->addRow("Precision:", m_cfg_prec_sp);

    m_cfg_panel->setEnabled(false);
    vl->addWidget(m_cfg_panel);

    connect(search, &QLineEdit::textChanged,
            this, &SBPMetadataWindow::onSearchTextChanged);
    connect(m_field_list, &QListWidget::itemChanged,
            this, [this](QListWidgetItem*){ onFieldListItemChanged(); });
    connect(m_field_list, &QListWidget::currentRowChanged,
            this, &SBPMetadataWindow::onFieldListCurrentChanged);
    connect(m_cfg_plot_cb,   &QCheckBox::toggled,   this, &SBPMetadataWindow::onShowInPlotToggled);
    connect(m_cfg_color_btn, &QToolButton::clicked, this, &SBPMetadataWindow::onColorButtonClicked);
    connect(m_cfg_thick_sp,  QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SBPMetadataWindow::onThicknessChanged);
    connect(m_cfg_dots_cb,   &QCheckBox::toggled,   this, &SBPMetadataWindow::onDotsToggled);
    connect(m_cfg_prec_sp,   QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SBPMetadataWindow::onPrecisionChanged);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Chart toolbar construction
// ─────────────────────────────────────────────────────────────────────────────
void SBPMetadataWindow::buildChartToolbar(QWidget* parent)
{
    auto* bar = new QWidget(parent);
    bar->setFixedHeight(Theme::kDialogBtnH);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(Theme::kSpacing2, 2, Theme::kSpacing2, 2);
    bl->setSpacing(Theme::kSpacing2);

    bl->addWidget(new QLabel("Chart:", bar));
    m_chart_type_cb = new QComboBox(bar);
    m_chart_type_cb->addItems({"Line", "Scatter", "Histogram"});
    bl->addWidget(m_chart_type_cb);

    bl->addWidget(new QLabel("|", bar));

    m_chart_x_lbl = new QLabel("X:", bar);
    bl->addWidget(m_chart_x_lbl);
    m_chart_x_cb = new QComboBox(bar);
    m_chart_x_cb->setMinimumWidth(100);
    bl->addWidget(m_chart_x_cb);

    bl->addWidget(new QLabel("Y:", bar));
    m_chart_y_cb = new QComboBox(bar);
    m_chart_y_cb->setMinimumWidth(100);
    bl->addWidget(m_chart_y_cb);

    m_chart_bins_lbl = new QLabel("Bins:", bar);
    bl->addWidget(m_chart_bins_lbl);
    m_chart_bins_sp = new QSpinBox(bar);
    m_chart_bins_sp->setRange(5, 200);
    m_chart_bins_sp->setValue(30);
    m_chart_bins_sp->setFixedWidth(kBinsSpinW);
    bl->addWidget(m_chart_bins_sp);

    m_btn_undock_chart = new QToolButton(bar);
    m_btn_undock_chart->setObjectName("metaUndockBtn");
    m_btn_undock_chart->setToolTip("Pop out chart  /  Dock back");
    m_btn_undock_chart->setFixedSize(Theme::kSmallBtnSz, Theme::kSmallBtnSz);
    m_btn_undock_chart->setText("⬜");
    connect(m_btn_undock_chart, &QToolButton::clicked,
            this, &SBPMetadataWindow::onUndockChart);
    bl->addWidget(m_btn_undock_chart);
    bl->addStretch(1);

    for (int i = 0; i < SBPNavModel::kFieldCount; ++i) {
        const QString name = SBPNavModel::kFieldDefs[i].name;
        m_chart_x_cb->addItem(name, i);
        m_chart_y_cb->addItem(name, i);
    }
    m_chart_x_cb->setCurrentIndex(0);
    m_chart_y_cb->setCurrentIndex(4);   // heading

    parent->layout()->addWidget(bar);

    connect(m_chart_type_cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SBPMetadataWindow::onChartTypeChanged);
    connect(m_chart_x_cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SBPMetadataWindow::onChartXChanged);
    connect(m_chart_y_cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SBPMetadataWindow::onChartYChanged);
    connect(m_chart_bins_sp, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SBPMetadataWindow::onChartBinsChanged);

    onChartTypeChanged(0);
}

} // namespace dolphin::ui
