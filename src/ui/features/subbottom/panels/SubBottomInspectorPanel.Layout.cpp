// SubBottomInspectorPanel.Layout.cpp — constructor and section builder helpers.
#include "ui/features/subbottom/panels/SubBottomInspectorPanel.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kKeyW = 72;  // key column width in inspector rows

SubBottomInspectorPanel::SubBottomInspectorPanel(QWidget* parent)
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

    // -- LINES
    {
        auto* bl = makeSection("Lines", true, container, vl);

        m_lines_list = new QListWidget(container);
        m_lines_list->setObjectName("avLinesList");
        m_lines_list->setFrameShape(QFrame::NoFrame);
        m_lines_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_lines_list->setMaximumHeight(120);
        bl->addWidget(m_lines_list);

        connect(m_lines_list, &QListWidget::itemClicked,
                this, [this](QListWidgetItem* item) {
                    if (item) emit layerChangeRequested(item->data(Qt::UserRole).toString().toStdString());
                });
    }

    // -- SURVEY DATA -------------------------------------------------------
    {
        auto* bl = makeSection("Survey Data", true, container, vl);
        makeRow(bl, tr("Traces"),   m_val_traces);
        makeRow(bl, tr("Samples"),  m_val_samples);
        makeRow(bl, tr("Duration"), m_val_duration);
        makeRow(bl, tr("Length"),   m_val_length);
    }

    // -- SOURCE FILE -------------------------------------------------------
    {
        auto* bl = makeSection("Source File", true, container, vl);
        makeWideRow(bl, tr("File"),   m_val_filename);
        makeRow    (bl, tr("Format"), m_val_fmt_size);
    }

    // -- COORDINATE SYSTEM (collapsed) -------------------------------------
    {
        auto* bl = makeSection("Coordinate System", false, container, vl);
        makeRow(bl, tr("Source CRS"), m_val_crs);
        makeRow(bl, tr("Type"),       m_val_crs_kind);
    }

    // -- SONAR -------------------------------------------------------------
    {
        auto* bl = makeSection("Sonar", true, container, vl);
        makeRow(bl, tr("Frequency"), m_val_freq);
        makeRow(bl, tr("Sound spd"), m_val_speed);
    }

    // -- VESSEL (collapsed) ------------------------------------------------
    {
        auto* bl = makeSection("Vessel", false, container, vl);
        makeRow(bl, tr("Survey"), m_val_survey);
        makeRow(bl, tr("Vessel"), m_val_vessel);
    }

    // -- VIEW SETTINGS -----------------------------------------------------
    {
        auto* bl = makeSection("View Settings", true, container, vl);

        // Trace width
        auto* tw_row = new QWidget(container);
        tw_row->setFixedHeight(Theme::kPanelRowH);
        auto* twl    = new QHBoxLayout(tw_row);
        twl->setContentsMargins(Theme::kSpacing4, 2, Theme::kSpacing4, 2);
        twl->setSpacing(Theme::kSpacing2);

        auto* tw_lbl = new QLabel(tr("Trace width"), container);
        tw_lbl->setObjectName("avMetaKey");
        tw_lbl->setFixedWidth(kKeyW);

        m_trace_width_spin = new QSpinBox(container);
        m_trace_width_spin->setObjectName("sbpSpin");
        m_trace_width_spin->setRange(1, 20);
        m_trace_width_spin->setValue(2);
        m_trace_width_spin->setSuffix(tr(" px"));
        m_trace_width_spin->setToolTip(
            tr("Horizontal pixels per trace column.\n"
               "Increase to widen traces for easier visual inspection;\n"
               "decrease to fit more of the line on screen.\n"
               "Also adjustable with Ctrl+scroll in the view."));
        twl->addWidget(tw_lbl);
        twl->addWidget(m_trace_width_spin, 1);
        bl->addWidget(tw_row);

        // Depth scale
        auto* ds_row = new QWidget(container);
        ds_row->setFixedHeight(Theme::kPanelRowH);
        auto* dsl    = new QHBoxLayout(ds_row);
        dsl->setContentsMargins(Theme::kSpacing4, 2, Theme::kSpacing4, 2);
        dsl->setSpacing(Theme::kSpacing2);

        auto* ds_lbl = new QLabel(tr("Depth scale"), container);
        ds_lbl->setObjectName("avMetaKey");
        ds_lbl->setFixedWidth(kKeyW);

        m_depth_scale_spin = new QDoubleSpinBox(container);
        m_depth_scale_spin->setObjectName("sbpSpin");
        m_depth_scale_spin->setRange(0.05, 5.0);
        m_depth_scale_spin->setSingleStep(0.05);
        m_depth_scale_spin->setDecimals(2);
        m_depth_scale_spin->setValue(0.50);
        m_depth_scale_spin->setSuffix(tr(" px/samp"));
        m_depth_scale_spin->setToolTip(
            tr("Vertical pixels per sample.\n"
               "Increase to stretch the depth axis and reveal fine structure;\n"
               "decrease to fit more depth on screen.\n"
               "Also adjustable with Shift+scroll in the view."));
        dsl->addWidget(ds_lbl);
        dsl->addWidget(m_depth_scale_spin, 1);
        bl->addWidget(ds_row);

        connect(m_trace_width_spin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v) { emit traceWidthChanged(v); });
        connect(m_depth_scale_spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double v) { emit depthScaleChanged(static_cast<float>(v)); });
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

        auto* btn_prev = makeNavBtn(tr("◄  Prev Line"));
        btn_prev->setToolTip(tr("Open the previous sub-bottom line in the project."));
        auto* btn_next = makeNavBtn(tr("Next Line  ►"));
        btn_next->setToolTip(tr("Open the next sub-bottom line in the project."));

        connect(btn_prev, &QToolButton::clicked, this, &SubBottomInspectorPanel::prevLineRequested);
        connect(btn_next, &QToolButton::clicked, this, &SubBottomInspectorPanel::nextLineRequested);

        rl->addWidget(btn_prev, 1);
        rl->addWidget(btn_next, 1);
        fl->addWidget(row);
    }
}

// static
QVBoxLayout* SubBottomInspectorPanel::makeSection(const QString& title,
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

void SubBottomInspectorPanel::makeRow(QVBoxLayout*  bl,
                                       const QString& key,
                                       QLabel*&       val_out)
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
    val_out->setObjectName("avMetaVal");
    val_out->setWordWrap(true);
    val_out->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    rl->addWidget(k);
    rl->addWidget(val_out, 1);
    bl->addWidget(row);
}

void SubBottomInspectorPanel::makeWideRow(QVBoxLayout*  bl,
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

void SubBottomInspectorPanel::setProjectLayers(
    const std::vector<std::pair<std::string, std::string>>& layers)
{
    if (!m_lines_list) return;
    QSignalBlocker sb(m_lines_list);
    m_lines_list->clear();
    for (const auto& [id, label] : layers) {
        auto* item = new QListWidgetItem(QString::fromStdString(label));
        item->setData(Qt::UserRole, QString::fromStdString(id));
        m_lines_list->addItem(item);
    }
}

void SubBottomInspectorPanel::setActiveLine(const std::string& id)
{
    if (!m_lines_list) return;
    QSignalBlocker sb(m_lines_list);
    const QString qid = QString::fromStdString(id);
    for (int i = 0; i < m_lines_list->count(); ++i) {
        if (m_lines_list->item(i)->data(Qt::UserRole).toString() == qid) {
            m_lines_list->setCurrentRow(i);
            return;
        }
    }
}

} // namespace dolphin::ui

#include "moc_SubBottomInspectorPanel.cpp"
