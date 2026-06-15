// MainWindow.Commands.cpp — command palette item provider.
//
// Supplies the full command list to the CommandBar on each activation.
// Commands are split into two groups:
//   • Static window actions — panel toggles, fit map, etc.
//   • Dynamic items        — per-layer and per-contact entries from the active project.
#include "ui/mainwindow/MainWindow.h"
#include "ui/shared/AppCommands.h"
#include "ui/features/map/MapView.h"
#include "ui/shell/Features.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/Contact.h"

namespace {
QString modalityBadge(dolphin::app::Modality m)
{
    using M = dolphin::app::Modality;
    switch (m) {
    case M::Sidescan:     return QStringLiteral("SSS");
    case M::SubBottom:    return QStringLiteral("SBP");
    case M::Magnetometer: return QStringLiteral("MAG");
    case M::Multibeam:    return QStringLiteral("MBE");
    case M::Raster:       return QStringLiteral("Raster");
    default:              return {};
    }
}
} // namespace

namespace dolphin::ui {

QList<CommandPaletteItem> MainWindow::buildCommandItems()
{
    QList<CommandPaletteItem> items;

    // Window-specific items not in AppCommands (View panel toggles, map fit).
    auto add = [&](const QString& cat, const QString& label,
                   const QString& sc,  const QString& aliases,
                   bool enabled, std::function<void()> fn)
    {
        items.append({cat, label, sc, aliases, enabled, false, std::move(fn)});
    };

    // Shared commands — label, shortcut, aliases, and category from AppCommands.
    auto addCmd = [&](CommandId id, bool enabled, std::function<void()> fn) {
        items.append(toPaletteItem(id, enabled, std::move(fn)));
    };

    const bool has_project = currentProject() != nullptr;
    const bool has_layer   = has_project && !activeLayerId().empty();
    const bool has_sbp     = has_layer && [this]() -> bool {
        const auto* l = currentProject()->findLayer(activeLayerId());
        return l && l->modality == app::Modality::SubBottom;
    }();

    // -- File -----------------------------------------------------------------
    addCmd(CommandId::NewProject,        true,        [this] { onNewProject(); });
    addCmd(CommandId::OpenProject,       true,        [this] { onOpenProject(); });
    addCmd(CommandId::SaveProject,       has_project, [this] { onSaveProject(); });
    addCmd(CommandId::SaveProjectAs,     has_project, [this] { onSaveProjectAs(); });
    addCmd(CommandId::OpenProjectFolder, has_project, [this] { onOpenProjectFolder(); });
    addCmd(CommandId::ImportFile,        has_project, [this] { onImportFile(); });
    addCmd(CommandId::CloseProject,      has_project, [this] { onCloseProject(); });

    // -- Project ---------------------------------------------------------------
    addCmd(CommandId::GeodeticSettings,  has_project, [this] { onGeodeticSettings(); });
    addCmd(CommandId::ResetRaw,          has_layer,   [this] { onResetRaw(); });

    // -- View ------------------------------------------------------------------
    add("View", tr("Toggle Primary Side Bar"),   "", "sidebar left panel context",
        true, [this] { onToggleContextPanel(); });
    add("View", tr("Toggle Secondary Side Bar"), "", "sidebar right properties inspector",
        true, [this] { onTogglePropertiesPanel(); });
    add("View", tr("Toggle Tool Bar"),           "", "toolbar tools right",
        true, [this] { setRightToolBarVisible(!rightToolBarVisible()); });
    add("View", tr("Fit Map to Data"),           "", "fit zoom reset map",
        m_map_view != nullptr, [this] { if (m_map_view) m_map_view->fitToDataAndReset(); });

    // -- Navigate -------------------------------------------------------------
    addCmd(CommandId::WaterfallOpen, has_layer, [this] { onWaterfallOpen(); });
    addCmd(CommandId::SubBottomOpen, has_sbp,   [this] { onSubBottomOpen(); });

    // -- Tools -----------------------------------------------------------------
    addCmd(CommandId::Measure,      true,        [this] { onToolMeasure(); });
    addCmd(CommandId::BottomTrack,  has_sbp,     [this] { onBottomTrack(); });
    addCmd(CommandId::NodeGraph,    true,        [this] { onNodeGraph(); });
    addCmd(CommandId::AddContact,   has_project, [this] { onAddContact(); });
    addCmd(CommandId::ClearContacts,has_project, [this] { onClearContacts(); });

    // -- Processing ------------------------------------------------------------
    addCmd(CommandId::RunSelectedLayer, has_layer,   [this] { onRunSelectedLayer(); });
    addCmd(CommandId::RunAllLayers,     has_project, [this] { onRunAllLayers(); });

    // -- Export ----------------------------------------------------------------
    addCmd(CommandId::ExportCsv,     has_project, [this] { onExportCsv(); });
    addCmd(CommandId::ExportGeotiff, has_project, [this] { onExportGeotiff(); });
    addCmd(CommandId::ExportKmz,     has_project, [this] { onExportKmz(); });
    addCmd(CommandId::ExportNav,     has_project, [this] { onExportNav(); });
    addCmd(CommandId::ExportPdf,     has_project, [this] { onExportPdf(); });
    addCmd(CommandId::ExportScreenshot, true,     [this] { onExportScreenshot(); });

    // -- Settings / Help -------------------------------------------------------
    addCmd(CommandId::AppSettings, true, [this] { onAppSettings(); });
    addCmd(CommandId::About,       true, [this] { onAbout(); });

    // -- Dynamic project items -------------------------------------------------
    if (has_project) {
        auto makeLine = [&](const app::DataLayer& l) {
            const QString badge = modalityBadge(l.modality);
            CommandPaletteItem it;
            it.category   = tr("Line");
            it.label      = QString::fromStdString(l.label);
            it.shortcut   = badge;
            it.aliases    = QStringLiteral("line survey ") + badge.toLower();
            it.searchOnly = true;
            it.action     = [this, id = l.id] { onLayerSelected(id); };
            return it;
        };

        // Active layer floats to the top of the Line group on every open.
        for (const auto& layer : currentProject()->layers())
            if (layer->id == activeLayerId()) { items.append(makeLine(*layer)); break; }
        for (const auto& layer : currentProject()->layers())
            if (layer->id != activeLayerId())  items.append(makeLine(*layer));
    }

    if constexpr (Features::kContacts) {
        if (has_project) {
            for (const auto& c : currentProject()->contacts()) {
                const uint64_t cid = c.id;
                CommandPaletteItem it;
                it.category   = tr("Contact");
                it.label      = QString::fromStdString(c.label)
                                 + "  " + QString::fromStdString(c.classification);
                it.aliases    = QStringLiteral("contact pick");
                it.searchOnly = true;
                it.action     = [this, cid] { onContactSelected(cid); };
                items.append(std::move(it));
            }
        }
    }

    return items;
}

} // namespace dolphin::ui
