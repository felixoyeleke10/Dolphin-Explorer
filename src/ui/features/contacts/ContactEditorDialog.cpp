// ContactEditorDialog.cpp — "Edit contact details" modal editor.
//
// Layout: a scrollable attribute form (left) beside the source-image viewer
// (right), with a command row (Delete · Prev/Next · Export · Close) underneath.
// Edits auto-commit as undoable diffs when navigating away / closing.

#include "ui/features/contacts/ContactEditorDialog.h"
#include "ui/features/contacts/ContactSnapshotView.h"
#include "ui/features/contacts/ContactVisuals.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shell/Theme.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "core/NavPoint.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFrame>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace dolphin::ui {

using cmvis::contactSnapshotPath;

namespace {

// Symbol combo items: display label + stored id ("" = auto).
struct SymbolOpt { const char* label; const char* id; };
const SymbolOpt kSymbols[] = {
    { QT_TRANSLATE_NOOP("ContactEditorDialog", "Auto"),     ""         },
    { QT_TRANSLATE_NOOP("ContactEditorDialog", "Circle"),   "circle"   },
    { QT_TRANSLATE_NOOP("ContactEditorDialog", "Square"),   "square"   },
    { QT_TRANSLATE_NOOP("ContactEditorDialog", "Triangle"), "triangle" },
    { QT_TRANSLATE_NOOP("ContactEditorDialog", "Diamond"),  "diamond"  },
    { QT_TRANSLATE_NOOP("ContactEditorDialog", "Cross"),    "cross"    },
    { QT_TRANSLATE_NOOP("ContactEditorDialog", "Star"),     "star"     },
};

QDoubleSpinBox* makeMetreSpin()
{
    auto* s = new QDoubleSpinBox;
    s->setObjectName(QStringLiteral("ceSpin"));
    s->setRange(0.0, 100000.0);
    s->setDecimals(2);
    s->setSingleStep(0.1);
    s->setSuffix(QStringLiteral(" m"));
    s->setSpecialValueText(QStringLiteral("—"));   // 0 shows as em-dash
    s->setAlignment(Qt::AlignRight);
    return s;
}

QLabel* makeFieldLabel(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("ceFieldLabel"));
    return l;
}

// Editable fields the form owns — used to decide whether a commit is needed.
bool editableEqual(const core::Contact& a, const core::Contact& b)
{
    auto fe = [](float x, float y) { return std::fabs(x - y) < 1e-4f; };
    // Coordinate tolerance = half the spin-box resolution (0.01 m projected,
    // 1e-6° geographic) so the display rounding never registers as an edit.
    const double ceps = core::spatialRefIsProjected(a.spatial_ref) ? 5e-3 : 5e-7;
    auto de = [ceps](double x, double y) { return std::fabs(x - y) < ceps; };
    return a.label == b.label
        && de(a.lat, b.lat)
        && de(a.lon, b.lon)
        && a.symbol == b.symbol
        && a.color_rgb == b.color_rgb
        && a.classification == b.classification
        && a.confidence == b.confidence
        && fe(a.height_m, b.height_m)
        && a.height_not_measurable == b.height_not_measurable
        && fe(a.shadow_m, b.shadow_m)
        && fe(a.width_m, b.width_m)
        && fe(a.length_m, b.length_m)
        && fe(a.depth_m, b.depth_m)
        && fe(a.burial_depth_m, b.burial_depth_m)
        && a.notes == b.notes
        && a.use_for_report == b.use_for_report
        && a.tags == b.tags;
}

} // namespace

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
//  Form
// ---------------------------------------------------------------------------

