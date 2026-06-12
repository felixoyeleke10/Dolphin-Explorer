// ImportReviewWizard.CRS.cpp — CRS tab: batch-level CRS picker + per-file
// diagnostic rows. Shared across all sensor types.
#include "ui/features/import/ImportReviewWizard.h"
#include "ui/shared/UiUtils.h"
#include "ui/shared/dialogs/CrsPickerDialog.h"
#include "ui/shell/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

void ImportReviewWizard::rebuildCrsTab()
{
    clearLayout(m_crs_layout);
    m_crs_value_label = nullptr;

    bool any_probing = false;
    int  n_review    = 0;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (e.done && e.result.success && !e.modality_mismatch && e.result.needs_crs_review)
            ++n_review;
    }

    if (any_probing) {
        auto* lbl = new QLabel(tr("Scanning files..."), m_crs_content);
        lbl->setObjectName("dlgLabelMeta");
        m_crs_layout->addWidget(lbl);
        m_crs_layout->addStretch();
        return;
    }

    if (m_entries.isEmpty()) {
        auto* lbl = new QLabel(tr("No files added yet."), m_crs_content);
        lbl->setObjectName("dlgLabelMeta");
        m_crs_layout->addWidget(lbl);
        m_crs_layout->addStretch();
        return;
    }

    if (n_review == 0) {
        m_crs_layout->addWidget(
            new QLabel(tr("All files have unambiguous CRS — no review needed."),
                       m_crs_content));
        m_crs_layout->addStretch();
        return;
    }

    // -- Batch-level CRS picker ------------------------------------------------
    auto* sect = new QFrame(m_crs_content);
    sect->setObjectName("dlgSection");
    auto* vl = new QVBoxLayout(sect);
    vl->setContentsMargins(10, Theme::kSpacing3, 10, Theme::kSpacing3);
    vl->setSpacing(Theme::kSpacing2);

    auto* desc = new QLabel(
        tr("%n file(s) contain projected coordinates. "
           "Confirm the source CRS so positions are correctly georeferenced.",
           "", n_review),
        sect);
    desc->setObjectName("dlgLabelMeta");
    desc->setWordWrap(true);
    vl->addWidget(desc);

    if (n_review > 1) {
        auto* warn = new QLabel(
            tr("<b>Note:</b> one CRS will be applied to all %n files listed below. "
               "If your files span different UTM zones, import each group separately.",
               "", n_review),
            sect);
        warn->setObjectName("dlgLabelWarning");
        warn->setWordWrap(true);
        vl->addWidget(warn);
    }

    auto* crs_row = new QWidget(sect);
    auto* hl      = new QHBoxLayout(crs_row);
    hl->setContentsMargins(0, 2, 0, 0);
    hl->setSpacing(Theme::kSpacing3);
    hl->addWidget(new QLabel(tr("CRS:"), crs_row));

    m_crs_value_label = new QLabel(crs_row);
    m_crs_value_label->setObjectName("dlgLabelMeta");
    m_crs_value_label->setText(
        m_project_crs.empty()
            ? tr("<i>Not set</i>")
            : QString::fromStdString(m_project_crs.id));

    auto* pick_btn = new QPushButton(tr("Pick CRS..."), crs_row);
    pick_btn->setObjectName("dlgBtnSecondary");
    pick_btn->setFixedHeight(Theme::kColorBtnH);

    connect(pick_btn, &QPushButton::clicked, this, [this]() {
        CrsPickerDialog dlg(m_project_crs, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_project_crs = dlg.selectedRef();
            if (m_crs_value_label)
                m_crs_value_label->setText(QString::fromStdString(m_project_crs.id));
            for (int i = 0; i < m_entries.size(); ++i)
                updateFileRow(i);
            updateImportButton();
        }
    });

    hl->addWidget(m_crs_value_label, 1);
    hl->addWidget(pick_btn);
    vl->addWidget(crs_row);
    m_crs_layout->addWidget(sect);

    // -- Per-file diagnostic rows ----------------------------------------------
    for (const auto& e : m_entries) {
        if (!e.done || !e.result.success || e.modality_mismatch) continue;
        if (!e.result.needs_crs_review) continue;
        const io::ProbeResult& r = e.result;

        auto* fsect = new QFrame(m_crs_content);
        fsect->setObjectName("dlgSection");
        auto* fvl = new QVBoxLayout(fsect);
        fvl->setContentsMargins(10, Theme::kSpacing1, 10, Theme::kSpacing1);
        fvl->setSpacing(2);

        fvl->addWidget(new QLabel("<b>" + e.file_name + "</b>", fsect));

        QString det;
        if (!r.coord_valid)       det = tr("No coordinate data");
        else if (r.is_projected)  det = tr("Detected: Projected (metre-scale values)");
        else                      det = tr("Detected: Geographic");
        if (r.possibly_swapped)   det += tr("  [!] X/Y may be swapped");
        if (r.units_contradicted) det += tr("  [!] Declared geo, projected values");

        auto* det_lbl = new QLabel(det, fsect);
        det_lbl->setObjectName("dlgLabelMeta");
        fvl->addWidget(det_lbl);

        if (!r.coord_samples.empty()) {
            const int prec = r.is_projected ? 1 : 6;
            QString s = tr("Samples: ");
            for (int j = 0, n = std::min(static_cast<int>(r.coord_samples.size()), 3); j < n; ++j) {
                if (j) s += "  ";
                s += QString("(%1, %2)")
                         .arg(r.coord_samples[j].x, 0, 'f', prec)
                         .arg(r.coord_samples[j].y, 0, 'f', prec);
            }
            auto* samp = new QLabel(s, fsect);
            samp->setObjectName("dlgLabelMono");
            fvl->addWidget(samp);
        }

        m_crs_layout->addWidget(fsect);
    }

    m_crs_layout->addStretch();
}

} // namespace dolphin::ui
