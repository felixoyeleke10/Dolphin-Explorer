// SSSMetadataFieldPanel.cpp — field sidebar construction and per-field config slots.
#include "ui/features/metadata/SSSMetadataWindow.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kThickSpinW = 42;  // line-thickness spinbox width

// -----------------------------------------------------------------------------
//  Field panel construction
// -----------------------------------------------------------------------------

void SSSMetadataWindow::buildFieldPanel(QWidget* parent)
{
    auto* vl = new QVBoxLayout(parent);
    vl->setContentsMargins(Theme::kSpacing2, Theme::kSpacing2, Theme::kSpacing2, Theme::kSpacing2);
    vl->setSpacing(Theme::kSpacing1);

    auto* hdr = new QLabel("Metadata", parent);
    QFont f = hdr->font(); f.setBold(true); f.setPointSize(f.pointSize() + 1);
    hdr->setFont(f);
    vl->addWidget(hdr);

    auto* search = new QLineEdit(parent);
    search->setPlaceholderText("Search field…");
    vl->addWidget(search);

    m_field_list = new QListWidget(parent);
    for (int i = 0; i < SSSNavModel::kFieldCount; ++i) {
        QString label = SSSNavModel::kFieldDefs[i].name;
        if (SSSNavModel::kFieldDefs[i].unit[0] != '\0')
            label += QString(" (%1)").arg(SSSNavModel::kFieldDefs[i].unit);
        auto* item = new QListWidgetItem(label, m_field_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, i);
    }
    vl->addWidget(m_field_list, 1);

    auto* sep = new QFrame(parent);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    vl->addWidget(sep);

    m_cfg_panel = new QWidget(parent);
    auto* fl = new QFormLayout(m_cfg_panel);
    fl->setContentsMargins(0, 2, 0, 2);
    fl->setSpacing(Theme::kSpacing1);

    m_cfg_name = new QLabel("—", m_cfg_panel);
    QFont bf = m_cfg_name->font(); bf.setBold(true); m_cfg_name->setFont(bf);
    fl->addRow(m_cfg_name);

    m_cfg_plot_cb = new QCheckBox("Show in plot", m_cfg_panel);
    fl->addRow(m_cfg_plot_cb);

    auto* cw = new QWidget(m_cfg_panel);
    auto* crl = new QHBoxLayout(cw);
    crl->setContentsMargins(0, 0, 0, 0); crl->setSpacing(Theme::kSpacing1);
    m_cfg_color_btn = new QToolButton(cw);
    m_cfg_color_btn->setFixedSize(Theme::kSmallBtnSz, Theme::kSmallBtnSz);
    crl->addWidget(m_cfg_color_btn);
    crl->addWidget(new QLabel("Color", cw));
    crl->addStretch();
    crl->addWidget(new QLabel("Width:", cw));
    m_cfg_thick_sp = new QSpinBox(cw);
    m_cfg_thick_sp->setRange(1, 5);
    m_cfg_thick_sp->setFixedWidth(kThickSpinW);
    crl->addWidget(m_cfg_thick_sp);
    fl->addRow(cw);

    m_cfg_dots_cb = new QCheckBox("Add dots", m_cfg_panel);
    fl->addRow(m_cfg_dots_cb);

    m_cfg_prec_sp = new QSpinBox(m_cfg_panel);
    m_cfg_prec_sp->setRange(0, 9);
    fl->addRow("Precision:", m_cfg_prec_sp);

    m_cfg_panel->setEnabled(false);
    vl->addWidget(m_cfg_panel);

    connect(search, &QLineEdit::textChanged,
            this, &SSSMetadataWindow::onSearchTextChanged);
    connect(m_field_list, &QListWidget::itemChanged,
            this, [this](QListWidgetItem*){ onFieldListItemChanged(); });
    connect(m_field_list, &QListWidget::currentRowChanged,
            this, &SSSMetadataWindow::onFieldListCurrentChanged);
    connect(m_cfg_plot_cb,   &QCheckBox::toggled,  this, &SSSMetadataWindow::onShowInPlotToggled);
    connect(m_cfg_color_btn, &QToolButton::clicked, this, &SSSMetadataWindow::onColorButtonClicked);
    connect(m_cfg_thick_sp,  QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SSSMetadataWindow::onThicknessChanged);
    connect(m_cfg_dots_cb,   &QCheckBox::toggled,  this, &SSSMetadataWindow::onDotsToggled);
    connect(m_cfg_prec_sp,   QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SSSMetadataWindow::onPrecisionChanged);
}