QWidget* ContactEditorDialog::buildForm()
{
    auto* host = new QWidget;
    auto* fl = new QFormLayout(host);
    fl->setContentsMargins(0, 0, Theme::kSpacing2, 0);
    fl->setSpacing(Theme::kSpacing2);           // dense rows, like the reference
    fl->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fl->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    // Muted section headers keep the dense form scannable.
    auto addSection = [&](const QString& title, bool first = false) {
        auto* wrap = new QWidget(host);
        auto* wl   = new QVBoxLayout(wrap);
        wl->setContentsMargins(0, first ? 0 : Theme::kSpacing2, 0, 0);
        wl->setSpacing(2);
        auto* lbl = new QLabel(title, wrap);
        lbl->setObjectName(QStringLiteral("ceSection"));
        auto* line = new QFrame(wrap);
        line->setObjectName(QStringLiteral("ceDivider"));
        line->setFrameShape(QFrame::HLine);
        wl->addWidget(lbl);
        wl->addWidget(line);
        fl->addRow(wrap);
    };

    addSection(tr("IDENTIFICATION"), /*first=*/true);

    m_name = new QLineEdit(host);
    m_name->setObjectName(QStringLiteral("ceField"));
    fl->addRow(makeFieldLabel(tr("Name:")), m_name);

    // Symbol + Colour on one row (reference layout).
    {
        auto* row = new QWidget(host);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);
        m_symbol = new QComboBox(row);
        m_symbol->setObjectName(QStringLiteral("ceCombo"));
        for (const auto& s : kSymbols)
            m_symbol->addItem(tr(s.label), QString::fromLatin1(s.id));
        auto* col_lbl = makeFieldLabel(tr("Color:"));
        m_color_btn = new QPushButton(row);
        m_color_btn->setObjectName(QStringLiteral("ceColorBtn"));
        m_color_btn->setFixedSize(56, Theme::kFormBtnH);
        m_color_btn->setCursor(Qt::PointingHandCursor);
        rl->addWidget(m_symbol, 1);
        rl->addWidget(col_lbl, 0);
        rl->addWidget(m_color_btn, 0);
        fl->addRow(makeFieldLabel(tr("Symbol:")), row);
    }

    // Class + Confidence on one row.
    {
        auto* row = new QWidget(host);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);
        m_class = new QComboBox(row);
        m_class->setObjectName(QStringLiteral("ceCombo"));
        m_class->setEditable(true);
        m_class->addItems({ QString(), tr("Boulder"), tr("Debris"), tr("Cable"),
                            tr("Pipeline"), tr("Anomaly"), tr("Unknown") });
        auto* conf_lbl = makeFieldLabel(tr("Confidence:"));
        m_confidence = new QComboBox(row);
        m_confidence->setObjectName(QStringLiteral("ceCombo"));
        m_confidence->addItems({ tr("Possible"), tr("Probable"), tr("Certain") });
        rl->addWidget(m_class, 1);
        rl->addWidget(conf_lbl, 0);
        rl->addWidget(m_confidence, 0);
        fl->addRow(makeFieldLabel(tr("Class:")), row);
    }

    addSection(tr("POSITION"));

    // Editable position rows + WGS84 echo (labels/format set per CRS on load).
    auto coordRow = [&](QLabel*& row_label, QDoubleSpinBox*& spin, QLabel*& echo,
                        const QString& label) {
        auto* row = new QWidget(host);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);
        spin = new QDoubleSpinBox(row);
        spin->setObjectName(QStringLiteral("ceSpin"));
        spin->setRange(-1e8, 1e8);
        spin->setDecimals(2);
        spin->setAlignment(Qt::AlignRight);
        echo = new QLabel(row);
        echo->setObjectName(QStringLiteral("ceEcho"));
        echo->setMinimumWidth(104);
        rl->addWidget(spin, 1);
        rl->addWidget(echo, 0);
        row_label = makeFieldLabel(label);
        fl->addRow(row_label, row);
    };
    coordRow(m_coord_n_label, m_coord_n, m_coord_n_echo, tr("Northing:"));
    coordRow(m_coord_e_label, m_coord_e, m_coord_e_echo, tr("Easting:"));

    addSection(tr("DIMENSIONS"));

    // All dimension fields share one fixed width so the section reads as a
    // clean two-column grid (Width|Length, Shadow|Burial align vertically).
    constexpr int kDimW   = 88;    // spin column width
    constexpr int kDimLbl = 44;    // inner (2nd-column) label width

    // Height + "Not measurable".
    {
        auto* row = new QWidget(host);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);
        m_height = makeMetreSpin(); m_height->setParent(row);
        m_height->setFixedWidth(kDimW);
        m_height_nm = new QCheckBox(tr("Not measurable"), row);
        rl->addWidget(m_height, 0);
        rl->addWidget(m_height_nm, 0);
        rl->addStretch(1);
        fl->addRow(makeFieldLabel(tr("Height:")), row);
    }

    // Width + Length share a row; Shadow + Burial share a row — halves the
    // vertical footprint without losing any field.
    auto pairRow = [&](const QString& l1, QDoubleSpinBox*& s1,
                       const QString& l2, QDoubleSpinBox*& s2) {
        auto* row = new QWidget(host);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);
        s1 = makeMetreSpin(); s1->setParent(row);
        s1->setFixedWidth(kDimW);
        auto* lbl2 = makeFieldLabel(l2);
        lbl2->setFixedWidth(kDimLbl);
        lbl2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        s2 = makeMetreSpin(); s2->setParent(row);
        s2->setFixedWidth(kDimW);
        rl->addWidget(s1, 0);
        rl->addWidget(lbl2, 0);
        rl->addWidget(s2, 0);
        rl->addStretch(1);
        fl->addRow(makeFieldLabel(l1), row);
    };
    pairRow(tr("Width:"),  m_width,  tr("Length:"), m_length);
    pairRow(tr("Shadow:"), m_shadow, tr("Burial:"),  m_burial);

    // Depth stays in the same first column as the other spins.
    {
        auto* row = new QWidget(host);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing3);
        m_depth = makeMetreSpin(); m_depth->setParent(row);
        m_depth->setFixedWidth(kDimW);
        rl->addWidget(m_depth, 0);
        rl->addStretch(1);
        fl->addRow(makeFieldLabel(tr("Depth:")), row);
    }

    addSection(tr("NOTES"));

    // Tags: editable combo + add / clear, with the contact's tags listed below.
    {
        auto* row = new QWidget(host);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(Theme::kSpacing2);
        m_tags_combo = new QComboBox(row);
        m_tags_combo->setObjectName(QStringLiteral("ceCombo"));
        m_tags_combo->setEditable(true);
        m_tags_combo->setInsertPolicy(QComboBox::NoInsert);
        auto* add_btn = new QToolButton(row);
        add_btn->setObjectName(QStringLiteral("ceMiniBtn"));
        add_btn->setText(QStringLiteral("+"));
        add_btn->setToolTip(tr("Add the tag to this contact."));
        auto* clear_btn = new QToolButton(row);
        clear_btn->setObjectName(QStringLiteral("ceMiniBtn"));
        clear_btn->setText(QStringLiteral("⌫"));
        clear_btn->setToolTip(tr("Remove all tags from this contact."));
        rl->addWidget(m_tags_combo, 1);
        rl->addWidget(add_btn, 0);
        rl->addWidget(clear_btn, 0);
        fl->addRow(makeFieldLabel(tr("Tags:")), row);

        m_tags_list = new QListWidget(host);
        m_tags_list->setObjectName(QStringLiteral("ceTags"));
        m_tags_list->setFixedHeight(46);
        m_tags_list->setToolTip(tr("Tags on this contact. Double-click a tag to remove it."));
        m_tags_list->setVisible(false);   // shown only while it has tags
        fl->addRow(QString(), m_tags_list);

        auto syncTagsVisible = [this] {
            m_tags_list->setVisible(m_tags_list->count() > 0);
        };
        connect(add_btn, &QToolButton::clicked, this, &ContactEditorDialog::addTagFromCombo);
        connect(m_tags_combo->lineEdit(), &QLineEdit::returnPressed,
                this, &ContactEditorDialog::addTagFromCombo);
        connect(clear_btn, &QToolButton::clicked, this, [this, syncTagsVisible] {
            m_tags_list->clear();
            syncTagsVisible();
        });
        connect(m_tags_list, &QListWidget::itemDoubleClicked, this,
                [this, syncTagsVisible](QListWidgetItem* it) {
                    delete it;
                    syncTagsVisible();
                });
        // Covers every mutation path incl. loadContactIntoForm's clear+refill.
        connect(m_tags_list->model(), &QAbstractItemModel::rowsInserted,
                this, [syncTagsVisible] { syncTagsVisible(); });
        connect(m_tags_list->model(), &QAbstractItemModel::rowsRemoved,
                this, [syncTagsVisible] { syncTagsVisible(); });
        connect(m_tags_list->model(), &QAbstractItemModel::modelReset,
                this, [syncTagsVisible] { syncTagsVisible(); });
    }

    m_desc = new QPlainTextEdit(host);
    m_desc->setObjectName(QStringLiteral("ceText"));
    m_desc->setPlaceholderText(tr("Description / notes"));
    m_desc->setFixedHeight(72);
    fl->addRow(makeFieldLabel(tr("Description:")), m_desc);

    m_use_report = new QCheckBox(tr("Use for report"), host);
    fl->addRow(QString(), m_use_report);

    // Colour picker.
    connect(m_color_btn, &QPushButton::clicked, this, [this] {
        const QColor init = effectiveColor();
        const QColor c = QColorDialog::getColor(init, this, tr("Contact Colour"));
        if (c.isValid()) { setColorSwatch(c); }
    });

    // Height "Not measurable" disables the height spin.
    connect(m_height_nm, &QCheckBox::toggled, this, [this](bool nm) {
        if (m_height) m_height->setEnabled(!nm);
    });

    // Live WGS84 echo + footer readout while the position is edited.
    connect(m_coord_n, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { if (!m_loading) updateCoordEchoes(); });
    connect(m_coord_e, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { if (!m_loading) updateCoordEchoes(); });

    return host;
}

