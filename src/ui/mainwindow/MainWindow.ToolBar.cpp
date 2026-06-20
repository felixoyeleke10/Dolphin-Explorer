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

    // Primary panels — always visible
    addBtn(":/icons/layers.svg",    "Explorer — project layers, contacts, and features",  PanelExplorer);
    addBtn(":/icons/waterfall.svg", "Sidescan Sonar viewer — SSS only",                   PanelWaterfall);

    // Secondary panels — each gets a direct button; if any are enabled, also
    // add a ··· overflow button so secondary panels can be reached without
    // knowing which feature flags are on.
    constexpr bool kHasSecondary = Features::kDataLibrary || Features::kNodeGraph
                                || Features::kContacts    || Features::kAnalyze
                                || Features::kAI          || Features::kPresent
                                || Features::kReport;

    if constexpr (Features::kDataLibrary)
        addBtn(":/icons/database.svg",
               "Data Library — sources, layers, contacts, and project health.",
               PanelDataLibrary);
    if constexpr (Features::kNodeGraph)
        addBtn(":/icons/run_all.svg",   "Processing",       PanelProcessing);
    if constexpr (Features::kContacts)
        addBtn(":/icons/contacts.svg",  "Contacts",         PanelContacts);
    if constexpr (Features::kAnalyze)
        addBtn(":/icons/analyze.svg",   "Analyze",          PanelAnalyze);
    if constexpr (Features::kAI)
        addBtn(":/icons/ai.svg",        "AI",               PanelAI);
    if constexpr (Features::kPresent)
        addBtn(":/icons/present.svg",   "Present",          PanelPresent);
    if constexpr (Features::kReport)
        addBtn(":/icons/report.svg",    "Report Generator", PanelReport);

    if constexpr (kHasSecondary) {
        layout->addSpacing(2);
        auto* more_btn = new AnimatedToolButton(bar);
        more_btn->setIcon(QIcon(":/icons/more.svg"));
        more_btn->setIconSize(QSize(Theme::kIconSideBar, Theme::kIconSideBar));
        more_btn->setToolTip(tr("More panels"));
        more_btn->setFixedSize(Theme::kActivityBarW, Theme::kSideButtonH);
        more_btn->setPopupMode(QToolButton::InstantPopup);

        auto* more_menu = new QMenu(more_btn);
        struct PanelEntry { bool enabled; const char* icon; const char* label; int panel; };
        for (const auto& e : {
                PanelEntry{Features::kDataLibrary, ":/icons/database.svg", "Data Library",    PanelDataLibrary},
                PanelEntry{Features::kNodeGraph,   ":/icons/run_all.svg",  "Processing",      PanelProcessing},
                PanelEntry{Features::kContacts,    ":/icons/contacts.svg", "Contacts",        PanelContacts},
                PanelEntry{Features::kAnalyze,     ":/icons/analyze.svg",  "Analyze",         PanelAnalyze},
                PanelEntry{Features::kAI,          ":/icons/ai.svg",       "AI",              PanelAI},
                PanelEntry{Features::kPresent,     ":/icons/present.svg",  "Present",         PanelPresent},
                PanelEntry{Features::kReport,      ":/icons/report.svg",   "Report Generator",PanelReport},
            }) {
            auto* act = more_menu->addAction(QIcon(e.icon), tr(e.label),
                                             this, [this, panel = e.panel]{ togglePanel(panel); });
            act->setEnabled(e.enabled);
        }
        more_btn->setMenu(more_menu);
        layout->addWidget(more_btn);
    }

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

    // One exclusive group covers all interactive tools — exactly one active at a time.
    auto* tool_grp = new QButtonGroup(bar);
    tool_grp->setExclusive(true);

    auto addToolBtn = [&](QToolButton*& out, const char* icon, const QString& tip,
                          void (MainWindow::*slot)(), bool checked = false) {
        auto* btn = new AnimatedToolButton(bar);
        btn->setIcon(QIcon(icon));
        btn->setIconSize(QSize(Theme::kIconSideBar, Theme::kIconSideBar));
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setFixedSize(Theme::kToolBarW, Theme::kSideButtonH);
        tool_grp->addButton(btn);
        connect(btn, &QToolButton::clicked, this, slot);
        layout->addWidget(btn);
        out = btn;
    };

    // Navigation tools — mutually exclusive, Cursor active by default.
    addToolBtn(m_cursor_btn,  ":/icons/cursor.svg",      tr("Cursor (V)     — pan and inspect"),                      &MainWindow::onToolCursor, true);
    addToolBtn(m_select_btn,  ":/icons/select.svg",      tr("Select (S)     — click to select a layer or feature"),    &MainWindow::onToolSelect);
    layout->addWidget(makeDivider());
    addToolBtn(m_zoom_btn,    ":/icons/zoom.svg",         tr("Zoom (Z)       — scroll or drag to zoom the map"),        &MainWindow::onToolZoom);
    addToolBtn(m_measure_btn, ":/icons/measure.svg",      tr("Measure (M)    — click points to measure distance"),      &MainWindow::onToolMeasure);
    layout->addWidget(makeDivider());
    addToolBtn(m_contact_btn, ":/icons/add_contact.svg",  tr("Contact (C)    — click map to place a contact pick.\n"
                                                             "Stays active; switch tools to stop placing."),            &MainWindow::onAddContact);
    layout->addWidget(makeDivider());

    // More actions — 3D toggle plus destructive ops hidden behind ···
    {
        auto* btn = new AnimatedToolButton(bar);
        btn->setIcon(QIcon(":/icons/more.svg"));
        btn->setIconSize(QSize(Theme::kIconSideBar, Theme::kIconSideBar));
        btn->setToolTip(tr("More actions"));
        btn->setFixedSize(Theme::kToolBarW, Theme::kSideButtonH);
        btn->setPopupMode(QToolButton::InstantPopup);

        auto* menu = new QMenu(btn);
        auto* act_3d = menu->addAction(
            QIcon(":/icons/layers.svg"),
            tr("Toggle 3D View"),
            this, &MainWindow::onToggle3D);
        act_3d->setToolTip(
            tr("Switch to OpenGL terrain/sonar-drape view. Run again to return to 2D plan view."));
        menu->addSeparator();
        auto* act_reset = menu->addAction(
            QIcon(":/icons/reset_raw.svg"),
            tr("Reset to Raw Navigation"),
            this, &MainWindow::onResetRaw);
        act_reset->setToolTip(
            tr("Discards all navigation corrections for the current sidescan layer\n"
               "and reloads it with the original recorded GPS positions."));
        auto* act_clear = menu->addAction(
            QIcon(":/icons/clear_contacts.svg"),
            tr("Clear All Contacts"),
            this, &MainWindow::onClearContacts);
        act_clear->setToolTip(tr("Removes every contact pick from this project."));

        btn->setMenu(menu);
        layout->addWidget(btn);
    }

    layout->addStretch();

    // Settings — pinned at the bottom, not part of the tool group.
    layout->addWidget(makeDivider());
    m_settings_btn = new AnimatedToolButton(bar);
    m_settings_btn->setIcon(QIcon(":/icons/settings2.svg"));
    m_settings_btn->setIconSize(QSize(Theme::kIconSideBar, Theme::kIconSideBar));
    m_settings_btn->setToolTip(tr("Settings"));
    m_settings_btn->setCheckable(true);
    m_settings_btn->setAutoExclusive(false);
    m_settings_btn->setFixedSize(Theme::kToolBarW, Theme::kSideButtonH);
    connect(m_settings_btn, &QToolButton::clicked, this, [this]() { togglePanel(PanelSettings); });
    layout->addWidget(m_settings_btn);

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

    // -- File ----------------------------------------------------------------
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

    // -- Export (dropdown button) ---------------------------------------------
    m_export_btn = new QToolButton(tb);
    m_export_btn->setIcon(QIcon(":/icons/export.svg"));
    m_export_btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_export_btn->setPopupMode(QToolButton::InstantPopup);
    m_export_btn->setToolTip(tr("Export project data.\n"
        "Choose from CSV table, GeoTIFF mosaic, KMZ, navigation track, or PDF report.\n"
        "Available only when a project is open."));
    m_export_btn->setEnabled(false);
    auto* exp_menu = new QMenu(m_export_btn);
    exp_menu->addAction(tr("Export Manager…"), this, &MainWindow::onExportManagerOpen);
    exp_menu->addSeparator();
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

    if constexpr (Features::kContacts) {
        auto* btn_contacts = new QToolButton(tb);
        btn_contacts->setIcon(QIcon(":/icons/contacts.svg"));
        btn_contacts->setToolTip(tr("Contact Manager — all contacts across SSS / SBP / MAG / MBES.\n"
                                    "Filter by sensor, search, navigate, remove, and export."));
        btn_contacts->setToolButtonStyle(Qt::ToolButtonIconOnly);
        connect(btn_contacts, &QToolButton::clicked, this, &MainWindow::onContactManagerOpen);
        tb->addWidget(btn_contacts);
    }

    tb->addSeparator();

    // -- Module shortcuts -----------------------------------------------------
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

    m_btn_sbp_open = module_tb_btns[1];

    connect(module_tb_btns[0], &QToolButton::clicked, this,
            &MainWindow::onGeodeticSettings);
    connect(m_btn_sbp_open, &QToolButton::clicked, this, &MainWindow::onSubBottomOpen);
    connect(module_tb_btns[2], &QToolButton::clicked, this, &MainWindow::onWaterfallOpen);

    // -- Map tool keyboard shortcuts ------------------------------------------
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
    addShortcut(QKeySequence("C"), &MainWindow::onAddContact);
}

} // namespace dolphin::ui
