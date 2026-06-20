// MainWindow.Menus.cpp — menu bar construction.
// Geodetic settings dialog lives in MainWindow.Geodesy.cpp.
#include "ui/mainwindow/MainWindow.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/shared/AppCommands.h"
#include "ui/shared/dialogs/SettingsDialog.h"
#include "ui/shell/Features.h"
#include "ui/shell/Theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

namespace dolphin::ui {

void MainWindow::setupMenuBar()
{
    buildFileMenu();
    buildEditMenu();
    buildProjectMenu();
    if constexpr (Features::kProcessing) buildProcessingMenu();
    buildViewMenu();
    if constexpr (Features::kNodeGraph)  buildNodeMenu();

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    auto* act_about = makeAction(CommandId::About, this);
    act_about->setText(tr("&About Dolphin Explorer"));
    connect(act_about, &QAction::triggered, this, &MainWindow::onAbout);
    help->addAction(act_about);

    auto* logo_lbl = new QLabel(menuBar());
    logo_lbl->setPixmap(QIcon(":/icons/dolphin_logo.svg").pixmap(QSize(18, 18)));
    logo_lbl->setContentsMargins(Theme::kSpacing3, 0, Theme::kSpacing2, 0);
    logo_lbl->setToolTip(tr("Dolphin Explorer"));
    menuBar()->setCornerWidget(logo_lbl, Qt::TopLeftCorner);
}

void MainWindow::buildFileMenu()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));

    auto* act_new = makeAction(CommandId::NewProject, this);
    act_new->setText(tr("&New Project"));
    connect(act_new, &QAction::triggered, this, &MainWindow::onNewProject);
    file->addAction(act_new);

    auto* act_open = makeAction(CommandId::OpenProject, this);
    act_open->setText(tr("&Open Project\u2026"));
    connect(act_open, &QAction::triggered, this, &MainWindow::onOpenProject);
    file->addAction(act_open);

    m_recent_menu = file->addMenu(tr("Open &Recent"));
    connect(m_recent_menu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);

    file->addSeparator();

    auto* act_save = makeAction(CommandId::SaveProject, this);
    act_save->setText(tr("&Save Project"));
    connect(act_save, &QAction::triggered, this, &MainWindow::onSaveProject);
    file->addAction(act_save);

    auto* act_save_as = makeAction(CommandId::SaveProjectAs, this);
    act_save_as->setText(tr("Save Project &As\u2026"));
    connect(act_save_as, &QAction::triggered, this, &MainWindow::onSaveProjectAs);
    file->addAction(act_save_as);

    file->addSeparator();

    m_act_open_folder = makeAction(CommandId::OpenProjectFolder, this);
    m_act_open_folder->setText(tr("Open Project &Folder"));
    connect(m_act_open_folder, &QAction::triggered, this, &MainWindow::onOpenProjectFolder);
    m_act_open_folder->setEnabled(false);
    file->addAction(m_act_open_folder);

    auto* act_close = makeAction(CommandId::CloseProject, this);
    act_close->setText(tr("&Close Project"));
    connect(act_close, &QAction::triggered, this, &MainWindow::onCloseProject);
    file->addAction(act_close);

    file->addSeparator();

    QMenu* imp = file->addMenu(
        QIcon(QString::fromUtf8(appCommand(CommandId::ImportFile).icon)), tr("&Import"));

    // Each modality submenu preselects that family in the import wizard; the wizard
    // still detects what each file actually contains and lets the user confirm.
    using AT = core::ArtifactType;
    const auto importAs = [this](std::vector<AT> preset) {
        return [this, preset] { importFilesWithPreset(preset); };
    };

    QMenu* sss = imp->addMenu(tr("Sidescan Sonar"));
    sss->addAction(tr("XTF\u2026"),                  this, importAs({AT::Sidescan}));
    sss->addAction(tr("JSF\u2026"),                  this, importAs({AT::Sidescan}));
    sss->addAction(tr("Humminbird SON\u2026"),        this, importAs({AT::Sidescan}));
    sss->addAction(tr("Lowrance SL2 / SL3\u2026"),   this, importAs({AT::Sidescan}));
    sss->addAction(tr("Dolphin Cache (DLPD)\u2026"),  this, importAs({AT::Sidescan}));

    QMenu* sbp = imp->addMenu(tr("Sub Bottom Profiler"));
    sbp->addAction(tr("SEG-Y\u2026"),                this, importAs({AT::SubBottom}));

    QMenu* mag = imp->addMenu(tr("Magnetometer"));
    mag->addAction(tr("CSV / Text\u2026"),           this, importAs({AT::Magnetometer}));

    QMenu* bathy = imp->addMenu(tr("Bathymetry"));
    bathy->addAction(tr("Kongsberg ALL / KMALL\u2026"), this, importAs({AT::Multibeam}));
    bathy->addAction(tr("Reson S7K\u2026"),          this, importAs({AT::Multibeam}));

    imp->addSeparator();
    // Generic browse: no sensor preset \u2014 the wizard detects each file's modalities.
    imp->addAction(tr("Browse All Formats\u2026"),   this, importAs({}));

    file->addSeparator();
    QMenu* exp = file->addMenu(QIcon(":/icons/export.svg"), tr("E&xport"));
    exp->addAction(tr("Export &Manager…"), this, &MainWindow::onExportManagerOpen);
    exp->addSeparator();
    // Export formats are Phase 2 — listed for discoverability but not yet active.
    for (auto [id, slot] : {
            std::pair{CommandId::ExportCsv,     &MainWindow::onExportCsv    },
            std::pair{CommandId::ExportGeotiff, &MainWindow::onExportGeotiff},
            std::pair{CommandId::ExportKmz,     &MainWindow::onExportKmz    },
            std::pair{CommandId::ExportNav,     &MainWindow::onExportNav    },
            std::pair{CommandId::ExportPdf,     &MainWindow::onExportPdf    },
        }) {
        auto* a = makeAction(id, this);
        connect(a, &QAction::triggered, this, slot);
        // CSV (contacts) and GeoTIFF (raster layers) are implemented; others Phase 2.
        if (id != CommandId::ExportCsv && id != CommandId::ExportGeotiff)
            a->setEnabled(false);
        exp->addAction(a);
    }
    exp->addSeparator();
    auto* act_screenshot = makeAction(CommandId::ExportScreenshot, this);
    connect(act_screenshot, &QAction::triggered,
            this, &MainWindow::onExportScreenshot);
    exp->addAction(act_screenshot);
    file->addSeparator();
    file->addAction(tr("E&xit"), QKeySequence::Quit, qApp, &QApplication::quit);
}