void ContactEditorDialog::setSnapshotProvider(
    std::function<QPixmap(const core::Contact&)> fn)
{
    m_snapshot_provider = std::move(fn);
    // The constructor loads the first contact before the owner installs the
    // provider — fetch now if that load came up empty.
    if (m_snapshot_provider && m_snap && !m_snap->hasPixmap() && m_index >= 0) {
        if (const core::Contact* c = findContact(currentId())) {
            const QPixmap pm = m_snapshot_provider(*c);
            if (!pm.isNull()) m_snap->setPixmap(pm);
        }
    }
}

void ContactEditorDialog::addTagFromCombo()
{
    if (!m_tags_combo || !m_tags_list) return;
    const QString tag = m_tags_combo->currentText().trimmed();
    if (tag.isEmpty()) return;
    for (int i = 0; i < m_tags_list->count(); ++i)
        if (m_tags_list->item(i)->text() == tag) return;   // no duplicates
    m_tags_list->addItem(tag);
    m_tags_combo->clearEditText();
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
        auto* lbl = makeFieldLabel(tr("Source image:"));
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
            rl->addWidget(makeFieldLabel(key), 0);
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

// ---------------------------------------------------------------------------
//  Data flow
// ---------------------------------------------------------------------------

const core::Contact* ContactEditorDialog::findContact(uint64_t id) const
{
    if (!m_project) return nullptr;
    for (const auto& c : m_project->contacts())
        if (c.id == id) return &c;
    return nullptr;
}

uint64_t ContactEditorDialog::currentId() const
{
    return (m_index >= 0 && m_index < static_cast<int>(m_ids.size()))
         ? m_ids[m_index] : 0;
}

QColor ContactEditorDialog::effectiveColor() const
{
    return m_color.isValid() ? m_color : QColor(255, 64, 64);
}

void ContactEditorDialog::setColorSwatch(const QColor& c)
{
    m_color = c;
    const QColor sw = effectiveColor();
    // Pure swatch: the chip fills the button; the value lives in the tooltip.
    QPixmap chip(42, 14);
    chip.fill(sw);
    m_color_btn->setText(QString());
    m_color_btn->setIcon(QIcon(chip));
    m_color_btn->setIconSize(chip.size());
    m_color_btn->setToolTip(m_color.isValid()
        ? tr("Contact colour %1 — click to change.")
              .arg(sw.name(QColor::HexRgb).toUpper())
        : tr("Contact colour: automatic (per classification) — click to choose."));
    if (!m_loading && m_snap) m_snap->setMarkerColor(sw);
}

void ContactEditorDialog::updateCoordEchoes()
{
    const bool proj = core::spatialRefIsProjected(m_before.spatial_ref);
    const double n = m_coord_n->value();
    const double e = m_coord_e->value();

    QString echo_n, echo_e, footer;
    if (proj) {
        // Try the projected → WGS84 transform (UTM-family CRSs); otherwise show
        // the CRS name so the operator still knows what the numbers mean.
        core::NavPoint in;
        in.lat          = n;
        in.lon          = e;
        in.is_projected = true;
        in.spatial_ref  = m_before.spatial_ref;
        in.valid        = true;
        core::NavPoint out;
        if (geo::normalizeNavForMap(in, core::makeWgs84SpatialRef(), out)) {
            echo_n = formatCoord(out.lat, false, 'N', 'S');
            echo_e = formatCoord(out.lon, false, 'E', 'W');
            footer = QStringLiteral("%1, %2 E (m)  ·  %3  %4")
                         .arg(n, 0, 'f', 2).arg(e, 0, 'f', 2)
                         .arg(echo_n, echo_e);
        } else {
            echo_n = spatialRefDisplayName(m_before.spatial_ref);
            footer = formatPosition(n, e, true);
        }
    } else {
        footer = formatPosition(n, e, false);
    }
    m_coord_n_echo->setText(echo_n);
    m_coord_e_echo->setText(echo_e);
    if (m_img_coords) m_img_coords->setText(footer);
}

void ContactEditorDialog::loadIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_ids.size())) return;
    if (index != m_index) commitIfChanged();

    m_index = index;
    const core::Contact* c = findContact(m_ids[index]);
    if (!c) { updateNavState(); return; }

    m_before = *c;
    loadContactIntoForm(*c);
    updateNavState();
    emit contactActivated(c->id);
}

