#include "ui/shared/AppCommands.h"
#include "ui/shell/Theme.h"
#include <QCoreApplication>
#include <QIcon>

namespace dolphin::ui {

// One row per CommandId enumerator, in declaration order.
// Labels are UTF-8; … = HORIZONTAL ELLIPSIS.
static const AppCommandDef kDefs[] = {
  // id                 icon                            label                       tooltip                                  shortcut         aliases                          category
  /* NewProject      */ {":/icons/new.svg",             "New Project…",        nullptr,                                 "Ctrl+N",        "",                              "File"       },
  /* OpenProject     */ {":/icons/open.svg",            "Open Project…",       nullptr,                                 "Ctrl+O",        "",                              "File"       },
  /* SaveProject     */ {":/icons/save.svg",            "Save Project",             nullptr,                                 "Ctrl+S",        "",                              "File"       },
  /* SaveProjectAs   */ {nullptr,                       "Save Project As…",    nullptr,                                 "Ctrl+Shift+S",  "",                              "File"       },
  /* OpenProjectFolder*/{nullptr,                       "Open Project Folder",      nullptr,                                 "Ctrl+Shift+F",  "",                              "File"       },
  /* CloseProject    */ {nullptr,                       "Close Project",            nullptr,                                 "",              "",                              "File"       },
  /* ImportFile      */ {":/icons/import.svg",          "Import File…",        "Import a sonar data file",              "Ctrl+I",        "import add",                    "File"       },
  /* GeodeticSettings*/ {":/icons/geodesy.svg",         "Geodetic Settings…",  nullptr,                                 "",              "crs datum projection geodesy",  "Project"    },
  /* ResetRaw        */ {":/icons/reset_raw.svg",       "Reset to Raw Navigation",  nullptr,                                 "",              "reset raw nav",                 "Project"    },
  /* AddContact      */ {":/icons/add_contact.svg",     "Add Contact",              "Add a contact to the map",              "",              "contact pick mark",             "Tools"      },
  /* ClearContacts   */ {":/icons/clear_contacts.svg",  "Clear All Contacts",       nullptr,                                 "",              "clear contacts delete",         "Tools"      },
  /* Measure         */ {":/icons/measure.svg",         "Measure",                  "Measure distance on the map",           "M",             "measure distance",              "Tools"      },
  /* BottomTrack     */ {":/icons/bottom_track.svg",    "Sub-bottom Viewer",        "Open the sub-bottom profiler viewer",   "B",             "subbottom sbp profiler chirp bottom track", "Tools"      },
  /* WaterfallOpen   */ {":/icons/waterfall.svg",       "Open Waterfall",           "Open the sidescan waterfall view",      "Ctrl+Shift+W",  "waterfall sss sonar",           "Navigate"   },
  /* SubBottomOpen   */ {":/icons/bottom_track.svg",   "Open Sub-Bottom Viewer",   "Open the sub-bottom profiler viewer",   "Ctrl+Shift+B",  "sbp subbottom profiler chirp",  "Navigate"   },
  /* RunSelectedLayer*/ {":/icons/run_line.svg",        "Run Selected Layer",       nullptr,                                 "Ctrl+Shift+R",  "run layer process",             "Processing" },
  /* RunAllLayers    */ {":/icons/run_all.svg",         "Run All Layers",           nullptr,                                 "Ctrl+R",        "run all process batch",         "Processing" },
  /* PrevLine        */ {nullptr,                       "Previous Line",            nullptr,                                 "P",             "prev back",                     "Navigate"   },
  /* NextLine        */ {nullptr,                       "Next Line",                nullptr,                                 "N",             "next forward",                  "Navigate"   },
  /* AppSettings     */ {nullptr,                       "App Settings…",       nullptr,                                 "Ctrl+,",        "settings preferences config",   "Settings"   },
  /* NodeGraph       */ {nullptr,                       "Node Graph…",         nullptr,                                 "Ctrl+G",        "nodes graph pipeline workflow", "Tools"      },
  /* ExportCsv       */ {":/icons/export_csv.svg",     "Export as CSV…",      nullptr,                                 "",              "export csv table contacts",     "Export"     },
  /* ExportGeotiff   */ {":/icons/export_geotiff.svg", "Export as GeoTIFF…",  nullptr,                                 "",              "export geotiff raster image",   "Export"     },
  /* ExportKmz       */ {":/icons/export_kmz.svg",     "Export as KMZ…",      nullptr,                                 "",              "export kmz kml",                "Export"     },
  /* ExportNav       */ {":/icons/export_nav.svg",     "Export Navigation…",  nullptr,                                 "",              "export nav gps track route",    "Export"     },
  /* ExportPdf       */ {":/icons/export_pdf.svg",     "Export as PDF…",      nullptr,                                 "",              "export pdf report print",       "Export"     },
  /* About           */ {nullptr,                       "About Dolphin…",      nullptr,                                 "",              "about version info",            "Help"       },
  /* ExportScreenshot*/ {":/icons/export.svg",         "Export Screenshot...",  "Save a stable screenshot of the current workspace", "Ctrl+Alt+S", "screenshot snapshot capture image png", "Export" },
};

static_assert(static_cast<int>(CommandId::ExportScreenshot) + 1
              == static_cast<int>(std::size(kDefs)),
              "kDefs must have exactly one entry per CommandId enumerator — "
              "add a row to kDefs whenever you extend CommandId");

const AppCommandDef& appCommand(CommandId id)
{
    return kDefs[static_cast<int>(id)];
}

QAction* makeAction(CommandId id, QObject* parent)
{
    const AppCommandDef& d = appCommand(id);
    auto* act = new QAction(parent);
    if (d.icon && d.icon[0])
        act->setIcon(Theme::icon(QString::fromUtf8(d.icon)));
    act->setText(QCoreApplication::translate("AppCommands", d.label));
    const char* tip = (d.tooltip && d.tooltip[0]) ? d.tooltip : d.label;
    act->setToolTip(QCoreApplication::translate("AppCommands", tip));
    if (d.shortcut && d.shortcut[0])
        act->setShortcut(QKeySequence(QString::fromUtf8(d.shortcut)));
    return act;
}

CommandPaletteItem toPaletteItem(CommandId id, bool enabled, std::function<void()> fn)
{
    const AppCommandDef& d = appCommand(id);
    CommandPaletteItem item;
    item.category   = QCoreApplication::translate("AppCommands", d.category);
    item.label      = QCoreApplication::translate("AppCommands", d.label);
    item.shortcut   = QString::fromUtf8(d.shortcut);
    item.aliases    = QString::fromUtf8(d.aliases);
    item.enabled    = enabled;
    item.searchOnly = false;   // shared commands always visible when palette is empty
    item.action     = std::move(fn);
    return item;
}

} // namespace dolphin::ui
