// WaterfallAnalysisPanelContact.cpp
//
// buildContactSection() — CONTACT PICKING panel section.
// buildFeatureSection() — FEATURE PICKING panel section (Phase 2 placeholder).
//
// Contacts are POINT picks (boulders, debris, anomalies, …).
// Features are SHAPE annotations (polygons, polylines, freehand outlines) — Phase 2.
// The two concepts must not be conflated in the UI or data model.

#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

// ─────────────────────────────────────────────────────────────────────────────
//  CONTACT PICKING section
//
//  Contacts are single-point picks.  Each click while the pick tool is active
//  places one contact at (ping, channel, range) with the selected classification.
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallAnalysisPanel::buildContactSection(QVBoxLayout* vl, QWidget* container)
{
    auto* bl = makeSection(tr("Contact Picking"), /*expanded=*/true, container, vl);

    // ── Pick tool button ──────────────────────────────────────────────────
    {
        auto* row = new QWidget;
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3, Theme::kSpacing4, Theme::kSpacing1);
        rl->setSpacing(Theme::kSpacing2);

        m_contact_pick_btn = new QToolButton(row);
        m_contact_pick_btn->setText(tr("Pick Contact"));
        m_contact_pick_btn->setCheckable(true);
        m_contact_pick_btn->setChecked(false);
        m_contact_pick_btn->setObjectName("wfToolBtn");
        m_contact_pick_btn->setToolTip(
            tr("Toggle contact picking.\n"
               "When active, click the waterfall at a target, shadow, or object to place one point contact.\n"
               "The contact uses the selected classification below."));
        m_contact_pick_btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_contact_pick_btn->setFixedHeight(Theme::kMediumBtnSz);
        rl->addWidget(m_contact_pick_btn);
        bl->addWidget(row);

        connect(m_contact_pick_btn, &QToolButton::toggled,
                this, [this](bool checked) {
                    emit contactToolChanged(checked ? 1 : 0);
                });
    }

    // ── Classification combo — point-object types only ────────────────────
    // "Feature" is intentionally absent: features are polygon/line annotations
    // handled by the separate FEATURE PICKING section (Phase 2).
    {
        auto* row = new QWidget;
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1, Theme::kSpacing4, Theme::kSpacing1);
        rl->setSpacing(Theme::kSpacing3);

        auto* lbl = new QLabel(tr("Classification"), row);
        lbl->setObjectName("wfSliderLabel");

        m_contact_class_combo = new QComboBox(row);
        m_contact_class_combo->addItems({
            tr("Boulder"),
            tr("Debris"),
            tr("Cable"),
            tr("Pipeline"),
            tr("Anomaly"),
            tr("Unknown"),
        });
        m_contact_class_combo->setObjectName("wfCombo");
        m_contact_class_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_contact_class_combo->setToolTip(
            tr("Classification assigned to the next contact pick.\n"
               "Use Boulder, Debris, Cable, Pipeline, or Anomaly when identifiable; use Unknown when unsure."));

        rl->addWidget(lbl);
        rl->addWidget(m_contact_class_combo);
        bl->addWidget(row);

        // Emit contactClassChanged whenever the combo selection changes so that
        // WaterfallView always uses the current classification for the next pick.
        static const ContactClass kClassMap[] = {
            ContactClass::Boulder,
            ContactClass::Debris,
            ContactClass::Cable,
            ContactClass::Pipeline,
            ContactClass::Anomaly,
            ContactClass::Unknown,
        };
        connect(m_contact_class_combo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) {
                    const int n = static_cast<int>(
                        sizeof(kClassMap) / sizeof(kClassMap[0]));
                    emit contactClassChanged(
                        (idx >= 0 && idx < n) ? kClassMap[idx]
                                              : ContactClass::Unknown);
                });
    }

    // ── Clear button ──────────────────────────────────────────────────────
    {
        auto* row = new QWidget;
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1, Theme::kSpacing4, 10);

        auto* clear_btn = new QToolButton(row);
        clear_btn->setText(tr("Clear All Contacts"));
        clear_btn->setObjectName("wfToolBtn");
        clear_btn->setToolTip(
            tr("Remove all contact picks currently shown in this waterfall view.\n"
               "Use with care when reviewing targets."));
        clear_btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        clear_btn->setFixedHeight(Theme::kFormBtnH);
        rl->addWidget(clear_btn);
        bl->addWidget(row);

        connect(clear_btn, &QToolButton::clicked,
                this, &WaterfallAnalysisPanel::clearContactsRequested);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  FEATURE PICKING section — Phase 2 placeholder
//
//  Features are SHAPE annotations: polygons, polylines, freehand outlines.
//  Unlike contacts (single point picks), a feature encloses or traces an area
//  or boundary — e.g., a debris field outline, a cable corridor, a sand wave.
// ─────────────────────────────────────────────────────────────────────────────

void WaterfallAnalysisPanel::buildFeatureSection(QVBoxLayout* vl, QWidget* container)
{
    auto* bl = makeSection(tr("Feature Picking"), /*expanded=*/false, container, vl);

    auto* lbl = new QLabel(tr("Shape annotations (polygons, lines) \u2014 Phase 2"));
    lbl->setObjectName("wfSliderLabel");
    lbl->setContentsMargins(Theme::kSpacing5, Theme::kSpacing3, Theme::kSpacing4, Theme::kSpacing3);
    lbl->setEnabled(false);
    lbl->setWordWrap(true);
    lbl->setToolTip(
        tr("Feature picking is reserved for future line, polygon, and area annotations.\n"
           "Use Contact Picking for point targets in the current version."));
    bl->addWidget(lbl);
}

} // namespace dolphin::ui