void ContactEditorDialog::loadContactIntoForm(const core::Contact& c)
{
    m_loading = true;

    m_name->setText(QString::fromStdString(c.label));

    int sym_idx = 0;
    for (int i = 0; i < static_cast<int>(std::size(kSymbols)); ++i)
        if (c.symbol == kSymbols[i].id) { sym_idx = i; break; }
    m_symbol->setCurrentIndex(sym_idx);

    m_class->setCurrentText(QString::fromStdString(c.classification));

    m_color = (c.color_rgb != 0) ? QColor::fromRgba(c.color_rgb) : QColor();
    setColorSwatch(m_color);

    // Position: reconfigure the editable rows for the contact's CRS.
    const bool proj = core::spatialRefIsProjected(c.spatial_ref);
    if (proj) {
        m_coord_n_label->setText(tr("Northing:"));
        m_coord_e_label->setText(tr("Easting:"));
        m_coord_n->setRange(-1e8, 1e8);  m_coord_n->setDecimals(2);
        m_coord_e->setRange(-1e8, 1e8);  m_coord_e->setDecimals(2);
        m_coord_n->setSuffix(QStringLiteral(" m"));
        m_coord_e->setSuffix(QStringLiteral(" m"));
        m_coord_n->setSingleStep(1.0);
        m_coord_e->setSingleStep(1.0);
    } else {
        m_coord_n_label->setText(tr("Latitude:"));
        m_coord_e_label->setText(tr("Longitude:"));
        m_coord_n->setRange(-90.0, 90.0);    m_coord_n->setDecimals(6);
        m_coord_e->setRange(-180.0, 180.0);  m_coord_e->setDecimals(6);
        m_coord_n->setSuffix(QStringLiteral("°"));
        m_coord_e->setSuffix(QStringLiteral("°"));
        m_coord_n->setSingleStep(0.0001);
        m_coord_e->setSingleStep(0.0001);
    }
    m_coord_n->setValue(c.lat);   // northing == lat slot, easting == lon slot
    m_coord_e->setValue(c.lon);
    updateCoordEchoes();

    m_height->setValue(static_cast<double>(c.height_m));
    m_height_nm->setChecked(c.height_not_measurable);
    m_height->setEnabled(!c.height_not_measurable);
    m_shadow->setValue(static_cast<double>(c.shadow_m));
    m_width->setValue(static_cast<double>(c.width_m));
    m_length->setValue(static_cast<double>(c.length_m));
    m_depth->setValue(static_cast<double>(c.depth_m));
    m_burial->setValue(static_cast<double>(c.burial_depth_m));

    m_confidence->setCurrentIndex(static_cast<int>(c.confidence));

    // Tags: this contact's tags in the list; project-wide tags as suggestions.
    m_tags_list->clear();
    for (const auto& t : c.tags)
        m_tags_list->addItem(QString::fromStdString(t));
    m_tags_combo->clear();
    if (m_project) {
        QStringList suggestions;
        for (const auto& pc : m_project->contacts())
            for (const auto& t : pc.tags) {
                const QString qt = QString::fromStdString(t);
                if (!suggestions.contains(qt)) suggestions << qt;
            }
        suggestions.sort(Qt::CaseInsensitive);
        m_tags_combo->addItems(suggestions);
    }
    m_tags_combo->clearEditText();

    m_desc->setPlainText(QString::fromStdString(c.notes));
    m_use_report->setChecked(c.use_for_report);

    // Source caption: the source FILE name (never the internal layer id).
    QString source_name;
    if (m_project && !c.line_id.empty()) {
        if (auto* layer = m_project->findLayer(c.line_id)) {
            if (const auto* src = m_project->findSource(layer->source_id))
                source_name = QFileInfo(QString::fromStdString(src->path)).fileName();
            if (source_name.isEmpty())
                source_name = QString::fromStdString(layer->label);
        }
    }
    QString cap = !source_name.isEmpty() ? source_name : tr("Source image");
    if (c.range_m > 0.f)   // waterfall pick: annotate the picked channel
        cap += (c.sample_idx == 0) ? tr("  ·  Port") : tr("  ·  Starboard");
    m_source_combo->clear();
    m_source_combo->addItem(cap);

    // Snapshot: persisted PNG first; otherwise fetch from the source pings.
    QPixmap pm;
    const QString path = contactSnapshotPath(m_project, c.id);
    if (!path.isEmpty()) pm.load(path);
    if (pm.isNull() && m_snapshot_provider) pm = m_snapshot_provider(c);
    m_snap->setPixmap(pm);
    m_snap->resetView();
    m_scale_sl->setValue(100);
    m_rot_sl->setValue(0);
    m_snap->setMarkerColor(effectiveColor());
    m_show_icon->setChecked(true);

    m_loading = false;
}

