// GeodesyPanel.cpp — left-context CRS / coordinate management panel

#include "ui/features/geodesy/GeodesyPanel.h"
#include "ui/shared/dialogs/CrsPickerDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"
#include "app/layers/DataLayer.h"
#include "app/project/Project.h"
#include "core/SpatialRef.h"
#include "geo/EpsgDatabase.h"

#include <QCheckBox>
#include <QFrame>
#include <QStyle>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kDotW = 14;  // CRS status dot indicator width

// -----------------------------------------------------------------------------
//  Construction
// -----------------------------------------------------------------------------

GeodesyPanel::GeodesyPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = makeCompactLayout<QVBoxLayout>(this);

    // Wrap everything in a scroll area so it works on small screens.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* body = new QWidget(scroll);
    body->setObjectName("panelBody");
    auto* body_l = new QVBoxLayout(body);
    body_l->setContentsMargins(Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4);
    body_l->setSpacing(Theme::kSpacing4);

    buildCrsSection(body_l);
    buildLayersSection(body_l);
    body_l->addStretch();

    scroll->setWidget(body);
    root->addWidget(scroll, 1);
}

// -----------------------------------------------------------------------------
//  Section builders
// -----------------------------------------------------------------------------

void GeodesyPanel::buildCrsSection(QVBoxLayout* root)
{
    auto* card = new QFrame(this);
    card->setObjectName("dlgSection");
    auto* vl = new QVBoxLayout(card);
    vl->setContentsMargins(Theme::kSpacing4, 10, Theme::kSpacing4, Theme::kSpacing4);
    vl->setSpacing(Theme::kSpacing3);

    auto* lbl = new QLabel(tr("Source CRS"), card);
    lbl->setObjectName("dlgSectionLabel");
    vl->addWidget(lbl);

    m_crs_value = new QLabel(card);
    m_crs_value->setObjectName("geoDetectedLabel");
    m_crs_value->setWordWrap(true);
    vl->addWidget(m_crs_value);

    auto* btn_row = new QWidget(card);
    auto* btn_hl  = makeCompactLayout<QHBoxLayout>(btn_row, Theme::kSpacing2);

    m_browse_btn = new QPushButton(tr("Browse…"), card);
    m_browse_btn->setObjectName("dlgBtnSecondary");
    m_browse_btn->setFixedHeight(Theme::kFormBtnH);

    m_clear_btn = new QPushButton(tr("Auto-detect"), card);
    m_clear_btn->setObjectName("dlgBtnSecondary");
    m_clear_btn->setFixedHeight(Theme::kFormBtnH);

    btn_hl->addWidget(m_browse_btn);
    btn_hl->addWidget(m_clear_btn);
    btn_hl->addStretch();
    vl->addWidget(btn_row);

    auto* note = new QLabel(
        tr("Set the coordinate system for files you import. "
           "Leave on Auto-detect if the file header is correct."), card);
    note->setObjectName("geoNoteLabel");
    note->setWordWrap(true);
    vl->addWidget(note);

    root->addWidget(card);

    connect(m_browse_btn, &QPushButton::clicked, this, [this]() {
        CrsPickerDialog picker(m_crs, this);
        if (picker.exec() != QDialog::Accepted) return;
        m_crs = picker.selectedRef();
        m_crs.exact = true;
        refreshCrsDisplay();
        emit crsChanged(m_crs);
    });

    connect(m_clear_btn, &QPushButton::clicked, this, [this]() {
        m_crs = {};
        refreshCrsDisplay();
        emit crsChanged(m_crs);
    });
}

