#pragma once
// AppCommands — single source of truth for shared command metadata.
//
// Define icon paths, labels, shortcuts, palette aliases, and categories here.
// Menus, toolbars, and command palettes all read from this table — changing a
// shortcut or label here propagates everywhere automatically.
//
// Usage — palette:
//   items.append(toPaletteItem(CommandId::NewProject, true, [this] { onNewProject(); }));
//
// Usage — menu / toolbar:
//   auto* act = makeAction(CommandId::RunSelectedLayer, this);
//   act->setText(tr("Run &Selected Layer"));   // optional: override with mnemonic label
//   connect(act, &QAction::triggered, this, &MainWindow::onRunSelectedLayer);
//   menu->addAction(act);

#include "ui/shared/dialogs/CommandPaletteDialog.h"
#include <QAction>
#include <functional>

namespace dolphin::ui {

enum class CommandId {
    // File
    NewProject,
    OpenProject,
    SaveProject,
    SaveProjectAs,
    OpenProjectFolder,
    CloseProject,
    ImportFile,
    // Project
    GeodeticSettings,
    ResetRaw,
    // Contacts / annotation
    AddContact,
    ClearContacts,
    // Tools
    Measure,
    BottomTrack,
    // Features / windows
    WaterfallOpen,
    SubBottomOpen,
    // Processing
    RunSelectedLayer,
    RunAllLayers,
    // Line navigation (shared between Waterfall and Sub-bottom viewer)
    PrevLine,
    NextLine,
    // Settings / preferences
    AppSettings,
    // Node graph / pipeline
    NodeGraph,
    // Export
    ExportCsv,
    ExportGeotiff,
    ExportKmz,
    ExportNav,
    ExportPdf,
    // Help
    About,
    ExportScreenshot,
};

struct AppCommandDef {
    const char* icon;       // resource path e.g. ":/icons/new.svg", or nullptr
    const char* label;      // canonical label, UTF-8; wrap with tr() / translate()
    const char* tooltip;    // longer tooltip for toolbar buttons; nullptr = use label
    const char* shortcut;   // "Ctrl+N", "Ctrl+Shift+W", or ""
    const char* aliases;    // space-separated, for palette fuzzy search
    const char* category;   // palette group header: "File", "Tools", "Navigate", …
};

// Returns the static definition for a command.
const AppCommandDef& appCommand(CommandId id);

// Build a CommandPaletteItem from the shared definition.
// The caller supplies the runtime-varying enabled state and the action handler.
CommandPaletteItem toPaletteItem(CommandId id, bool enabled, std::function<void()> fn);

// Creates a QAction configured from the AppCommandDef for id: icon (if defined),
// translated label, tooltip, and keyboard shortcut. The caller owns the action.
// Override setText() afterward to add a menu mnemonic or a short toolbar label.
QAction* makeAction(CommandId id, QObject* parent = nullptr);

} // namespace dolphin::ui