// -----------------------------------------------------------------------------
//  Field visibility
// -----------------------------------------------------------------------------

void SSSMetadataWindow::onFieldListItemChanged()
{
    applyFieldVisibility();
}

void SSSMetadataWindow::applyFieldVisibility()
{
    QVector<int> visible;
    for (int i = 0; i < m_field_list->count(); ++i) {
        const auto* item = m_field_list->item(i);
        const int fi = item->data(Qt::UserRole).toInt();
        m_field_cfg[fi].visible = (item->checkState() == Qt::Checked);
        if (m_field_cfg[fi].visible) visible.append(fi);
    }
    m_model->setVisibleFields(visible);
}

void SSSMetadataWindow::onSearchTextChanged(const QString& text)
{
    const QString lc = text.toLower();
    for (int i = 0; i < m_field_list->count(); ++i) {
        auto* item = m_field_list->item(i);
        item->setHidden(!lc.isEmpty() && !item->text().toLower().contains(lc));
    }
}

// -----------------------------------------------------------------------------
//  Per-field config
// -----------------------------------------------------------------------------

void SSSMetadataWindow::onFieldListCurrentChanged(int row)
{
    if (row < 0) { m_selected_field = -1; m_cfg_panel->setEnabled(false); return; }
    const int fi = m_field_list->item(row)->data(Qt::UserRole).toInt();
    m_selected_field = fi;
    m_cfg_panel->setEnabled(true);
    updateFieldConfig(fi);
}

void SSSMetadataWindow::updateFieldConfig(int fi)
{
    m_updating_cfg = true;
    m_cfg_name->setText(QString::fromUtf8(SSSNavModel::kFieldDefs[fi].name));
    m_cfg_plot_cb->setChecked(m_field_cfg[fi].show_plot);
    m_cfg_color_btn->setStyleSheet(colorSwatchSheet(m_field_cfg[fi].color));
    m_cfg_thick_sp->setValue(m_field_cfg[fi].thickness);
    m_cfg_dots_cb->setChecked(m_field_cfg[fi].add_dots);
    const int prec = (m_field_cfg[fi].precision >= 0)
                     ? m_field_cfg[fi].precision
                     : SSSNavModel::kFieldDefs[fi].default_prec;
    m_cfg_prec_sp->setValue(prec);
    m_updating_cfg = false;
}

void SSSMetadataWindow::saveFieldConfig(int fi)
{
    m_model->setFieldPrecision(fi, m_field_cfg[fi].precision);
    for (int c = 0; c < m_model->columnCount(); ++c) {
        if (m_model->headerData(c, Qt::Horizontal, Qt::UserRole).toInt() == fi) {
            emit m_model->dataChanged(m_model->index(0, c),
                                      m_model->index(m_model->rowCount() - 1, c));
            break;
        }
    }
    updatePlot();
    updateChart();
}

void SSSMetadataWindow::onShowInPlotToggled(bool checked)
{
    if (m_updating_cfg || m_selected_field < 0) return;
    m_field_cfg[m_selected_field].show_plot = checked;
    updatePlot();
}

void SSSMetadataWindow::onColorButtonClicked()
{
    if (m_selected_field < 0) return;
    QColor c = QColorDialog::getColor(m_field_cfg[m_selected_field].color, this, "Field Color");
    if (!c.isValid()) return;
    m_field_cfg[m_selected_field].color = c;
    m_cfg_color_btn->setStyleSheet(colorSwatchSheet(c));
    updatePlot();
    updateChart();
}

void SSSMetadataWindow::onThicknessChanged(int v)
{
    if (m_updating_cfg || m_selected_field < 0) return;
    m_field_cfg[m_selected_field].thickness = v;
    updatePlot();
}

void SSSMetadataWindow::onDotsToggled(bool checked)
{
    if (m_updating_cfg || m_selected_field < 0) return;
    m_field_cfg[m_selected_field].add_dots = checked;
    updatePlot();
}

void SSSMetadataWindow::onPrecisionChanged(int v)
{
    if (m_updating_cfg || m_selected_field < 0) return;
    m_field_cfg[m_selected_field].precision = v;
    saveFieldConfig(m_selected_field);
}

} // namespace dolphin::ui
