// MainWindow.ToolBar.cpp — buildActivityBar, buildRightToolBar, setupToolBar.
#include "ui/mainwindow/MainWindow.h"
#include "ui/shared/AppCommands.h"
#include "ui/shared/widgets/AnimatedToolButton.h"
#include "ui/shell/Features.h"
#include "ui/shell/Theme.h"

#include <QAction>
#include <QButtonGroup>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

QFrame* MainWindow::buildActivityBar(QWidget* parent)
{
    auto* bar = new QFrame(parent);
    bar->setObjectName("activityBar");
    bar->setFixedWidth(Theme::kActivityBarW);
    auto* layout = new QVBoxLayout(bar);
    layout->setContentsMargins(0, Theme::kSpacing3, 0, Theme::kSpacing3);
    layout->setSpacing(2);

    auto addBtn = [&](const char* icon, const char* tip, int panel) {
        auto* btn = new AnimatedToolButton(bar);
        btn->setIcon(QIcon(icon));
        btn->setIconSize(QSize(Theme::kIconSideBar, Theme::kIconSideBar));
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setAutoExclusive(false);
        btn->setFixedSize(Theme::kActivityBarW, Theme::kSideButtonH);
        connect(btn, &QToolButton::clicked, this, [this, panel]() { togglePanel(panel); });
        layout->addWidget(btn);
        m_activity_btns[panel] = btn;
    };

    addBtn(":/icons/layers.svg",    "Explorer — project layers, contacts, and features",  PanelExplorer);
    addBtn(":/icons/waterfall.svg", "Sidescan Sonar viewer — SSS only",                      PanelWaterfall);
    if constexpr (Features::kDataLibrary)
        addBtn(":/icons/database.svg",
               "Data Library — sources, layers, contacts, and project health.",
               PanelDataLibrary);
    if constexpr (Features::kNodeGraph)
        addBtn(":/icons/run_all.svg",    "Processing",      PanelProcessing);
    if constexpr (Features::kContacts)
        addBtn(":/icons/contacts.svg",   "Contacts",        PanelContacts);
    if constexpr (Features::kAnalyze)
        addBtn(":/icons/analyze.svg",  "Analyze",         PanelAnalyze);
    if constexpr (Features::kAI)
        addBtn(":/icons/ai.svg",       "AI",              PanelAI);
    if constexpr (Features::kPresent)
        addBtn(":/icons/present.svg",  "Present",         PanelPresent);
    if constexpr (Features::kReport)
        addBtn(":/icons/report.svg",   "Report Generator",PanelReport);

    layout->addStretch();

    auto* btn_settings = new AnimatedToolButton(bar);
    btn_settings->setIcon(QIcon(":/icons/settings2.svg"));
    btn_settings->setIconSize(QSize(Theme::kIconSideBar, Theme::kIconSideBar));
    btn_settings->setToolTip(tr("Settings"));
    btn_settings->setCheckable(true);
    btn_settings->setAutoExclusive(false);
    btn_settings->setFixedSize(Theme::kActivityBarW, Theme::kSideButtonH);
    connect(btn_settings, &QToolButton::clicked, this, [this]() { togglePanel(PanelSettings); });
    layout->addWidget(btn_settings);
    m_activity_btns[PanelSettings] = btn_settings;

    return bar;
}

