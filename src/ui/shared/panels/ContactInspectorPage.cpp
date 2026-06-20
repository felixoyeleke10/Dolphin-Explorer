#include "ui/shared/panels/ContactInspectorPage.h"
#include "ui/shell/Theme.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shared/widgets/ElidingLabel.h"
#include "ui/shared/widgets/CollapsibleSection.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace dolphin::ui {

namespace {
QString confidenceText(core::Confidence c)
{
    switch (c) {
    case core::Confidence::Probable: return QObject::tr("Probable");
    case core::Confidence::Certain:  return QObject::tr("Certain");
    default:                         return QObject::tr("Possible");
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
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Collapsible "Info" section — same framing as the layer inspector.
    auto* section = new CollapsibleSection(tr("Info"), this);
    section->setIcon(QStringLiteral(":/icons/panel_info.svg"));

    auto* body = new QWidget(section);
    auto* vl   = new QVBoxLayout(body);
    vl->setContentsMargins(0, 2, 0, Theme::kSpacing2);
    vl->setSpacing(0);

    makeRow(vl, tr("Label"),      m_label);
    makeRow(vl, tr("Class"),      m_class);
    makeRow(vl, tr("Confidence"), m_conf);
    makeRow(vl, tr("Lat"),        m_lat);
    makeRow(vl, tr("Lon"),        m_lon);
    m_depth_row = makeRow(vl, tr("Depth"), m_depth);
    m_range_row = makeRow(vl, tr("Range"), m_range);
    m_width_row = makeRow(vl, tr("Width"), m_width);
    m_notes_row = makeRow(vl, tr("Notes"), m_notes);
    m_notes->setWordWrap(true);   // notes are free text — allow wrapping over elide

    section->setContent(body);
    outer->addWidget(section);
    outer->addStretch(1);
}

QWidget* ContactInspectorPage::makeRow(QVBoxLayout* vl, const QString& key, ElidingLabel*& val_out)
{
    auto* row = new QWidget(this);
    auto* rl  = new QHBoxLayout(row);
    rl->setContentsMargins(Theme::kSpacing4, 3, Theme::kSpacing4, 3);
    rl->setSpacing(Theme::kSpacing3);

    auto* k = new QLabel(key, this);
    k->setObjectName("inspMetaKey");
    k->setFixedWidth(Theme::kKeyLabelW);
    k->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    val_out = new ElidingLabel(QStringLiteral("—"), this);
    val_out->setObjectName("inspMetaVal");
    val_out->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    rl->addWidget(k);
    rl->addWidget(val_out, 1);
    vl->addWidget(row);
    return row;
}

void ContactInspectorPage::setOptionalRow(QWidget* row, ElidingLabel* val, const QString& value)
{
    if (!row || !val) return;
    const QString trimmed = value.trimmed();
    row->setVisible(!trimmed.isEmpty());
    val->setText(trimmed);
}

void ContactInspectorPage::refresh(const core::Contact* c)
{
    if (!c) return;
    const bool proj = core::spatialRefIsProjected(c->spatial_ref);

    m_label->setText(QString::fromStdString(c->label));
    m_class->setText(c->classification.empty() ? tr("—")
                                               : QString::fromStdString(c->classification));
    m_conf->setText(confidenceText(c->confidence));
    m_lat->setText(formatCoord(c->lat, proj, 'N', 'S'));
    m_lon->setText(formatCoord(c->lon, proj, 'E', 'W'));

    setOptionalRow(m_depth_row, m_depth,
                   c->depth_m > 0.f ? tr("%1 m").arg(c->depth_m, 0, 'f', 2) : QString());
    setOptionalRow(m_range_row, m_range,
                   c->range_m > 0.f ? tr("%1 m").arg(c->range_m, 0, 'f', 2) : QString());
    setOptionalRow(m_width_row, m_width,
                   c->width_m > 0.f ? tr("%1 m").arg(c->width_m, 0, 'f', 2) : QString());
    setOptionalRow(m_notes_row, m_notes, QString::fromStdString(c->notes));
}

} // namespace dolphin::ui
