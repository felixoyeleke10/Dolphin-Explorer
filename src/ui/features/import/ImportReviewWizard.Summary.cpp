// ImportReviewWizard.Summary.cpp — Summary tab: one row per file showing format,
// modality, coordinate range, record count and any probe warnings.
#include "ui/features/import/ImportReviewWizard.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

namespace dolphin::ui {

void ImportReviewWizard::rebuildSummaryTab()
{
    clearLayout(m_summary_layout);

    if (m_entries.isEmpty()) {
        auto* lbl = new QLabel(tr("No files added yet."), m_summary_content);
        lbl->setObjectName("dlgLabelMeta");
        m_summary_layout->addWidget(lbl);
        m_summary_layout->addStretch();
        return;
    }

    for (const auto& e : m_entries) {
        auto* row = new QFrame(m_summary_content);
        auto* hl  = new QHBoxLayout(row);
        hl->setContentsMargins(Theme::kSpacing1, 2, Theme::kSpacing1, 2);
        hl->setSpacing(Theme::kSpacing3);

        auto* name = new QLabel("<b>" + e.file_name + "</b>", row);
        hl->addWidget(name);

        if (e.probing) {
            hl->addWidget(new QLabel(tr("Scanning..."), row), 1);
        } else if (!e.result.success) {
            auto* err = new QLabel(
                tr("Error: ") + QString::fromStdString(e.result.error_message), row);
            err->setObjectName("dlgLabelDanger");
            hl->addWidget(err, 1);
        } else if (e.modality_mismatch) {
            auto* warn = new QLabel(tr("Wrong sensor type — will not be imported"), row);
            warn->setObjectName("dlgLabelCaution");
            hl->addWidget(warn, 1);
        } else {
            const io::ProbeResult& r = e.result;

            QString info = QString::fromStdString(r.format_name)
                         + "  \xC2\xB7  " + modalityString(r) + "  \xC2\xB7  ";
            if (!r.coord_valid) {
                info += tr("No coordinates");
            } else if (r.is_projected) {
                info += tr("Projected  X %1\xE2\x80\x93%2  Y %3\xE2\x80\x93%4")
                    .arg(r.coord_min_x, 0, 'f', 0).arg(r.coord_max_x, 0, 'f', 0)
                    .arg(r.coord_min_y, 0, 'f', 0).arg(r.coord_max_y, 0, 'f', 0);
            } else {
                info += tr("Geographic  %1\xC2\xB0\xE2\x80\x93%2\xC2\xB0  %3\xC2\xB0\xE2\x80\x93%4\xC2\xB0")
                    .arg(r.coord_min_x, 0, 'f', 3).arg(r.coord_max_x, 0, 'f', 3)
                    .arg(r.coord_min_y, 0, 'f', 3).arg(r.coord_max_y, 0, 'f', 3);
            }
            if (r.estimated_record_count > 0)
                info += tr("  \xC2\xB7  ~%1 records")
                    .arg(QLocale().toString(r.estimated_record_count));

            auto* info_lbl = new QLabel(info, row);
            info_lbl->setObjectName("dlgLabelMeta");
            hl->addWidget(info_lbl, 1);

            if (!r.units_contradicted) {
                for (const auto& w : r.warnings) {
                    auto* wlbl = new QLabel(
                        QString::fromStdString(w), m_summary_content);
                    wlbl->setObjectName("dlgLabelCaution");
                    m_summary_layout->addWidget(row);
                    m_summary_layout->addWidget(wlbl);
                    row = nullptr;
                    break;
                }
            }
        }

        if (row) m_summary_layout->addWidget(row);
    }

    m_summary_layout->addStretch();
}

} // namespace dolphin::ui