QFrame* MainWindow::buildRightToolBar(QWidget* parent)
{
    auto* bar = new QFrame(parent);
    bar->setObjectName("toolBar");
    bar->setFixedWidth(Theme::kToolBarW);
    auto* layout = new QVBoxLayout(bar);
    layout->setContentsMargins(0, Theme::kSpacing3, 0, Theme::kSpacing3);
    layout->setSpacing(2);

    auto makeDivider = [&]() -> QFrame* {
        auto* d = new QFrame(bar);
        d->setObjectName("toolbarDivider");
        d->setFixedHeight(Theme::kSepSz);
        return d;
    };

    auto addToolBtn = [&](const char* icon, const char* tip,
                          QButtonGroup* group, void (MainWindow::*slot)(),
                          bool checked = false) {
        auto* btn = new AnimatedToolButton(bar);
        btn->setIcon(QIcon(icon));
        btn->setIconSize(QSize(Theme::kIconSideBar, Theme::kIconSideBar));
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setFixedSize(Theme::kToolBarW, Theme::kSideButtonH);
        group->addButton(btn);
        connect(btn, &QToolButton::clicked, this, slot);
        layout->addWidget(btn);
    };

    // Cursor / Select toggle — icon shows the mode you will switch TO on click.
    // Starts in cursor (pan) mode, so the button shows the select icon.
    m_cursor_select_btn = new AnimatedToolButton(bar);
    m_cursor_select_btn->setIcon(QIcon(":/icons/select.svg"));
    m_cursor_select_btn->setIconSize(QSize(Theme::kIconSideBar, Theme::kIconSideBar));
    m_cursor_select_btn->setToolTip(tr("Switch to Select (S)"));
    m_cursor_select_btn->setFixedSize(Theme::kToolBarW, Theme::kSideButtonH);
    connect(m_cursor_select_btn, &QToolButton::clicked, this, &MainWindow::onToolCursorSelectToggle);
    layout->addWidget(m_cursor_select_btn);
    layout->addWidget(makeDivider());

    // View tools
    auto* view_grp = new QButtonGroup(bar);
    view_grp->setExclusive(true);
    addToolBtn(":/icons/zoom.svg",    "Zoom (Z)",    view_grp, &MainWindow::onToolZoom);
    addToolBtn(":/icons/measure.svg", "Measure (M)", view_grp, &MainWindow::onToolMeasure);
    layout->addWidget(makeDivider());

    // Contact / annotation actions
    struct Item {
        const char* icon;
        const char* tip;
        void (MainWindow::*slot)();
        bool enabled;
    };
    for (const auto& item : {
            Item{":/icons/add_contact.svg",
                 "Add Contact — click on the map to place a point pick (target, shadow, anomaly, etc.).",
                 &MainWindow::onAddContact,    true },
            Item{":/icons/reset_raw.svg",
                 "Reset to Raw Navigation — SSS only\n"
                 "Discards all navigation corrections for the current sidescan layer\n"
                 "and reloads it with the original recorded GPS positions.",
                 &MainWindow::onResetRaw,      true },
            Item{":/icons/clear_contacts.svg",
                 "Clear All Contacts — removes every contact pick from this project.",
                 &MainWindow::onClearContacts, true },
        }) {
        auto* btn = new AnimatedToolButton(bar);
        btn->setIcon(QIcon(item.icon));
        btn->setIconSize(QSize(Theme::kIconSideBar, Theme::kIconSideBar));
        btn->setToolTip(item.tip);
        btn->setEnabled(item.enabled);
        btn->setFixedSize(Theme::kToolBarW, Theme::kSideButtonH);
        connect(btn, &QToolButton::clicked, this, item.slot);
        layout->addWidget(btn);
    }

    layout->addStretch();

    // Settings gear — pinned at the bottom of the right toolbar.
    auto* btn_settings = new AnimatedToolButton(bar);
    btn_settings->setIcon(QIcon(":/icons/settings.svg"));
    btn_settings->setIconSize(QSize(Theme::kIconSideBar, Theme::kIconSideBar));
    btn_settings->setToolTip(tr("Application Settings"));
    btn_settings->setFixedSize(Theme::kToolBarW, Theme::kSideButtonH);
    connect(btn_settings, &QToolButton::clicked, this, &MainWindow::onAppSettings);
    layout->addWidget(btn_settings);

    return bar;
}