void MainWindow::buildEditMenu()
{
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));

    m_act_undo = edit->addAction(tr("&Undo"), m_undo_stack, &QUndoStack::undo);
    m_act_undo->setShortcut(QKeySequence::Undo);
    m_act_undo->setEnabled(false);

    m_act_redo = edit->addAction(tr("&Redo"), m_undo_stack, &QUndoStack::redo);
    m_act_redo->setShortcut(QKeySequence::Redo);
    m_act_redo->setEnabled(false);

    connect(m_undo_stack, &QUndoStack::canUndoChanged, m_act_undo, &QAction::setEnabled);
    connect(m_undo_stack, &QUndoStack::canRedoChanged,  m_act_redo, &QAction::setEnabled);

    connect(m_undo_stack, &QUndoStack::undoTextChanged, this, [this](const QString& text) {
        m_act_undo->setText(text.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(text));
    });
    connect(m_undo_stack, &QUndoStack::redoTextChanged, this, [this](const QString& text) {
        m_act_redo->setText(text.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(text));
    });
}

void MainWindow::buildProjectMenu()
{
    QMenu* proj = menuBar()->addMenu(tr("&Project"));

    auto* act_geo = makeAction(CommandId::GeodeticSettings, this);
    act_geo->setText(tr("&Geodetic Settings\u2026"));
    connect(act_geo, &QAction::triggered, this, &MainWindow::onGeodeticSettings);
    proj->addAction(act_geo);

    if constexpr (Features::kContacts) {
        proj->addSeparator();
        QMenu* contacts = proj->addMenu(tr("&Contacts"));

        contacts->addAction(tr("Contact &Manager…"),
                            this, &MainWindow::onContactManagerOpen);
        contacts->addSeparator();

        auto* act_add = makeAction(CommandId::AddContact, this);
        act_add->setText(tr("&Add Contact"));
        connect(act_add, &QAction::triggered, this, &MainWindow::onAddContact);
        contacts->addAction(act_add);

        auto* act_clear = makeAction(CommandId::ClearContacts, this);
        act_clear->setText(tr("&Clear All"));
        connect(act_clear, &QAction::triggered, this, &MainWindow::onClearContacts);
        contacts->addAction(act_clear);

        contacts->addSeparator();
        auto* act_exp_csv = makeAction(CommandId::ExportCsv, this);
        connect(act_exp_csv, &QAction::triggered, this, &MainWindow::onExportCsv);
        contacts->addAction(act_exp_csv);
        auto* act_exp_kmz = makeAction(CommandId::ExportKmz, this);
        connect(act_exp_kmz, &QAction::triggered, this, &MainWindow::onExportKmz);
        act_exp_kmz->setEnabled(false);
        contacts->addAction(act_exp_kmz);
    }
}


