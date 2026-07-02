// WaterfallInspectorPanel.Layout.cpp — constructor and section builder helpers.
#include "ui/features/waterfall/panels/WaterfallInspectorPanel.h"
#include "render/sonar/SSSPalette.h"
#include "ui/features/waterfall/components/WfValueRow.h"
#include "ui/shared/dialogs/SettingsDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kKeyW      = 62;  // key column width in inspector rows
static constexpr int kToggleBtnW = 40;  // On/Off toggle button fixed width

WaterfallInspectorPanel::WaterfallInspectorPanel(QWidget* parent)
    : QFrame(parent)
{
    auto* fl = makeCompactLayout<QVBoxLayout>(this);

    // -- Scrollable content area -------------------------------------------
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("av_panel_scroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    fl->addWidget(scroll, 1);

    auto* container = new QWidget;
    auto* vl        = makeCompactLayout<QVBoxLayout>(container);
    scroll->setWidget(container);

    // -- FILES (expanded) --------------------------------------------------
    {
        auto* bl = makeSection("Files", true, container, vl);

        m_files_list = new QListWidget(container);
        m_files_list->setObjectName("avFilesList");
        m_files_list->setFrameShape(QFrame::NoFrame);
        m_files_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_files_list->setMaximumHeight(120);
        bl->addWidget(m_files_list);

        connect(m_files_list, &QListWidget::itemClicked,
                this, [this](QListWidgetItem* item) {
                    if (item)
                        emit layerChangeRequested(
                            item->data(Qt::UserRole).toString().toStdString());
                });
    }

    // -- SURVEY DATA (expanded) ---------------------------------------------
    {
        auto* bl = makeSection("Survey Data", true, container, vl);
        makeRow(bl, tr("Pings"),    m_val_pings);
        makeRow(bl, tr("Samples"),  m_val_samples);
        makeRow(bl, tr("Duration"), m_val_duration);
        makeRow(bl, tr("Length"),   m_val_length);
    }

    // -- COORDINATE SYSTEM (collapsed) -------------------------------------
    {
        auto* bl = makeSection("Coordinate System", false, container, vl);
        makeRow(bl, tr("Source CRS"), m_val_crs);
        makeRow(bl, tr("Type"),       m_val_crs_kind);

        // "Set Source CRS" button — shown only when source CRS is auto-detected / unconfirmed.
        m_btn_set_crs = new QPushButton(tr("Set Source CRS…"), container);
        m_btn_set_crs->setObjectName("setCrsButton");
        m_btn_set_crs->setToolTip(
            tr("Confirm or change the source coordinate reference system for this survey file.\n"
               "Applies to all layers imported from the same source.\n"
               "Use this when positions look shifted or the CRS is marked unconfirmed.\n"
               "The project display CRS (map target) is not affected."));
        m_btn_set_crs->setVisible(false);
        m_btn_set_crs->setFixedHeight(Theme::kInputH);
        bl->addWidget(m_btn_set_crs);

        connect(m_btn_set_crs, &QPushButton::clicked,
                this, &WaterfallInspectorPanel::setCrsRequested);
    }

    // -- SONAR (expanded) --------------------------------------------------
    {
        auto* bl = makeSection("Sonar", true, container, vl);
        makeRow(bl, tr("Model"),     m_val_sonar_model);
        makeRow(bl, tr("Frequency"), m_val_freq);
        makeRow(bl, tr("Sound spd"), m_val_sound_spd);
    }

    // -- VESSEL (collapsed) ------------------------------------------------
    {
        auto* bl = makeSection("Vessel", false, container, vl);
        makeRow(bl, tr("Survey"), m_val_survey);
        makeRow(bl, tr("Vessel"), m_val_vessel);
    }

    // -- VIEW SETTINGS (expanded) — palette combo --------------------------
    {
        auto* bl = makeSection("View Settings", true, container, vl);

        auto* row = new QWidget(container);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(Theme::kSpacing5, Theme::kSpacing1, Theme::kSpacing4, Theme::kSpacing1);
        rl->setSpacing(Theme::kSpacing2);

        auto* k = new QLabel(tr("Palette"), container);
        k->setObjectName("avMetaKey");
        k->setFixedWidth(kKeyW);
        k->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        m_palette_combo = new QComboBox(container);
        m_palette_combo->setObjectName("avPaletteCombo");
        m_palette_combo->setToolTip(
            tr("Choose the color palette used to draw the waterfall amplitudes.\n"
               "Greyscale is best for QC; high-contrast palettes can help reveal weak targets or shadows."));
        for (int i = 0; i < PaletteIndex::Count; ++i)
            m_palette_combo->addItem(SSSPalette::name(i));
        {
            QSettings qs;
            const QVariant sss_idx = qs.value(QStringLiteral("sss/paletteIdx"));
            if (sss_idx.isValid()) {
                m_palette_combo->setCurrentIndex(sss_idx.toInt());
            } else {
                m_palette_combo->setCurrentIndex(SSSPalette::indexFromName(
                    qs.value(SettingsDialog::kKeyDefaultPalette,
                             QStringLiteral("Gray")).toString()));
            }
        }

        rl->addWidget(k);
        rl->addWidget(m_palette_combo, 1);
        bl->addWidget(row);

        connect(m_palette_combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int idx) { emit paletteChanged(idx); });

        // -- Scale rows ----------------------------------------------------
        m_scale_v = new WfValueRow(tr("V. Scale"), 0, 300, 0, 1, 0, tr(" p/cm"), container);
        m_scale_v->setSpecialValueText(0.0, tr("Auto"));
        m_scale_v->setToolTip(
            tr("Vertical (along-track) scale.\n"
               "Sets how many pings appear per centimetre of screen height.\n"
               "Auto uses the default 2 px/ping row height.\n"
               "Increase to compress long lines; decrease toward Auto for detailed review."));
        bl->addWidget(m_scale_v);

        m_scale_h = new WfValueRow(tr("H. Scale"), 0, 3000, 0, 5, 0, tr(" s/cm"), container);
        m_scale_h->setSpecialValueText(0.0, tr("Auto"));
        m_scale_h->setToolTip(
            tr("Horizontal (across-track) scale.\n"
               "Sets how many range samples appear per centimetre of screen width.\n"
               "Auto fits the full swath to the available half-width.\n"
               "Increase to compress range; decrease toward Auto for target inspection."));
        bl->addWidget(m_scale_h);

        connect(m_scale_v, &WfValueRow::valueChanged, this, [this](double v) {
            emit verticalScaleChanged(static_cast<float>(v));
        });
        connect(m_scale_h, &WfValueRow::valueChanged, this, [this](double v) {
            emit horizontalScaleChanged(static_cast<float>(v));
        });

        // -- Amplitude bar toggle -------------------------------------------
        auto* amp_row = new QWidget(container);
        amp_row->setFixedHeight(Theme::kDialogBtnH);
        auto* arl = new QHBoxLayout(amp_row);
        arl->setContentsMargins(Theme::kSpacing5, Theme::kSpacing1, Theme::kSpacing4, Theme::kSpacing1);
        arl->setSpacing(Theme::kSpacing2);

        auto* amp_lbl = new QLabel(tr("Amp. Chart"), container);
        amp_lbl->setObjectName("avMetaKey");
        amp_lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        m_amp_bar_toggle = new QToolButton(container);
        m_amp_bar_toggle->setObjectName("avToggleBtn");
        m_amp_bar_toggle->setCheckable(true);
        {
            const bool amp_on = QSettings().value(
                QStringLiteral("waterfall/showAmpBar"), true).toBool();
            m_amp_bar_toggle->setChecked(amp_on);
            m_amp_bar_toggle->setText(amp_on ? tr("On") : tr("Off"));
        }
        m_amp_bar_toggle->setFixedWidth(kToggleBtnW);
        m_amp_bar_toggle->setToolTip(
            tr("Show or hide the amplitude profile chart at the bottom of the waterfall.\n"
               "Use it to inspect average signal strength across range and spot gain or noise problems."));
        connect(m_amp_bar_toggle, &QToolButton::toggled, this, [this](bool on) {
            m_amp_bar_toggle->setText(on ? tr("On") : tr("Off"));
            emit ampBarToggled(on);
        });

        arl->addWidget(amp_lbl);
        arl->addWidget(m_amp_bar_toggle);
        bl->addWidget(amp_row);
    }

    vl->addStretch();

    // -- NAV BUTTONS — pinned below scroll, always visible -----------------
    {
        auto* sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setObjectName("avHRule");
        fl->addWidget(sep);

        auto* row = new QWidget(this);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(10, Theme::kSpacing3, 10, 10);
        rl->setSpacing(Theme::kSpacing2);

        auto makeNavBtn = [&](const QString& text) -> QToolButton* {
            auto* b = new QToolButton(this);
            b->setText(text);
            b->setObjectName("avNavBtn");
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setFixedHeight(Theme::kDialogBtnH);
            return b;
        };

        m_btn_prev = makeNavBtn(tr("◄  Prev Line"));
        m_btn_prev->setToolTip(
            tr("Open the previous sidescan line in the project.\n"
               "Use this to compare adjacent survey lines."));
        m_btn_next = makeNavBtn(tr("Next Line  ►"));
        m_btn_next->setToolTip(
            tr("Open the next sidescan line in the project.\n"
               "Use this to continue line-by-line QC."));

        connect(m_btn_prev, &QToolButton::clicked, this, &WaterfallInspectorPanel::prevLineRequested);
        connect(m_btn_next, &QToolButton::clicked, this, &WaterfallInspectorPanel::nextLineRequested);

        rl->addWidget(m_btn_prev, 1);
        rl->addWidget(m_btn_next, 1);
        fl->addWidget(row);
    }
}