void MainWindow::setupToolBar()
{
    auto* tb = addToolBar(tr("Main"));
    tb->setObjectName("mainToolBar");
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setIconSize(QSize(Theme::kIconToolBar, Theme::kIconToolBar));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // ── File ────────────────────────────────────────────────────────────────
    auto* act_new = makeAction(CommandId::NewProject, this);
    connect(act_new, &QAction::triggered, this, &MainWindow::onNewProject);
    tb->addAction(act_new);

    auto* act_open = makeAction(CommandId::OpenProject, this);
    connect(act_open, &QAction::triggered, this, &MainWindow::onOpenProject);
    tb->addAction(act_open);

    m_act_save = makeAction(CommandId::SaveProject, this);
    connect(m_act_save, &QAction::triggered, this, &MainWindow::onSaveProject);
    m_act_save->setEnabled(false);
    tb->addAction(m_act_save);

    tb->addSeparator();

    auto* act_import = makeAction(CommandId::ImportFile, this);
    connect(act_import, &QAction::triggered, this, &MainWindow::onImportFile);
    tb->addAction(act_import);

    // ── Export (dropdown button) ─────────────────────────────────────────────
    m_export_btn = new QToolButton(tb);
    m_export_btn->setIcon(QIcon(":/icons/export.svg"));
    m_export_btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_export_btn->setPopupMode(QToolButton::InstantPopup);
    m_export_btn->setToolTip(tr("Export project data.\n"
        "Choose from CSV table, GeoTIFF mosaic, KMZ, navigation track, or PDF report.\n"
        "Available only when a project is open."));
    m_export_btn->setEnabled(false);
    auto* exp_menu = new QMenu(m_export_btn);
    for (auto [id, slot] : {
            std::pair{CommandId::ExportCsv,     &MainWindow::onExportCsv    },
            std::pair{CommandId::ExportGeotiff, &MainWindow::onExportGeotiff},
            std::pair{CommandId::ExportKmz,     &MainWindow::onExportKmz    },
            std::pair{CommandId::ExportNav,     &MainWindow::onExportNav    },
            std::pair{CommandId::ExportPdf,     &MainWindow::onExportPdf    },
        }) {
        auto* a = makeAction(id, this);
        connect(a, &QAction::triggered, this, slot);
        if (id != CommandId::ExportCsv) a->setEnabled(false);
        m_export_actions.push_back(a);
        exp_menu->addAction(a);
    }
    m_export_btn->setMenu(exp_menu);
    tb->addWidget(m_export_btn);

    if constexpr (Features::kDataLibrary) {
        auto* btn_db = new QToolButton(tb);
        btn_db->setIcon(QIcon(":/icons/database.svg"));
        btn_db->setToolTip(tr("Data Library — sources, layers, contacts, and project health."));
        btn_db->setToolButtonStyle(Qt::ToolButtonIconOnly);
        connect(btn_db, &QToolButton::clicked, this, &MainWindow::onDataLibraryOpen);
        tb->addWidget(btn_db);
    }

    tb->addSeparator();

    // ── Module shortcuts ─────────────────────────────────────────────────────
    struct ModuleBtn { CommandId id; const char* tip; bool checkable; bool enabled; };
    const ModuleBtn module_btns[] = {
        { CommandId::GeodeticSettings,
          "Geodesy — coordinate reference system settings.\n"
          "Set or change the source CRS for all imported files and the project display CRS.",
          false, true  },
        { CommandId::SubBottomOpen,
          "Sub-Bottom Viewer — SBP only (B)\n"
          "Opens the seismic section viewer for sub-bottom profiler layers.",
          false, true  },
        { CommandId::WaterfallOpen,
          "Sidescan Sonar Viewer — SSS only\n"
          "Opens the waterfall image viewer for the active sidescan layer.\n"
          "Requires an SSS layer to be selected in the Explorer.",
          true,  true  },
    };
    QToolButton* module_tb_btns[3] = {};
    for (int i = 0; i < 3; ++i) {
        auto* btn = new QToolButton(tb);
        btn->setIcon(QIcon(QString::fromUtf8(appCommand(module_btns[i].id).icon)));
        btn->setToolTip(module_btns[i].tip);
        btn->setCheckable(module_btns[i].checkable);
        btn->setEnabled(module_btns[i].enabled);
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        tb->addWidget(btn);
        module_tb_btns[i] = btn;
    }

    m_btn_bottom_track = module_tb_btns[1];

    connect(module_tb_btns[0], &QToolButton::clicked, this,
            &MainWindow::onGeodeticSettings);
    connect(m_btn_bottom_track, &QToolButton::clicked, this, &MainWindow::onSubBottomOpen);
    connect(module_tb_btns[2], &QToolButton::clicked, this, &MainWindow::onWaterfallOpen);

    // ── Map tool keyboard shortcuts ──────────────────────────────────────────
    // ApplicationShortcut so V/S/Z/M fire even when a viewer window has focus.
    auto addShortcut = [this](const QKeySequence& key, void (MainWindow::*slot)()) {
        auto* act = new QAction(this);
        act->setShortcut(key);
        act->setShortcutContext(Qt::ApplicationShortcut);
        addAction(act);
        connect(act, &QAction::triggered, this, slot);
    };
    addShortcut(QKeySequence("V"), &MainWindow::onToolCursor);
    addShortcut(QKeySequence("S"), &MainWindow::onToolSelect);
    addShortcut(QKeySequence("Z"), &MainWindow::onToolZoom);
    addShortcut(QKeySequence("M"), &MainWindow::onToolMeasure);
}

} // namespace dolphin::ui