core::Contact ContactEditorDialog::readForm() const
{
    core::Contact c = m_before;   // preserve id, coords, artifact, timestamps, group

    c.label          = m_name->text().trimmed().toStdString();
    c.symbol         = m_symbol->currentData().toString().toStdString();
    c.classification = m_class->currentText().trimmed().toStdString();
    c.color_rgb      = m_color.isValid() ? m_color.rgba() : 0u;

    // Position is editable (moving the pick); same slot convention as loading.
    // Only adopt the spin value when it truly changed, so an untouched position
    // keeps its full stored precision (the spins display rounded values).
    {
        const double ceps = core::spatialRefIsProjected(m_before.spatial_ref) ? 5e-3 : 5e-7;
        if (std::fabs(m_coord_n->value() - m_before.lat) >= ceps) c.lat = m_coord_n->value();
        if (std::fabs(m_coord_e->value() - m_before.lon) >= ceps) c.lon = m_coord_e->value();
    }

    c.height_not_measurable = m_height_nm->isChecked();
    c.height_m       = static_cast<float>(m_height->value());
    c.shadow_m       = static_cast<float>(m_shadow->value());
    c.width_m        = static_cast<float>(m_width->value());
    c.length_m       = static_cast<float>(m_length->value());
    c.depth_m        = static_cast<float>(m_depth->value());
    c.burial_depth_m = static_cast<float>(m_burial->value());

    c.confidence     = static_cast<core::Confidence>(
                           std::clamp(m_confidence->currentIndex(), 0, 2));

    c.notes          = m_desc->toPlainText().toStdString();
    c.use_for_report = m_use_report->isChecked();

    c.tags.clear();
    for (int i = 0; i < m_tags_list->count(); ++i)
        c.tags.push_back(m_tags_list->item(i)->text().toStdString());
    return c;
}

