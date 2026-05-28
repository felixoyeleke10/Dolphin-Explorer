// SubBottomSettingsDialog.cpp — constructor, read-back, colour helper, apply.
#include "ui/features/subbottom/SubBottomSettingsDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kMinW = 440;
static constexpr int kMinH = 380;

SubBottomSettingsDialog::SubBottomSettingsDialog(const SubBottomDisplayParams& params,
                                                  const ViewScale&              scale,
                                                  const SubBottomViewStyle&     style,
                                                  QWidget* parent)
    : QDialog(parent)
    , m_bt_color  (style.bt_color)
    , m_xhair_color(style.xhair_color)
    , m_grid_color (style.grid_color)
{
    setWindowTitle(tr("Sub-Bottom Settings"));
    setMinimumWidth(kMinW);
    setMinimumHeight(kMinH);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4);

    auto* tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    root->addWidget(tabs, 1);

    buildDisplayTab(tabs);
    buildAcquisitionTab(tabs);
    buildViewScaleTab(tabs);
    buildCrosshairTab(tabs);
    buildGridTab(tabs);

    // Populate from params
    m_palette_combo->setCurrentIndex(params.palette_index);
    m_gain_spin->setValue(static_cast<double>(params.gain));
    m_bt_check->setChecked(params.show_bottom_track);
    updateColorButton(m_bt_color_btn, m_bt_color);
    m_bt_thickness_spin->setValue(style.bt_thickness);
    m_speed_spin->setValue(static_cast<double>(params.sound_speed_ms));
    m_trace_w_spin->setValue(scale.px_per_trace);
    m_depth_s_spin->setValue(static_cast<double>(scale.px_per_sample));
    m_xhair_show_check->setChecked(style.xhair_show);
    updateColorButton(m_xhair_color_btn, m_xhair_color);
    m_xhair_style_combo->setCurrentIndex(
        [&] {
            switch (style.xhair_style) {
            case Qt::SolidLine:    return 0;
            case Qt::DashLine:     return 1;
            case Qt::DotLine:      return 2;
            case Qt::DashDotLine:  return 3;
            default:               return 1;
            }
        }());
    m_xhair_width_spin->setValue(style.xhair_width);
    m_xhair_opacity_spin->setValue(style.xhair_opacity);
    m_grid_show_check->setChecked(style.grid_show);
    updateColorButton(m_grid_color_btn, m_grid_color);
    m_grid_opacity_spin->setValue(style.grid_opacity);
    m_grid_interval_spin->setValue(static_cast<double>(style.grid_interval_ms));

    root->addSpacing(8);
    auto* bbox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bbox->button(QDialogButtonBox::Ok)->setObjectName("dlgBtnOk");
    auto* apply_btn = bbox->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
    root->addWidget(bbox);

    connect(apply_btn, &QPushButton::clicked, this, &SubBottomSettingsDialog::onApply);
    connect(bbox, &QDialogButtonBox::accepted, this, [this] { onApply(); accept(); });
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Read-back
// ─────────────────────────────────────────────────────────────────────────────

SubBottomDisplayParams SubBottomSettingsDialog::displayParams() const
{
    SubBottomDisplayParams p;
    p.palette_index     = m_palette_combo->currentIndex();
    p.gain              = static_cast<float>(m_gain_spin->value());
    p.show_bottom_track = m_bt_check->isChecked();
    p.sound_speed_ms    = static_cast<float>(m_speed_spin->value());
    return p;
}

SubBottomSettingsDialog::ViewScale SubBottomSettingsDialog::viewScale() const
{
    return { m_trace_w_spin->value(),
             static_cast<float>(m_depth_s_spin->value()) };
}

SubBottomViewStyle SubBottomSettingsDialog::viewStyle() const
{
    SubBottomViewStyle s;
    s.xhair_show    = m_xhair_show_check->isChecked();
    s.xhair_color   = m_xhair_color;
    s.xhair_style   = static_cast<Qt::PenStyle>(
        m_xhair_style_combo->currentData().toInt());
    s.xhair_width   = m_xhair_width_spin->value();
    s.xhair_opacity = m_xhair_opacity_spin->value();
    s.grid_show     = m_grid_show_check->isChecked();
    s.grid_color    = m_grid_color;
    s.grid_opacity  = m_grid_opacity_spin->value();
    s.grid_interval_ms = static_cast<float>(m_grid_interval_spin->value());
    s.bt_color      = m_bt_color;
    s.bt_thickness  = m_bt_thickness_spin->value();
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Colour button swatch helper
// ─────────────────────────────────────────────────────────────────────────────

// static
void SubBottomSettingsDialog::updateColorButton(QPushButton* btn, const QColor& c)
{
    btn->setStyleSheet(colorPickerSheet(c));
    btn->setText(c.name().toUpper());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Apply / persist
// ─────────────────────────────────────────────────────────────────────────────

void SubBottomSettingsDialog::onApply()
{
    const auto dp = displayParams();
    const auto vs = viewScale();
    const auto st = viewStyle();

    QSettings qs;
    qs.setValue("sbpDisplay/palette",  dp.palette_index);
    qs.setValue("sbpDisplay/gain",     static_cast<double>(dp.gain));
    qs.setValue("sbpDisplay/showBt",   dp.show_bottom_track);
    qs.setValue("sbpDisplay/speed",    static_cast<double>(dp.sound_speed_ms));
    qs.setValue("sbpView/pxPerTrace",  vs.px_per_trace);
    qs.setValue("sbpView/pxPerSample", static_cast<double>(vs.px_per_sample));
    qs.setValue("sbpStyle/xhairShow",    st.xhair_show);
    qs.setValue("sbpStyle/xhairColor",   st.xhair_color.name());
    qs.setValue("sbpStyle/xhairStyle",   static_cast<int>(st.xhair_style));
    qs.setValue("sbpStyle/xhairWidth",   st.xhair_width);
    qs.setValue("sbpStyle/xhairOpacity", st.xhair_opacity);
    qs.setValue("sbpStyle/gridShow",     st.grid_show);
    qs.setValue("sbpStyle/gridColor",    st.grid_color.name());
    qs.setValue("sbpStyle/gridOpacity",  st.grid_opacity);
    qs.setValue("sbpStyle/gridInterval", static_cast<double>(st.grid_interval_ms));
    qs.setValue("sbpStyle/btColor",      st.bt_color.name());
    qs.setValue("sbpStyle/btThickness",  st.bt_thickness);

    emit applied(dp, vs.px_per_trace, vs.px_per_sample, st);
}

} // namespace dolphin::ui