void WaterfallInspectorPanel::setNavEnabled(bool has_prev, bool has_next)
{
    if (m_btn_prev) m_btn_prev->setEnabled(has_prev);
    if (m_btn_next) m_btn_next->setEnabled(has_next);
}

// static
QVBoxLayout* WaterfallInspectorPanel::makeSection(const QString& title,
                                                   bool           expanded,
                                                   QWidget*       parent,
                                                   QVBoxLayout*   parent_layout)
{
    auto* hdr = new QPushButton(parent);
    hdr->setCheckable(true);
    hdr->setChecked(expanded);
    hdr->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    hdr->setObjectName("avCollapseHdr");
    hdr->setFixedHeight(Theme::kColorBtnH);
    hdr->setFlat(true);

    auto* body = new QWidget(parent);
    body->setObjectName("avCollapseBody");
    auto* bl = makeCompactLayout<QVBoxLayout>(body);
    body->setVisible(expanded);

    auto syncLabel = [hdr, title](bool checked) {
        hdr->setText(QString(checked ? "▼  " : "▶  ") + title);
    };
    syncLabel(expanded);

    QObject::connect(hdr, &QPushButton::toggled, body, &QWidget::setVisible);
    QObject::connect(hdr, &QPushButton::toggled, [syncLabel](bool c) { syncLabel(c); });

    parent_layout->addWidget(hdr);
    parent_layout->addWidget(body);
    return bl;
}

