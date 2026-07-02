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

// -----------------------------------------------------------------------------
//  CONTACT PICKING section
//
//  Contacts are single-point picks.  Each click while the pick tool is active
//  places one contact at (ping, channel, range) with the selected classification.
// -----------------------------------------------------------------------------

void WaterfallAnalysisPanel::buildContactSection(QVBoxLayout* vl, QWidget* container)
{
    auto* bl = makeSection(tr("Contact Picking"), /*expanded=*/true, container, vl);

    // -- Pick tool button --------------------------------------------------
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

    // -- Classification combo — point-object types only --------------------
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

    // -- Clear button ------------------------------------------------------
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

// -----------------------------------------------------------------------------
//  FEATURE PICKING section — Phase 2 placeholder
//
//  Features are SHAPE annotations: polygons, polylines, freehand outlines.
//  Unlike contacts (single point picks), a feature encloses or traces an area
//  or boundary — e.g., a debris field outline, a cable corridor, a sand wave.
// -----------------------------------------------------------------------------

void WaterfallAnalysisPanel::setFeatureToolActive(int tool)
{
    if (m_feature_draw_btn) {
        QSignalBlocker sb(m_feature_draw_btn);
        m_feature_draw_btn->setChecked(tool != 0);
    }
}

QString WaterfallAnalysisPanel::currentFeatureClassText() const
{
    return m_feature_class_combo ? m_feature_class_combo->currentText() : QString{};
}

void WaterfallAnalysisPanel::buildFeatureSection(QVBoxLayout* vl, QWidget* container)
{
    auto* bl = makeSection(tr("Feature Picking"), /*expanded=*/false, container, vl);

    // Emits featureToolChanged for the current draw-button + type-combo state.
    // tool: 0 = off, 1 = polygon, 2 = line.
    auto emitTool = [this]() {
        const bool on   = m_feature_draw_btn && m_feature_draw_btn->isChecked();
        const int  type = (m_feature_type_combo && m_feature_type_combo->currentIndex() == 1)
                              ? 2 : 1;   // 0=Polygon\u21921, 1=Line\u21922
        emit featureToolChanged(on ? type : 0);
    };

    // -- Draw toggle -------------------------------------------------------
    {
        auto* row = new QWidget;
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3, Theme::kSpacing4, Theme::kSpacing1);
        rl->setSpacing(Theme::kSpacing2);

        m_feature_draw_btn = new QToolButton(row);
        m_feature_draw_btn->setText(tr("Draw Feature"));
        m_feature_draw_btn->setCheckable(true);
        m_feature_draw_btn->setObjectName("wfToolBtn");
        m_feature_draw_btn->setToolTip(
            tr("Toggle feature drawing.\n"
               "Click the waterfall to add vertices; double-click or Enter to finish, "
               "Esc or right-click to cancel.\n"
               "Polygon encloses an area; Line traces an open path."));
        m_feature_draw_btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_feature_draw_btn->setFixedHeight(Theme::kMediumBtnSz);
        rl->addWidget(m_feature_draw_btn);
        bl->addWidget(row);

        connect(m_feature_draw_btn, &QToolButton::toggled, this, [emitTool](bool) { emitTool(); });
    }

    // -- Type combo (Polygon / Line) --------------------------------------
    {
        auto* row = new QWidget;
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1, Theme::kSpacing4, Theme::kSpacing1);
        rl->setSpacing(Theme::kSpacing3);

        auto* lbl = new QLabel(tr("Type"), row);
        lbl->setObjectName("wfSliderLabel");

        m_feature_type_combo = new QComboBox(row);
        m_feature_type_combo->addItems({ tr("Polygon"), tr("Line") });
        m_feature_type_combo->setObjectName("wfCombo");
        m_feature_type_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_feature_type_combo->setToolTip(tr("Polygon (closed area) or Line (open path)."));

        rl->addWidget(lbl);
        rl->addWidget(m_feature_type_combo);
        bl->addWidget(row);

        connect(m_feature_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [emitTool](int) { emitTool(); });
    }

    // -- Classification combo ---------------------------------------------
    {
        auto* row = new QWidget;
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1, Theme::kSpacing4, 10);
        rl->setSpacing(Theme::kSpacing3);

        auto* lbl = new QLabel(tr("Classification"), row);
        lbl->setObjectName("wfSliderLabel");

        m_feature_class_combo = new QComboBox(row);
        m_feature_class_combo->addItems({
            tr("Unclassified"),
            tr("Debris Field"),
            tr("Survey Zone"),
            tr("Exclusion Zone"),
            tr("Cable Corridor"),
            tr("Pipeline"),
            tr("Boundary"),
            tr("Sand Waves"),
        });
        m_feature_class_combo->setObjectName("wfCombo");
        m_feature_class_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_feature_class_combo->setToolTip(tr("Classification assigned to the next drawn feature."));

        rl->addWidget(lbl);
        rl->addWidget(m_feature_class_combo);
        bl->addWidget(row);
    }
}

} // namespace dolphin::ui
