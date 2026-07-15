#pragma once
#include <QString>

// Internal segment functions — not part of the public AppStyle API.
// Each AppStyleXxx.cpp defines one of these; AppStyle.cpp concatenates them.
namespace dolphin::ui::detail {
QString qssBase();
QString qssShell();
QString qssPanels();
QString qssDialogs();
QString qssDialogChrome();
QString qssDialogImport();
QString qssDialogContacts();
QString qssDialogProgress();
QString qssAcousticViews();
QString qssNodeGraph();
} // namespace dolphin::ui::detail
