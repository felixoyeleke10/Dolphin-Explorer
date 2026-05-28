#include "ui/shared/panels/ContactInspectorPage.h"
#include "ui/shell/Theme.h"
#include "ui/shared/CoordFormat.h"
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {
int confidenceIndex(core::Confidence c)
{
    switch (c) {
    case core::Confidence::Probable: return 1;
    case core::Confidence::Certain:  return 2;
    default:                         return 0;
    }
}
} // namespace

ContactInspectorPage::ContactInspectorPage(QWidget* parent)
    : QWidget(parent)
{
    build();
}

void ContactInspectorPage::build()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(Theme::kSpacing2, Theme::kSpacing2, Theme::kSpacing2, Theme::kSpacing2);

    auto* hdr = new QLabel(tr("Contact"), this);
    hdr->setObjectName("inspectorTitle");
    layout->addWidget(hdr);
    layout->addSpacing(8);

    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_label = new QLineEdit(this); m_label->setReadOnly(true);
    m_lat   = new QLineEdit(this); m_lat->setReadOnly(true);
    m_lon   = new QLineEdit(this); m_lon->setReadOnly(true);
    m_depth = new QLineEdit(this); m_depth->setReadOnly(true);
    m_range = new QLineEdit(this); m_range->setReadOnly(true);
    m_width = new QLineEdit(this); m_width->setReadOnly(true);

    form->addRow(tr("Label:"),    m_label);
    form->addRow(tr("Lat:"),      m_lat);
    form->addRow(tr("Lon:"),      m_lon);
    form->addRow(tr("Depth (m):"), m_depth);
    form->addRow(tr("Range (m):"), m_range);
    form->addRow(tr("Width (m):"), m_width);

    m_class = new QComboBox(this);
    m_class->addItems({tr("Boulder"), tr("Debris"), tr("Cable"),
                       tr("Pipeline"), tr("Anomaly"), tr("Unknown")});
    m_class->setEnabled(false);
    form->addRow(tr("Class:"), m_class);

    m_conf = new QComboBox(this);
    m_conf->addItems({tr("Possible"), tr("Probable"), tr("Certain")});
    m_conf->setEnabled(false);
    form->addRow(tr("Confidence:"), m_conf);

    layout->addLayout(form);
    layout->addSpacing(8);

    auto* notes_lbl = new QLabel(tr("Notes:"), this);
    layout->addWidget(notes_lbl);
    m_notes = new QTextEdit(this);
    m_notes->setReadOnly(true);
    m_notes->setMaximumHeight(80);
    layout->addWidget(m_notes);
    layout->addStretch();
}

void ContactInspectorPage::refresh(const core::Contact* c)
{
    if (!c) return;
    m_label->setText(QString::fromStdString(c->label));
    const bool proj = core::spatialRefIsProjected(c->spatial_ref);
    m_lat->setText(formatCoord(c->lat, proj, 'N', 'S'));
    m_lon->setText(formatCoord(c->lon, proj, 'E', 'W'));
    m_depth->setText(QString::number(c->depth_m, 'f', 2));
    m_range->setText(QString::number(c->range_m, 'f', 2));
    m_width->setText(QString::number(c->width_m, 'f', 2));
    const int ci = m_class->findText(QString::fromStdString(c->classification));
    m_class->setCurrentIndex(ci >= 0 ? ci : 0);
    m_conf->setCurrentIndex(confidenceIndex(c->confidence));
    m_notes->setPlainText(QString::fromStdString(c->notes));
}

} // namespace dolphin::ui