void MainWindow::buildProcessingMenu()
{
    QMenu* proc = menuBar()->addMenu(tr("&Processing"));
    m_act_run_layer = makeAction(CommandId::RunSelectedLayer, this);
    m_act_run_layer->setText(tr("Run &Selected Layer"));
    connect(m_act_run_layer, &QAction::triggered, this, &MainWindow::onRunSelectedLayer);
    m_act_run_layer->setEnabled(false);
    proc->addAction(m_act_run_layer);

    m_act_run_all = makeAction(CommandId::RunAllLayers, this);
    m_act_run_all->setText(tr("&Run All Layers"));
    connect(m_act_run_all, &QAction::triggered, this, &MainWindow::onRunAllLayers);
    m_act_run_all->setEnabled(false);
    proc->addAction(m_act_run_all);
    // Explicit, confirmable "commit corrections to data" (SeaView-style mosaic bake).
    // Ordinary gain/imaging Apply is display-state only; this writes the corrected
    // .dlpd sidecars so the map mosaic and exports include the full corrections.
    proc->addAction(tr("&Bake Corrections into Data…"),
                    this, &MainWindow::onBakeCorrections);
    proc->addSeparator();
    proc->addAction(QIcon(":/icons/nav_editor.svg"),
        tr("&Navigation Processing…"), this, &MainWindow::onRenumberContacts);
    proc->addAction(QIcon(":/icons/seabed_track.svg"),
        tr("&Heading / Georeference…"), this, &MainWindow::onLineProps);
    proc->addSeparator();
    auto* act_reset = makeAction(CommandId::ResetRaw, this);
    act_reset->setText(tr("Reset Layer to Raw"));
    connect(act_reset, &QAction::triggered, this, &MainWindow::onResetRaw);
    proc->addAction(act_reset);
    if constexpr (Features::kNodeGraph) {
        auto* act_ng = makeAction(CommandId::NodeGraph, this);
        act_ng->setText(tr("Node Graph\u2026"));
        connect(act_ng, &QAction::triggered, this, &MainWindow::onNodeGraph);
        proc->addAction(act_ng);
    }
    proc->addSeparator();
    proc->addAction(tr("&Processing Window\u2026"),
        QKeySequence("Ctrl+Shift+P"), this, &MainWindow::onOpenProcessingWindow);
}

