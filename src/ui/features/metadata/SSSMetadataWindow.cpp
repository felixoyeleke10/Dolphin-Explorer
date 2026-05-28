// SSSMetadataWindow.cpp — window shell, UI construction, selection status.
// Companion files:
//   SSSMetadataChart.cpp     — buildChartToolbar, toggle/dock, chart update slots
//   SSSNavModel.cpp          — field defs + SSSNavModel implementation
//   SSSMetadataPlotWidget.cpp — PlotMetrics + SSSMetadataPlotWidget
//   SSSMetadataLoad.cpp      — setProject, startLoad, onPingsLoaded, applyVisibleFilter
//   SSSMetadataFieldPanel.cpp — buildFieldPanel + per-field config slots
//   SSSMetadataExport.cpp    — buildTabText, exportToCsv, keyboard, context menu

#include "ui/features/metadata/SSSMetadataWindow.h"
#include "ui/shell/Theme.h"

#include <QAction>
#include <QCheckBox>
#include <QTimer>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QSpinBox>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace dolphin::ui {

static constexpr int kInitW = 1200;
static constexpr int kInitH = 740;

static const QColor kDefaultColors[SSSNavModel::kFieldCount] = {
    // ── Identification ─────────────────────────────────────────────────────────
    {0x88,0x88,0x88}, {0x68,0x88,0x68},   // ping #, ping id
    {0x38,0xa8,0x58},                      // channel
    // ── Timestamps ─────────────────────────────────────────────────────────────
    {0x88,0x88,0x88}, {0x88,0x88,0x98},   // time, nav time
    // ── Resolved position ──────────────────────────────────────────────────────
    {0x48,0xa8,0xe8}, {0x48,0xe8,0xa8},   // lat, lon
    // ── Raw source positions ───────────────────────────────────────────────────
    {0x28,0x88,0xc8}, {0x28,0xc8,0x88},   // fish lat, fish lon
    {0x18,0x68,0xa8}, {0x18,0xa8,0x68},   // vessel lat, vessel lon
    // ── Depth / dynamics ───────────────────────────────────────────────────────
    {0xc8,0x88,0x38}, {0x58,0xc8,0x68},   // depth, altitude
    {0xe8,0x58,0x58}, {0x58,0x88,0xe8},   // roll, pitch
    // ── Heading sources ────────────────────────────────────────────────────────
    {0xe8,0xc8,0x48}, {0xc8,0xa8,0x38},   // heading, sensor hdg
    {0xa8,0x88,0x58},                      // ship hdg
    // ── Motion ─────────────────────────────────────────────────────────────────
    {0x98,0x58,0xe8}, {0x48,0xe8,0xe8},   // speed, heave
    // ── Sonar geometry ─────────────────────────────────────────────────────────
    {0xe8,0x88,0x48}, {0xa8,0xa8,0x48},   // slant range, sample rate
    {0x88,0x48,0x48}, {0x48,0x88,0x48},   // blanking, layback
    {0x88,0x48,0x88}, {0x58,0xa8,0x78},   // cable out, fish dx
    {0x78,0x58,0xa8}, {0xa8,0x78,0x48},   // fish dy, kp
    // ── Acoustics ──────────────────────────────────────────────────────────────
    {0xe8,0x48,0x88}, {0x48,0xc8,0x98},   // frequency, SV
    {0x68,0x88,0xc8},                      // bandwidth
    // ── Gain / calibration ─────────────────────────────────────────────────────
    {0xa8,0x78,0x78}, {0x78,0xa8,0x78},   // gain, init gain
    {0x78,0x78,0xa8},                      // volt scale
    // ── Bottom / QC ────────────────────────────────────────────────────────────
    {0x68,0x58,0xa8},                      // samples
    {0xe8,0x78,0x58}, {0xc8,0x98,0x48},   // btm range, btm conf
    {0xe8,0x48,0x48}, {0xe8,0x88,0x28},   // qc flags, corrections
};

// ─────────────────────────────────────────────────────────────────────────────
//  SSSMetadataWindow — construction
// ─────────────────────────────────────────────────────────────────────────────

SSSMetadataWindow::SSSMetadataWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("SSS Survey Data");
    resize(kInitW, kInitH);

    m_field_cfg.resize(SSSNavModel::kFieldCount);
    for (int i = 0; i < SSSNavModel::kFieldCount; ++i)
        m_field_cfg[i].color = kDefaultColors[i];

    m_model = new SSSNavModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole + 1);

    m_load_debounce = new QTimer(this);
    m_load_debounce->setSingleShot(true);
    m_load_debounce->setInterval(150);
    connect(m_load_debounce, &QTimer::timeout, this, &SSSMetadataWindow::startLoad);

    buildUi();
}

// ─────────────────────────────────────────────────────────────────────────────
//  UI construction
// ─────────────────────────────────────────────────────────────────────────────

