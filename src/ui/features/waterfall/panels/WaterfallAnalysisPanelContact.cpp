// WaterfallAnalysisPanelContact.cpp — automatic and manual contact workflows.
#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {
namespace {

QLabel* addSubheading(QVBoxLayout* layout, const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName("wfSubSectionLabel");
    label->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3,
                              Theme::kSpacing4, Theme::kSpacing1);
    layout->addWidget(label);
    return label;
}

QWidget* makeComboRow(const QString& label, QComboBox*& combo, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1,
                               Theme::kSpacing4, Theme::kSpacing1);
    layout->setSpacing(Theme::kSpacing3);
    auto* key = new QLabel(label, row);
    key->setObjectName("wfSliderLabel");
    combo = new QComboBox(row);
    combo->setObjectName("wfCombo");
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(key);
    layout->addWidget(combo);
    return row;
}

QToolButton* makeAction(const QString& text, QWidget* parent, bool primary = false)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setObjectName(primary ? "wfPrimaryBtn" : "wfToolBtn");
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setFixedHeight(Theme::kFormBtnH);
    return button;
}

} // namespace

void WaterfallAnalysisPanel::buildContactSection(QVBoxLayout* vl, QWidget* container)
{
    auto* body = makeSection(tr("Contact Picking"), true, container, vl);

    // Automatic candidate detection — a real scan of the currently loaded
    // physical-amplitude window, not a placeholder action.
    addSubheading(body, tr("AUTOMATIC DETECTION"), container);

    body->addWidget(makeComboRow(tr("Classification"), m_contact_class_combo, container));
    m_contact_class_combo->addItems({
        tr("Boulder"), tr("Debris"), tr("Cable"), tr("Pipeline"),
        tr("Anomaly"), tr("Unknown")});
    m_contact_class_combo->setToolTip(tr("Classification assigned to automatically detected contacts."));

    static const ContactClass classes[] = {
        ContactClass::Boulder, ContactClass::Debris, ContactClass::Cable,
        ContactClass::Pipeline, ContactClass::Anomaly, ContactClass::Unknown};
    connect(m_contact_class_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                emit contactClassChanged(index >= 0 && index < 6
                    ? classes[index] : ContactClass::Unknown);
            });

    QComboBox* sensitivity = nullptr;
    body->addWidget(makeComboRow(tr("Sensitivity"), sensitivity, container));
    sensitivity->addItems({tr("Conservative"), tr("Balanced"), tr("Sensitive")});
    sensitivity->setCurrentIndex(1);
    sensitivity->setToolTip(tr("Conservative returns fewer, stronger candidates. Sensitive finds more candidates and may include noise."));

    auto* scan_row = new QWidget(container);
    auto* scan_layout = new QHBoxLayout(scan_row);
    scan_layout->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1,
                                    Theme::kSpacing4, Theme::kSpacing3);
    auto* scan = makeAction(tr("Scan Loaded Window"), scan_row, true);
    scan->setToolTip(tr("Detect contact candidates in the pings currently loaded in this Waterfall window."));
    scan_layout->addWidget(scan);
    body->addWidget(scan_row);
    connect(scan, &QToolButton::clicked, this, [this, sensitivity] {
        emit automaticContactScanRequested(sensitivity->currentIndex());
    });

    // Manual picking — classification first, then one clear primary action.
    addSubheading(body, tr("MANUAL PICKING"), container);
    auto* pick_row = new QWidget(container);
    auto* pick_layout = new QHBoxLayout(pick_row);
    pick_layout->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1,
                                    Theme::kSpacing4, Theme::kSpacing2);
    m_contact_pick_btn = makeAction(tr("Pick Contact"), pick_row, true);
    m_contact_pick_btn->setCheckable(true);
    m_contact_pick_btn->setToolTip(tr("Enable manual picking, then click a target in the Waterfall image."));
    pick_layout->addWidget(m_contact_pick_btn);
    body->addWidget(pick_row);
    connect(m_contact_pick_btn, &QToolButton::toggled, this,
            [this](bool checked) { emit contactToolChanged(checked ? 1 : 0); });

    auto* manage_row = new QWidget(container);
    auto* manage_layout = new QHBoxLayout(manage_row);
    manage_layout->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1,
                                      Theme::kSpacing4, Theme::kSpacing3);
    manage_layout->setSpacing(Theme::kSpacing2);
    auto* edit = makeAction(tr("Review / Edit…"), manage_row);
    edit->setToolTip(tr("Review and edit contacts from this line."));
    auto* clear = makeAction(tr("Clear All"), manage_row);
    clear->setToolTip(tr("Remove all project contacts after confirmation."));
    manage_layout->addWidget(edit);
    manage_layout->addWidget(clear);
    body->addWidget(manage_row);
    connect(edit, &QToolButton::clicked, this, &WaterfallAnalysisPanel::editContactsRequested);
    connect(clear, &QToolButton::clicked, this, &WaterfallAnalysisPanel::clearContactsRequested);
}

} // namespace dolphin::ui