void MainWindow::buildViewMenu()
{
    QMenu* view = menuBar()->addMenu(tr("&View"));

    auto* act_toolbar = view->addAction(tr("Tool Bar (Right)"));
    act_toolbar->setCheckable(true);
    act_toolbar->setChecked(rightToolBarVisible());
    connect(act_toolbar, &QAction::triggered, this, [this, act_toolbar]() {
        setRightToolBarVisible(act_toolbar->isChecked());
    });

    auto* act_props = view->addAction(tr("Properties Panel"));
    act_props->setCheckable(true);
    act_props->setChecked(m_props_open);
    connect(act_props, &QAction::triggered, this, [this, act_props]() {
        setPropertiesOpen(act_props->isChecked());
    });

    view->addSeparator();

    // -- Map Sonar Preview Quality ---------------------------------------------
    QMenu* sonar_menu = view->addMenu(tr("Map Sonar Preview"));
    auto*  ag         = new QActionGroup(sonar_menu);
    ag->setExclusive(true);

    struct QualityEntry { MapSonarQuality q; const char* label; };
    static constexpr QualityEntry kEntries[] = {
        { MapSonarQuality::Off,          "Off"                 },
        { MapSonarQuality::CoverageOnly, "Coverage Only"       },
        { MapSonarQuality::Low,          "Low"                 },
        { MapSonarQuality::Medium,       "Medium"              },
        { MapSonarQuality::High,         "High"                },
    };

    const int saved_q = m_display_state
        ? static_cast<int>(m_display_state->mapQuality())
        : static_cast<int>(MapSonarQuality::CoverageOnly);

    for (const auto& e : kEntries) {
        auto* act = sonar_menu->addAction(tr(e.label));
        act->setCheckable(true);
        act->setChecked(static_cast<int>(e.q) == saved_q);
        ag->addAction(act);
        m_act_map_quality[static_cast<int>(e.q)] = act;

        const MapSonarQuality q = e.q;
        connect(act, &QAction::triggered, this, [this, q]() {
            onMapSonarQuality(q);
        });

        // Separator before Low (visual grouping: off/coverage | preview tiers)
        if (e.q == MapSonarQuality::CoverageOnly)
            sonar_menu->addSeparator();
    }

    connect(view, &QMenu::aboutToShow, this, [this, act_toolbar, act_props]() {
        act_toolbar->setChecked(rightToolBarVisible());
        act_props->setChecked(m_props_open);

        // Sync quality checkmarks with the display-state authority.
        const int cur = m_display_state
            ? static_cast<int>(m_display_state->mapQuality())
            : static_cast<int>(MapSonarQuality::CoverageOnly);
        for (int i = 0; i < static_cast<int>(m_act_map_quality.size()); ++i)
            if (m_act_map_quality[i])
                m_act_map_quality[i]->setChecked(i == cur);
    });

    view->addSeparator();

    auto* act_wf = makeAction(CommandId::WaterfallOpen, this);
    act_wf->setText(tr("Waterfall  (SSS)"));
    connect(act_wf, &QAction::triggered, this, &MainWindow::onWaterfallOpen);
    view->addAction(act_wf);

    auto* act_sb = makeAction(CommandId::SubBottomOpen, this);
    act_sb->setText(tr("Sub-bottom Viewer  (SBP)"));
    connect(act_sb, &QAction::triggered, this, &MainWindow::onSubBottomOpen);
    view->addAction(act_sb);
}

void MainWindow::onMapSonarQuality(MapSonarQuality q)
{
    // Route through the display-state authority: it persists the choice and emits
    // displayStateChanged(MapQuality); our handler applies it to the controller and
    // syncs the menu check-marks. Single source of truth.
    if (m_display_state) m_display_state->setMapQuality(q);
}

void MainWindow::buildNodeMenu()
{
    QMenu* node = menuBar()->addMenu(tr("&Node"));
    auto* act_ng = makeAction(CommandId::NodeGraph, this);
    act_ng->setText(tr("&Open Node Graph"));
    connect(act_ng, &QAction::triggered, this, &MainWindow::onNodeGraph);
    node->addAction(act_ng);
    node->addSeparator();
    node->addAction(tr("&Execute Graph"), QKeySequence("Ctrl+E"), this, &MainWindow::onRunAllLayers);
    auto* act_reset = makeAction(CommandId::ResetRaw, this);
    act_reset->setText(tr("&Reset Graph"));
    connect(act_reset, &QAction::triggered, this, &MainWindow::onResetRaw);
    node->addAction(act_reset);
}

} // namespace dolphin::ui