void ContactEditorDialog::commitIfChanged()
{
    if (m_loading || m_index < 0) return;
    if (!findContact(m_before.id)) return;   // contact vanished — nothing to commit
    core::Contact after = readForm();
    if (editableEqual(after, m_before)) return;
    emit contactSaveRequested(m_before, after);
    m_before = after;                        // reflect the committed state
}

void ContactEditorDialog::updateNavState()
{
    const int n = static_cast<int>(m_ids.size());
    if (m_prev_btn) m_prev_btn->setEnabled(m_index > 0);
    if (m_next_btn) m_next_btn->setEnabled(m_index >= 0 && m_index < n - 1);
    if (m_title_lbl && n > 0 && m_index >= 0) {
        const core::Contact* c = findContact(m_ids[m_index]);
        const QString name = c ? QString::fromStdString(c->label) : QString();
        m_title_lbl->setText(tr("%1  (%2 of %3)")
            .arg(name).arg(m_index + 1).arg(n));
    }
}

void ContactEditorDialog::showContact(std::vector<uint64_t> ordered_ids,
                                      uint64_t current_id)
{
    commitIfChanged();
    m_ids = std::move(ordered_ids);
    int idx = 0;
    for (int i = 0; i < static_cast<int>(m_ids.size()); ++i)
        if (m_ids[i] == current_id) { idx = i; break; }
    m_index = -1;                 // force reload without re-committing
    loadIndex(idx);
}