void WaterfallInspectorPanel::makeRow(QVBoxLayout*  bl,
                                       const QString& key,
                                       QLabel*&       val_out,
                                       const QString& obj_name)
{
    auto* row = new QWidget(this);
    auto* rl  = new QHBoxLayout(row);
    rl->setContentsMargins(Theme::kSpacing4, 3, Theme::kSpacing4, 3);
    rl->setSpacing(Theme::kSpacing3);

    auto* k = new QLabel(key, this);
    k->setObjectName("avMetaKey");
    k->setFixedWidth(kKeyW);
    k->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    val_out = new QLabel("—", this);
    val_out->setObjectName(obj_name);
    val_out->setWordWrap(true);
    val_out->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    rl->addWidget(k);
    rl->addWidget(val_out, 1);
    bl->addWidget(row);
}

void WaterfallInspectorPanel::makeWideRow(QVBoxLayout*  bl,
                                           const QString& key,
                                           QLabel*&       val_out)
{
    auto* grp = new QWidget(this);
    auto* gl  = new QVBoxLayout(grp);
    gl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing1, Theme::kSpacing4, 2);
    gl->setSpacing(1);

    auto* k = new QLabel(key, this);
    k->setObjectName("avMetaKey");
    k->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    val_out = new QLabel("—", this);
    val_out->setObjectName("avMetaValWide");
    val_out->setWordWrap(true);
    val_out->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    val_out->setTextFormat(Qt::PlainText);
    val_out->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    gl->addWidget(k);
    gl->addWidget(val_out);
    bl->addWidget(grp);
}

void WaterfallInspectorPanel::setProjectLayers(
    const std::vector<std::pair<std::string, std::string>>& layers)
{
    if (!m_files_list) return;
    QSignalBlocker sb(m_files_list);
    m_files_list->clear();
    for (const auto& [id, label] : layers) {
        auto* item = new QListWidgetItem(QString::fromStdString(label));
        item->setData(Qt::UserRole, QString::fromStdString(id));
        m_files_list->addItem(item);
    }
}

void WaterfallInspectorPanel::setActiveLine(const std::string& id)
{
    if (!m_files_list) return;
    QSignalBlocker sb(m_files_list);
    const QString qid = QString::fromStdString(id);
    for (int i = 0; i < m_files_list->count(); ++i) {
        if (m_files_list->item(i)->data(Qt::UserRole).toString() == qid) {
            m_files_list->setCurrentRow(i);
            return;
        }
    }
}

} // namespace dolphin::ui
