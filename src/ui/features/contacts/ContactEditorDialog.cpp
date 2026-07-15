// ContactEditorDialog.cpp — "Edit contact details" modal editor.
//
// Layout: a scrollable attribute form (left) beside the source-image viewer
// (right), with a command row (Delete · Prev/Next · Export · Close) underneath.
// Edits auto-commit as undoable diffs when navigating away / closing.

#include "ui/features/contacts/ContactEditorDialog.h"
#include "ui/features/contacts/ContactSnapshotView.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace dolphin::ui {

ContactEditorDialog::ContactEditorDialog(app::Project* project,
                                         std::vector<uint64_t> ordered_ids,
                                         uint64_t current_id,
                                         QWidget* parent)
    : QDialog(parent)
    , m_project(project)
    , m_ids(std::move(ordered_ids))
{
    setObjectName(QStringLiteral("contactEditor"));   // themed via AppStyleDialogs (ce*)
    setWindowTitle(tr("Edit contact details"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumSize(820, 560);
    resize(920, 600);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::kSpacing5, Theme::kSpacing4, Theme::kSpacing5, Theme::kSpacing4);
    root->setSpacing(Theme::kSpacing4);

    auto* body = new QHBoxLayout;
    body->setSpacing(Theme::kSpacing5);
    root->addLayout(body, 1);

    // Left: the attribute form (fixed column, no scroll — the dialog is sized to
    // fit). Kept narrow so the source image stays the dominant element.
    auto* form = buildForm();
    form->setMinimumWidth(292);
    form->setMaximumWidth(318);
    body->addWidget(form, 0, Qt::AlignTop);

    body->addWidget(buildImagePane(), 1);
    root->addWidget(buildCommandRow());

    int start = 0;
    for (int i = 0; i < static_cast<int>(m_ids.size()); ++i)
        if (m_ids[i] == current_id) { start = i; break; }
    loadIndex(start);
}

// ---------------------------------------------------------------------------
//  Image pane
// ---------------------------------------------------------------------------

QWidget* ContactEditorDialog::buildImagePane()
{
    auto* host = new QWidget;
    auto* vl = new QVBoxLayout(host);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(Theme::kSpacing2);

    // Header: "Source image:" caption + Export (reference layout).
    {
        auto* row = new QWidget(host);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);
        auto* lbl = new QLabel(tr("Source image:"), row);
        lbl->setObjectName(QStringLiteral("ceFieldLabel"));
        m_source_combo = new QComboBox(row);
        m_source_combo->setObjectName(QStringLiteral("ceCombo"));
        m_source_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_export_btn = new QPushButton(tr("Export"), row);
        m_export_btn->setObjectName(QStringLiteral("ceExportBtn"));
        m_export_btn->setToolTip(tr("Export this contact as a CSV / PDF / Word report."));
        rl->addWidget(lbl, 0);
        rl->addWidget(m_source_combo, 1);
        rl->addWidget(m_export_btn, 0);
        vl->addWidget(row);
    }

    // Framed viewer card around the snapshot.
    {
        auto* frame = new QFrame(host);
        frame->setObjectName(QStringLiteral("ceImageFrame"));
        auto* fl2 = new QVBoxLayout(frame);
        fl2->setContentsMargins(1, 1, 1, 1);
        m_snap = new ContactSnapshotView(frame);
        fl2->addWidget(m_snap);
        vl->addWidget(frame, 1);
    }

    // Footer: position readout (left) + "Show / hide contact icon" (right).
    {
        auto* row = new QWidget(host);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);
        m_img_coords = new QLabel(QStringLiteral("—"), row);
        m_img_coords->setObjectName(QStringLiteral("ceFooter"));
        m_img_coords->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_show_icon = new QCheckBox(tr("Show / hide contact icon"), row);
        m_show_icon->setChecked(true);
        rl->addWidget(m_img_coords, 1);
        rl->addWidget(m_show_icon, 0);
        vl->addWidget(row);
    }

    // Scale + Rotation on one row (reference layout).
    {
        auto* row = new QWidget(host);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);

        auto slider = [&](QSlider*& sl, QLabel*& lbl, int lo, int hi, int val,
                          const QString& key) {
            auto* key_label = new QLabel(key, row);
            key_label->setObjectName(QStringLiteral("ceFieldLabel"));
            rl->addWidget(key_label, 0);
            sl = new QSlider(Qt::Horizontal, row);
            sl->setRange(lo, hi);
            sl->setValue(val);
            lbl = new QLabel(row);
            lbl->setObjectName(QStringLiteral("ceEcho"));
            lbl->setMinimumWidth(40);
            lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rl->addWidget(sl, 1);
            rl->addWidget(lbl, 0);
        };
        slider(m_scale_sl, m_scale_lbl, 25, 400, 100, tr("Scale:"));
        slider(m_rot_sl, m_rot_lbl, -180, 180, 0, tr("Rotation:"));
        vl->addWidget(row);
    }

    connect(m_scale_sl, &QSlider::valueChanged, this, [this](int v) {
        m_snap->setScalePercent(v);
        m_scale_lbl->setText(QStringLiteral("%1%").arg(v));
    });
    connect(m_rot_sl, &QSlider::valueChanged, this, [this](int v) {
        m_snap->setRotationDeg(v);
        m_rot_lbl->setText(QStringLiteral("%1°").arg(v));
    });
    connect(m_snap, &ContactSnapshotView::scaleChanged, m_scale_sl, [this](int v) {
        if (m_scale_sl->value() != v) m_scale_sl->setValue(v);
    });
    connect(m_show_icon, &QCheckBox::toggled, m_snap, &ContactSnapshotView::setShowMarker);
    connect(m_export_btn, &QPushButton::clicked, this, [this] {
        if (currentId() != 0) emit exportRequested(currentId());
    });

    m_scale_lbl->setText(QStringLiteral("100%"));
    m_rot_lbl->setText(QStringLiteral("0°"));
    return host;
}

