// ImportReviewWizard.SensorTabs.cpp — Heading, Channels, and Header tab rebuilds.
#include "ui/features/import/ImportReviewWizard.h"
#include "ui/shell/Theme.h"
#include "ui/shared/UiUtils.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  Heading tab
// ─────────────────────────────────────────────────────────────────────────────

void ImportReviewWizard::rebuildHeadingTab()
{
    clearLayout(m_hdg_layout);

    bool any_probing  = false;
    bool any_heading  = false;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (!e.done || !e.result.success) continue;
        if (!e.result.heading_valid) continue;

        any_heading = true;
        const io::ProbeResult& r = e.result;

        auto* sect = new QFrame(m_hdg_content);
        sect->setObjectName("dlgSection");
        auto* vl = new QVBoxLayout(sect);
        vl->setContentsMargins(10, Theme::kSpacing2, 10, Theme::kSpacing2);
        vl->setSpacing(3);

        vl->addWidget(new QLabel("<b>" + e.file_name + "</b>", sect));

        // Range line
        auto* range_lbl = new QLabel(
            tr("Min: %1°    Max: %2°    Mean: %3°")
                .arg(r.heading_min,  0, 'f', 1)
                .arg(r.heading_max,  0, 'f', 1)
                .arg(r.heading_mean, 0, 'f', 1),
            sect);
        range_lbl->setObjectName("dlgLabelMono");
        vl->addWidget(range_lbl);

        // Cardinal direction hint for mean heading
        const float h = r.heading_mean;
        const char* cardinal =
            (h <  22.5f || h >= 337.5f) ? "N"  :
            (h <  67.5f)                 ? "NE" :
            (h < 112.5f)                 ? "E"  :
            (h < 157.5f)                 ? "SE" :
            (h < 202.5f)                 ? "S"  :
            (h < 247.5f)                 ? "SW" :
            (h < 292.5f)                 ? "W"  : "NW";
        auto* dir_lbl = new QLabel(
            tr("Average track direction: %1 (%2)")
                .arg(r.heading_mean, 0, 'f', 0)
                .arg(QString::fromLatin1(cardinal)),
            sect);
        dir_lbl->setObjectName("dlgLabelMeta");
        vl->addWidget(dir_lbl);

        m_hdg_layout->addWidget(sect);
    }

    if (any_probing) {
        auto* lbl = new QLabel(tr("Scanning files..."), m_hdg_content);
        lbl->setObjectName("dlgLabelMeta");
        m_hdg_layout->addWidget(lbl);
    } else if (!any_heading) {
        auto* lbl = new QLabel(tr("No heading data found in these files."), m_hdg_content);
        lbl->setObjectName("dlgLabelMeta");
        m_hdg_layout->addWidget(lbl);
    }

    m_hdg_layout->addStretch();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Channels tab
// ─────────────────────────────────────────────────────────────────────────────

void ImportReviewWizard::rebuildChannelsTab()
{
    clearLayout(m_chn_layout);

    bool any_probing  = false;
    bool any_channels = false;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (!e.done || !e.result.success) continue;
        if (e.result.channels.empty()) continue;

        any_channels = true;
        const io::ProbeResult& r = e.result;

        auto* sect = new QFrame(m_chn_content);
        sect->setObjectName("dlgSection");
        auto* vl = new QVBoxLayout(sect);
        vl->setContentsMargins(10, Theme::kSpacing2, 10, Theme::kSpacing2);
        vl->setSpacing(3);

        vl->addWidget(new QLabel("<b>" + e.file_name + "</b>", sect));

        for (const auto& ch : r.channels) {
            QString line = "  " + QString::fromStdString(ch.name);
            if (!ch.modality.empty() && ch.modality != ch.name)
                line += "  [" + QString::fromStdString(ch.modality) + "]";
            if (ch.frequency_khz > 0.f)
                line += tr("   %1 kHz").arg(ch.frequency_khz, 0, 'f', 0);
            if (ch.range_m > 0.f)
                line += tr("   range %1 m").arg(ch.range_m, 0, 'f', 0);
            if (ch.samples_per_ping > 0)
                line += tr("   %1 samples/ping").arg(ch.samples_per_ping);

            auto* ch_lbl = new QLabel(line, sect);
            ch_lbl->setObjectName("dlgLabelMono");
            vl->addWidget(ch_lbl);
        }

        m_chn_layout->addWidget(sect);
    }

    if (any_probing) {
        auto* lbl = new QLabel(tr("Scanning files..."), m_chn_content);
        lbl->setObjectName("dlgLabelMeta");
        m_chn_layout->addWidget(lbl);
    } else if (!any_channels) {
        auto* lbl = new QLabel(tr("No channel table available for these files."), m_chn_content);
        lbl->setObjectName("dlgLabelMeta");
        m_chn_layout->addWidget(lbl);
    }

    m_chn_layout->addStretch();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Header tab
// ─────────────────────────────────────────────────────────────────────────────

void ImportReviewWizard::rebuildHeaderTab()
{
    clearLayout(m_hdr_layout);

    bool any_probing = false;
    bool any_header  = false;

    for (const auto& e : m_entries) {
        if (e.probing) { any_probing = true; continue; }
        if (!e.done || !e.result.success) continue;
        if (e.result.text_header_excerpt.empty()) continue;

        any_header = true;

        auto* sect = new QFrame(m_hdr_content);
        sect->setObjectName("dlgSection");
        auto* vl = new QVBoxLayout(sect);
        vl->setContentsMargins(10, Theme::kSpacing2, 10, Theme::kSpacing2);
        vl->setSpacing(Theme::kSpacing1);

        vl->addWidget(new QLabel("<b>" + e.file_name + "</b>", sect));

        auto* txt = new QLabel(
            QString::fromStdString(e.result.text_header_excerpt), sect);
        txt->setObjectName("dlgLabelMonoSmall");
        txt->setWordWrap(true);
        txt->setTextInteractionFlags(Qt::TextSelectableByMouse);
        vl->addWidget(txt);

        m_hdr_layout->addWidget(sect);
    }

    if (any_probing) {
        auto* lbl = new QLabel(tr("Scanning files..."), m_hdr_content);
        lbl->setObjectName("dlgLabelMeta");
        m_hdr_layout->addWidget(lbl);
    } else if (!any_header) {
        auto* lbl = new QLabel(tr("No text header found in these files."), m_hdr_content);
        lbl->setObjectName("dlgLabelMeta");
        m_hdr_layout->addWidget(lbl);
    }

    m_hdr_layout->addStretch();
}

} // namespace dolphin::ui
