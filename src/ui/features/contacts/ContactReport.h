#pragma once
// ContactReport — generates a contacts report as PDF (QPdfWriter) or Word .docx
// (OOXML built with util::ZipWriter). Both outputs are produced from one shared
// row model so they stay in sync.
#include <QString>
#include <vector>
#include "core/Contact.h"

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

} // namespace ContactReport

} // namespace dolphin::ui