void GeodesyPanel::buildLayersSection(QVBoxLayout* root)
{
    // -- Layer overview card -----------------------------------------------
    m_layers_card = new QFrame(this);
    m_layers_card->setObjectName("dlgSection");
    auto* vl = new QVBoxLayout(m_layers_card);
    vl->setContentsMargins(Theme::kSpacing4, 10, Theme::kSpacing4, 10);
    vl->setSpacing(Theme::kSpacing2);

    auto* hdr = new QLabel(tr("Survey Lines"), m_layers_card);
    hdr->setObjectName("dlgSectionLabel");
    vl->addWidget(hdr);

    m_no_proj_lbl = new QLabel(tr("No project open."), m_layers_card);
    m_no_proj_lbl->setObjectName("geoNoteLabel");
    m_no_proj_lbl->setWordWrap(true);
    vl->addWidget(m_no_proj_lbl);

    // Dynamic rows injected by refreshLayerList() go into m_layers_vl.
    auto* rows_w = new QWidget(m_layers_card);
    m_layers_vl  = makeCompactLayout<QVBoxLayout>(rows_w, 2);
    vl->addWidget(rows_w);

    root->addWidget(m_layers_card);

    // -- Apply card --------------------------------------------------------
    m_apply_card = new QFrame(this);
    m_apply_card->setObjectName("dlgSection");
    auto* avl = new QVBoxLayout(m_apply_card);
    avl->setContentsMargins(Theme::kSpacing4, 10, Theme::kSpacing4, Theme::kSpacing4);
    avl->setSpacing(Theme::kSpacing3);

    m_force_chk = new QCheckBox(
        tr("Also apply to lines that already have a confirmed CRS"), m_apply_card);
    m_force_chk->setObjectName("dlgCheckBox");
    avl->addWidget(m_force_chk);

    m_apply_btn = new QPushButton(tr("Apply CRS to Survey Lines"), m_apply_card);
    m_apply_btn->setObjectName("dlgBtnPrimary");
    m_apply_btn->setFixedHeight(Theme::kPanelRowH);
    avl->addWidget(m_apply_btn);

    auto* apply_note = new QLabel(
        tr("Geographic (WGS 84) lines are never affected."), m_apply_card);
    apply_note->setObjectName("geoNoteLabel");
    apply_note->setWordWrap(true);
    avl->addWidget(apply_note);

    root->addWidget(m_apply_card);

    connect(m_apply_btn, &QPushButton::clicked, this, [this]() {
        emit applyRequested(m_crs, m_force_chk->isChecked());
    });
}

// -----------------------------------------------------------------------------
//  Public refresh
// -----------------------------------------------------------------------------

void GeodesyPanel::refresh(app::Project* project, const core::SpatialRef& pending_crs)
{
    m_project = project;
    m_crs     = pending_crs;
    refreshCrsDisplay();
    refreshLayerList();
}

// -----------------------------------------------------------------------------
//  Private refresh helpers
// -----------------------------------------------------------------------------

void GeodesyPanel::refreshCrsDisplay()
{
    if (!m_crs_value) return;
    if (m_crs.empty()) {
        m_crs_value->setText(tr("Auto-detect from file header"));
        m_crs_value->setObjectName("geoAutoDetect");
    } else {
        m_crs_value->setText(
            QString::fromStdString(geo::epsgDisplayName(m_crs)));
        m_crs_value->setObjectName("geoDetectedLabel");
    }
    m_crs_value->style()->unpolish(m_crs_value);
    m_crs_value->style()->polish(m_crs_value);
    if (m_apply_btn)
        m_apply_btn->setEnabled(!m_crs.empty() && m_project != nullptr);
}

void GeodesyPanel::refreshLayerList()
{
    if (!m_layers_vl || !m_no_proj_lbl) return;

    // Clear old rows.
    while (QLayoutItem* item = m_layers_vl->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const bool has_project = (m_project != nullptr);
    m_no_proj_lbl->setVisible(!has_project);
    m_apply_card->setVisible(has_project);

    if (!has_project) return;

    const auto& layers = m_project->layers();
    if (layers.empty()) {
        auto* empty = new QLabel(tr("No survey lines imported yet."), this);
        empty->setObjectName("geoNoteLabel");
        m_layers_vl->addWidget(empty);
        return;
    }

    for (const auto& layer : layers) {
        if (!layer) continue;

        const auto& ref = layer->source_spatial_ref;

        const char* dot_state;
        QString tip;

        if (!core::spatialRefIsProjected(ref)) {
            dot_state = "ok";         // green — geographic / WGS84
            tip       = tr("Geographic (WGS 84)");
        } else if (ref.exact) {
            dot_state = "confirmed";  // blue — confirmed projected
            tip       = QString::fromStdString(geo::epsgDisplayName(ref));
        } else {
            dot_state = "warning";    // amber — projected, CRS not confirmed
            tip       = tr("Projected — CRS not confirmed");
        }

        auto* row = new QWidget(this);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 1, 0, 1);
        rl->setSpacing(Theme::kSpacing2);

        auto* dot = new QLabel("●", row);
        dot->setObjectName("geoLayerDot");
        dot->setProperty("state", dot_state);
        dot->style()->unpolish(dot);
        dot->style()->polish(dot);
        dot->setFixedWidth(kDotW);

        auto* name_lbl = new QLabel(
            QString::fromStdString(layer->label), row);
        name_lbl->setObjectName("geoLayerRow");
        name_lbl->setToolTip(tip);

        auto* crs_lbl = new QLabel(tip, row);
        crs_lbl->setObjectName("geoNoteLabel");
        crs_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        rl->addWidget(dot);
        rl->addWidget(name_lbl, 1);
        rl->addWidget(crs_lbl);
        m_layers_vl->addWidget(row);
    }
}

} // namespace dolphin::ui