void ContactEditorDialog::refresh(app::Project* project)
{
    if (m_loading) return;   // mid-load / mid-delete — the caller re-syncs after
    m_project = project;

    // Drop ids that no longer exist; keep the current one selected if possible.
    const uint64_t want = currentId();
    std::vector<uint64_t> kept;
    kept.reserve(m_ids.size());
    for (uint64_t id : m_ids)
        if (findContact(id)) kept.push_back(id);
    m_ids = std::move(kept);

    if (m_ids.empty()) { accept(); return; }

    int  idx   = 0;
    bool found = false;
    for (int i = 0; i < static_cast<int>(m_ids.size()); ++i)
        if (m_ids[i] == want) { idx = i; found = true; break; }
    if (!found)
        idx = std::clamp(m_index, 0, static_cast<int>(m_ids.size()) - 1);
    m_index = idx;

    // If the current contact survived and its stored state still matches the
    // snapshot we loaded, the change was elsewhere — keep the form (and any
    // in-progress edits) untouched; only the nav counts may have moved.
    if (found) {
        const core::Contact* cur = findContact(want);
        if (cur && editableEqual(*cur, m_before)) { updateNavState(); return; }
    }

    m_index = -1;          // force a reload without committing the stale form
    loadIndex(idx);
}

void ContactEditorDialog::done(int r)
{
    // QDialog::accept()/reject() bypass closeEvent, so this is the single
    // funnel for every close path — commit the pending edit before closing.
    commitIfChanged();
    QDialog::done(r);
}

} // namespace dolphin::ui