void SSSMetadataWindow::buildUi()
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
        exp_menu->addAction("Export All to CSV…",             this, &SSSMetadataWindow::onExportAll);
        exp_menu->addAction("Export Selection to CSV…",       this, &SSSMetadataWindow::onExportSelection);
        exp_menu->addSeparator();
        exp_menu->addAction("Copy All (Tab-delimited)",       this, &SSSMetadataWindow::onCopyAll);
        exp_menu->addAction("Copy Selection (Tab-delimited)", this, &SSSMetadataWindow::onCopySelection);
        btn_export->setMenu(exp_menu);
        tbl->addWidget(btn_export);

        // Select All
        auto* btn_sel = makeBtn(":/icons/select_all.svg", "Select All  (Ctrl+A)");
        auto* act_sel_all = new QAction(this);
        act_sel_all->setShortcut(QKeySequence::SelectAll);
        addAction(act_sel_all);
        connect(btn_sel,     &QToolButton::clicked, this, [this]{ m_table->selectAll(); });
        connect(act_sel_all, &QAction::triggered,   this, [this]{ m_table->selectAll(); });
        tbl->addWidget(btn_sel);

        // Copy selection
        auto* btn_copy = makeBtn(":/icons/copy.svg", "Copy Selection  (Ctrl+C)");
        auto* act_copy = new QAction(this);
        act_copy->setShortcut(QKeySequence::Copy);
        addAction(act_copy);
        connect(btn_copy, &QToolButton::clicked, this, &SSSMetadataWindow::onCopySelection);
        connect(act_copy, &QAction::triggered,   this, &SSSMetadataWindow::onCopySelection);
        tbl->addWidget(btn_copy);

        auto* sep = new QFrame(toolbar);
        sep->setObjectName("metaSep");
        sep->setFrameShape(QFrame::VLine);
        sep->setFixedHeight(Theme::kToolbarSepH);
        tbl->addWidget(sep);

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
                this, &SSSMetadataWindow::onToggleChart);
        tbl->addWidget(m_btn_toggle_chart);

        root->addWidget(toolbar);
    }

    // ── Top bar ───────────────────────────────────────────────────────────────
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

        m_visible_only_cb = new QCheckBox("Visible pings only", bar);
        bl->addWidget(m_visible_only_cb);

        root->addWidget(bar);
    }

    // ── Horizontal splitter: table-pane | field panel ─────────────────────────
    m_outer_vsplit = new QSplitter(Qt::Vertical, this);
    auto* outer_vsplit = m_outer_vsplit;
    outer_vsplit->setHandleWidth(5);

    auto* upper = new QWidget(outer_vsplit);
    {
        auto* ul = new QVBoxLayout(upper);
        ul->setContentsMargins(0, 0, 0, 0);
        ul->setSpacing(0);

        auto* hsplit = new QSplitter(Qt::Horizontal, upper);
        hsplit->setHandleWidth(3);

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
            m_table->horizontalHeader()->setDefaultSectionSize(88);
            m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            m_table->setContextMenuPolicy(Qt::CustomContextMenu);

            m_sel_status = new QLabel(table_pane);
            m_sel_status->setObjectName("metaSelStatus");
            m_sel_status->setFixedHeight(Theme::kStatusH);
            m_sel_status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            tl->addWidget(m_table, 1);
            tl->addWidget(m_sel_status);
        }

        auto* lpanel = new QWidget(hsplit);
        lpanel->setMinimumWidth(180);
        lpanel->setMaximumWidth(240);
        buildFieldPanel(lpanel);
        hsplit->addWidget(lpanel);

        hsplit->addWidget(table_pane);
        hsplit->setStretchFactor(0, 0);
        hsplit->setStretchFactor(1, 1);

        ul->addWidget(hsplit, 1);
    }
    outer_vsplit->addWidget(upper);

    // ── Chart panel ───────────────────────────────────────────────────────────
    m_chart_pane = new QWidget(outer_vsplit);
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
    outer_vsplit->addWidget(m_chart_pane);
    outer_vsplit->setStretchFactor(0, 3);
    outer_vsplit->setStretchFactor(1, 1);
    outer_vsplit->setSizes({520, 160});

    root->addWidget(outer_vsplit, 1);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_visible_only_cb, &QCheckBox::toggled,
            this, &SSSMetadataWindow::onShowOnlyVisibleToggled);
    connect(m_table, &QTableView::customContextMenuRequested,
            this, &SSSMetadataWindow::showTableContextMenu);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SSSMetadataWindow::onSelectionChanged);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Selection status bar
// ─────────────────────────────────────────────────────────────────────────────

void SSSMetadataWindow::onSelectionChanged()
{
    updateSelectionStatus();
}

void SSSMetadataWindow::updateSelectionStatus()
{
    const auto indices = m_table->selectionModel()->selectedIndexes();
    if (indices.isEmpty()) {
        m_sel_status->clear();
        return;
    }

    double sum = 0, mn = std::numeric_limits<double>::max(),
                    mx = -std::numeric_limits<double>::max();
    int numeric = 0;

    for (const auto& idx : indices) {
        const double v = idx.data(Qt::UserRole + 1).toDouble();
        bool ok;
        idx.data(Qt::DisplayRole).toString().toDouble(&ok);
        if (ok) {
            sum += v;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
            ++numeric;
        }
    }

    QString msg = QString("Selected: %1 cells").arg(indices.size());
    if (numeric > 0) {
        const double avg = sum / numeric;
        msg += QString("   |   Min: %1   Max: %2   Avg: %3   Sum: %4")
                   .arg(mn,  0, 'g', 5)
                   .arg(mx,  0, 'g', 5)
                   .arg(avg, 0, 'g', 5)
                   .arg(sum, 0, 'g', 6);
    }
    m_sel_status->setText(msg);
}

} // namespace dolphin::ui
