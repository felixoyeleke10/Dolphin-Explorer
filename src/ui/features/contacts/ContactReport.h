#pragma once
// ContactReport — generates a machine-readable contact CSV or a formatted
// report as PDF/Word. PDF and DOCX share one presentation row model.
#include <QString>
#include <vector>
#include "core/Contact.h"

class QWidget;

namespace dolphin::app { class Project; }

namespace dolphin::ui {

namespace ContactReport {

// `contacts` are the rows to include (caller decides scope — e.g. the visible set);
// `project` resolves each contact's sensor/line. Returns false on write failure.
bool writeCsv (const QString& path, const QString& title,
               const std::vector<core::Contact>& contacts, app::Project* project);
bool writePdf (const QString& path, const QString& title,
               const std::vector<core::Contact>& contacts, app::Project* project);
bool writeDocx(const QString& path, const QString& title,
               const std::vector<core::Contact>& contacts, app::Project* project);

// Full interactive export: format/save-file dialog (CSV / PDF / Word) + write +
// error box. Shared by the Contact Manager and the Contact Editor so every
// export entry point behaves identically. Returns the written path, or empty
// if cancelled / nothing to export / write failure (failure shows a warning).
QString exportInteractive(QWidget* parent, const QString& title,
                          const std::vector<core::Contact>& contacts,
                          app::Project* project);

} // namespace ContactReport

} // namespace dolphin::ui
