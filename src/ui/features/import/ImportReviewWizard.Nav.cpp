// ImportReviewWizard.Nav.cpp — Nav tab: per-file coordinate type, bounding box
// and sample coordinates. Shared across all sensor types.
#include "ui/features/import/ImportReviewWizard.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

void ImportReviewWizard::rebuildNavTab()
{
    clearLayout(m_nav_layout);

    bool any_probing = false;
    bool any_file    = false;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (!e.done || e.modality_mismatch) continue;

        any_file = true;
        const io::ProbeResult& r = e.result;

        auto* sect = new QFrame(m_nav_content);
        sect->setObjectName("dlgSection");
        auto* vl = new QVBoxLayout(sect);
        vl->setContentsMargins(10, Theme::kSpacing2, 10, Theme::kSpacing2);
        vl->setSpacing(3);

        vl->addWidget(new QLabel("<b>" + e.file_name + "</b>", sect));

        if (!r.success) {
            auto* lbl = new QLabel(
                tr("Error: ") + QString::fromStdString(r.error_message), sect);
            lbl->setObjectName("dlgLabelDanger");
            vl->addWidget(lbl);
            m_nav_layout->addWidget(sect);
            continue;
        }

        if (!r.coord_valid) {
            auto* lbl = new QLabel(tr("No navigation data found"), sect);
            lbl->setObjectName("dlgLabelMeta");
            vl->addWidget(lbl);
            m_nav_layout->addWidget(sect);
            continue;
        }

        const int prec = r.is_projected ? 1 : 5;
        const QString type  = r.is_projected ? tr("Projected (m)") : tr("Geographic (deg)");
        const QString range = r.is_projected
            ? tr("X: %1 to %2    Y: %3 to %4")
                  .arg(r.coord_min_x, 0, 'f', prec).arg(r.coord_max_x, 0, 'f', prec)
                  .arg(r.coord_min_y, 0, 'f', prec).arg(r.coord_max_y, 0, 'f', prec)
            : tr("Lon: %1 to %2    Lat: %3 to %4")
                  .arg(r.coord_min_x, 0, 'f', prec).arg(r.coord_max_x, 0, 'f', prec)
                  .arg(r.coord_min_y, 0, 'f', prec).arg(r.coord_max_y, 0, 'f', prec);

        auto* range_lbl = new QLabel(type + "    " + range, sect);
        range_lbl->setObjectName("dlgLabelMono");
        vl->addWidget(range_lbl);

        if (!r.coord_samples.empty()) {
            const int n = std::min(static_cast<int>(r.coord_samples.size()), 5);
            QString s = tr("Samples: ");
            for (int j = 0; j < n; ++j) {
                if (j) s += "   ";
                s += QString("(%1, %2)")
                    .arg(r.coord_samples[j].x, 0, 'f', prec)
                    .arg(r.coord_samples[j].y, 0, 'f', prec);
            }
            auto* samp = new QLabel(s, sect);
            samp->setObjectName("dlgLabelMono");
            vl->addWidget(samp);
        }

        if (r.possibly_swapped) {
            auto* w = new QLabel(tr("[!] X/Y coordinates may be transposed"), sect);
            w->setObjectName("dlgLabelCaution");
            vl->addWidget(w);
        }
        if (r.units_contradicted) {
            auto* w = new QLabel(
                tr("[!] Declared geographic CRS but values appear projected"), sect);
            w->setObjectName("dlgLabelCaution");
            vl->addWidget(w);
        }

        m_nav_layout->addWidget(sect);
    }

    if (any_probing) {
        auto* lbl = new QLabel(tr("Scanning files..."), m_nav_content);
        lbl->setObjectName("dlgLabelMeta");
        m_nav_layout->addWidget(lbl);
    } else if (!any_file) {
        auto* lbl = new QLabel(tr("No files added yet."), m_nav_content);
        lbl->setObjectName("dlgLabelMeta");
        m_nav_layout->addWidget(lbl);
    }

    m_nav_layout->addStretch();
}

} // namespace dolphin::ui