// ---------------------------------------------------------------------------
//  Command row
// ---------------------------------------------------------------------------

QWidget* ContactEditorDialog::buildCommandRow()
{
    auto* host = new QWidget;
    auto* hl = new QHBoxLayout(host);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(Theme::kSpacing3);

    m_delete_btn = new QPushButton(tr("Delete"), host);
    m_delete_btn->setObjectName(QStringLiteral("ceDeleteBtn"));
    hl->addWidget(m_delete_btn);

    hl->addStretch(1);

    m_prev_btn = new QToolButton(host);
    m_prev_btn->setObjectName(QStringLiteral("ceNavBtn"));
    m_prev_btn->setText(tr("‹ Prev"));
    hl->addWidget(m_prev_btn);

    m_title_lbl = new QLabel(host);
    m_title_lbl->setObjectName(QStringLiteral("ceNavTitle"));
    m_title_lbl->setAlignment(Qt::AlignCenter);
    m_title_lbl->setMinimumWidth(140);
    hl->addWidget(m_title_lbl);

    m_next_btn = new QToolButton(host);
    m_next_btn->setObjectName(QStringLiteral("ceNavBtn"));
    m_next_btn->setText(tr("Next ›"));
    hl->addWidget(m_next_btn);

    hl->addStretch(1);

    auto* close_btn = new QPushButton(tr("Close"), host);
    close_btn->setObjectName(QStringLiteral("dlgBtnOk"));   // primary accent button
    close_btn->setDefault(true);
    hl->addWidget(close_btn);

    connect(m_prev_btn, &QToolButton::clicked, this, [this] { loadIndex(m_index - 1); });
    connect(m_next_btn, &QToolButton::clicked, this, [this] { loadIndex(m_index + 1); });
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_delete_btn, &QPushButton::clicked, this, [this] {
        const uint64_t id = currentId();
        if (id == 0) return;
        // Block the re-entrant refresh (and any commit of the stale form) while
        // the owner's undo/bus chain runs, then re-sync once from the project.
        m_loading = true;
        emit removeContactRequested(id);
        m_loading = false;
        refresh(m_project);
    });

    return host;
}

} // namespace dolphin::ui
