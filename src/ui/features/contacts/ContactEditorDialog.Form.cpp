// ContactEditorDialog.Form.cpp — attribute-form construction and tag controls.

#include "ui/features/contacts/ContactEditorDialog.h"
#include "ui/shell/Theme.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

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

} // namespace

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

} // namespace dolphin::ui
