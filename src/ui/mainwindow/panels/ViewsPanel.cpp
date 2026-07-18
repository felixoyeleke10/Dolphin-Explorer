// ViewsPanel.cpp — left-panel per-viewer display settings (MAP | SSS | SBP).
#include "ui/mainwindow/panels/ViewsPanel.h"
#include "ui/features/map/MapTypes.h"
#include "ui/features/subbottom/SubBottomPalette.h"
#include "ui/shared/UiUtils.h"
#include "ui/shared/widgets/PanelTabBar.h"
#include "ui/shared/widgets/HistogramRangeSlider.h"
#include "ui/shell/Theme.h"
#include "render/sonar/SSSPalette.h"
#include "render/sonar/SonarDisplayParams.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {

QLabel* makeRowLabel(const QString& text, QWidget* parent)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setObjectName("viewsRowLabel");
    return lbl;
}

QComboBox* makeSssPaletteCombo(QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    combo->setObjectName("viewsCombo");
    combo->setFixedHeight(Theme::kSmallBtnSz + 4);
    for (int i = 0; i < PaletteIndex::Count; ++i)
        combo->addItem(QLatin1String(SSSPalette::name(i)), i);
    return combo;
}

QSpinBox* makeOpacitySpin(QWidget* parent)
{
    auto* spin = new QSpinBox(parent);
    spin->setObjectName("viewsSpin");
    spin->setFixedHeight(Theme::kSmallBtnSz + 4);
    spin->setRange(0, 100);
    spin->setValue(100);
    spin->setSuffix(QStringLiteral(" %"));
    spin->setToolTip(QObject::tr("Layer transparency on the map"));
    return spin;
}

} // namespace

ViewsPanel::ViewsPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("viewsPanel");
    auto* root = makeCompactLayout<QVBoxLayout>(this);

    m_tabs = new PanelTabBar(this);
    m_tabs->addTab(tr("MAP"), 0);
    m_tabs->addTab(tr("SSS"), 1);
    m_tabs->addTab(tr("SBP"), 2);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildMapPage());
    m_stack->addWidget(buildSssPage());
    m_stack->addWidget(buildSbpPage());

    m_tabs->setCurrentId(0);
    connect(m_tabs, &PanelTabBar::tabChanged,
            m_stack, &QStackedWidget::setCurrentIndex);

    root->addWidget(m_tabs);
    root->addWidget(m_stack);
}

// -- Pages -----------------------------------------------------------------

QWidget* ViewsPanel::buildMapPage()
{
    auto* page = new QWidget(this);
    auto* gl   = new QGridLayout(page);
    gl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3,
                           Theme::kSpacing3, Theme::kSpacing3);
    gl->setHorizontalSpacing(Theme::kSpacing3);
    gl->setVerticalSpacing(Theme::kSpacing2);
    gl->setColumnStretch(1, 1);

    // Palette — global sonar-mosaic colour map (DisplayStateManager authority).
    m_map_palette = makeSssPaletteCombo(page);
    m_map_palette->setToolTip(tr("Colour map for sonar mosaics on the map"));
    connect(m_map_palette, &QComboBox::activated, this, [this](int i) {
        emit mapPaletteSelected(m_map_palette->itemData(i).toInt());
    });
    gl->addWidget(makeRowLabel(tr("Palette"), page), 0, 0);
    gl->addWidget(m_map_palette, 0, 1, 1, 2);

    // Sonar preview tier (Off / Coverage only / Low / Medium / High).
    m_map_preview = new QComboBox(page);
    m_map_preview->setObjectName("viewsCombo");
    m_map_preview->setFixedHeight(Theme::kSmallBtnSz + 4);
    m_map_preview->setToolTip(tr("Sonar imagery rendering tier on the map"));
    m_map_preview->addItem(tr("Off"),           static_cast<int>(MapSonarQuality::Off));
    m_map_preview->addItem(tr("Coverage only"), static_cast<int>(MapSonarQuality::CoverageOnly));
    m_map_preview->addItem(tr("Low"),           static_cast<int>(MapSonarQuality::Low));
    m_map_preview->addItem(tr("Medium"),        static_cast<int>(MapSonarQuality::Medium));
    m_map_preview->addItem(tr("High"),          static_cast<int>(MapSonarQuality::High));
    connect(m_map_preview, &QComboBox::activated, this, [this](int i) {
        emit mapQualitySelected(m_map_preview->itemData(i).toInt());
    });
    gl->addWidget(makeRowLabel(tr("Sonar preview"), page), 1, 0);
    gl->addWidget(m_map_preview, 1, 1, 1, 2);

    gl->setRowStretch(2, 1);  // absorb leftover height so rows stay tight at top
    return page;
}

QWidget* ViewsPanel::buildSssPage()
{
    auto* page = new QWidget(this);
    auto* gl   = new QGridLayout(page);
    gl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3,
                           Theme::kSpacing3, Theme::kSpacing3);
    gl->setHorizontalSpacing(Theme::kSpacing3);
    gl->setVerticalSpacing(Theme::kSpacing2);
    gl->setColumnStretch(1, 1);

    m_sss_palette = makeSssPaletteCombo(page);
    m_sss_palette->setToolTip(tr("Palette used by all sidescan views"));
    connect(m_sss_palette, &QComboBox::activated, this, [this](int i) {
        emit sssPaletteSelected(m_sss_palette->itemData(i).toInt());
    });
    gl->addWidget(makeRowLabel(tr("Palette"), page), 0, 0);
    gl->addWidget(m_sss_palette, 0, 1);

    // Blend mode — how this line's mosaic composites over lines beneath it
    // (visible where surveys overlap). Cheap: a repaint, no re-raster.
    m_sss_blend = new QComboBox(page);
    m_sss_blend->setObjectName("viewsCombo");
    m_sss_blend->setFixedHeight(Theme::kSmallBtnSz + 4);
    m_sss_blend->setToolTip(tr("How this line blends over overlapping lines"));
    m_sss_blend->addItem(tr("Blend"),    0);
    m_sss_blend->addItem(tr("Cover up"), 1);
    m_sss_blend->addItem(tr("Lighten"),  2);
    m_sss_blend->addItem(tr("Darken"),   3);
    connect(m_sss_blend, &QComboBox::activated, this, [this](int i) {
        emit sssBlendSelected(m_sss_blend->itemData(i).toInt());
    });
    gl->addWidget(makeRowLabel(tr("Blend mode"), page), 1, 0);
    gl->addWidget(m_sss_blend, 1, 1);

    // Gain/Contrast intentionally live only in the right-panel Tools (the full
    // SSS imaging chain with Apply). Brightness on the left is the Dynamic-range
    // control below, which sets the black/white points on the histogram.

    m_sss_opacity = makeOpacitySpin(page);
    connect(m_sss_opacity, qOverload<int>(&QSpinBox::valueChanged),
            this, &ViewsPanel::sssOpacityEdited);
    gl->addWidget(makeRowLabel(tr("Transparency"), page), 2, 0);
    gl->addWidget(m_sss_opacity, 2, 1);

    // Draping surface — the project's XYZ/CSV bathymetry the 3D view renders as
    // terrain (one shared surface; moved here from the MAP tab). Project-global,
    // so it stays enabled regardless of the active line.
    m_drape_name = new QLabel(tr("None"), page);
    m_drape_name->setObjectName("viewsDrapeName");
    m_drape_name->setToolTip(tr("Bathymetry surface (XYZ/CSV) shown as terrain in the 3D view"));

    m_drape_browse = new QToolButton(page);
    m_drape_browse->setObjectName("viewsDrapeBtn");
    m_drape_browse->setText(QStringLiteral("…"));
    m_drape_browse->setToolTip(tr("Choose a bathymetry file (XYZ / CSV)"));
    m_drape_browse->setCursor(Qt::PointingHandCursor);
    connect(m_drape_browse, &QToolButton::clicked,
            this, &ViewsPanel::drapingBrowseRequested);

    m_drape_clear = new QToolButton(page);
    m_drape_clear->setObjectName("viewsDrapeBtn");
    m_drape_clear->setText(QStringLiteral("✕"));
    m_drape_clear->setToolTip(tr("Remove the draping surface"));
    m_drape_clear->setCursor(Qt::PointingHandCursor);
    m_drape_clear->hide();
    connect(m_drape_clear, &QToolButton::clicked,
            this, &ViewsPanel::drapingClearRequested);

    gl->addWidget(makeRowLabel(tr("Draping surface"), page), 3, 0);
    auto* drape_row = new QWidget(page);
    auto* dl = makeCompactLayout<QHBoxLayout>(drape_row);
    dl->setSpacing(Theme::kSpacing1);
    dl->addWidget(m_drape_name, 1);
    dl->addWidget(m_drape_clear);
    dl->addWidget(m_drape_browse);
    gl->addWidget(drape_row, 3, 1);

    // Clip the mosaic to drawn polygons (show sonar inside) + across-track beam
    // fan overlay. Both cheap repaints on the active line.
    m_sss_clip = new QCheckBox(tr("Clipping polygons"), page);
    m_sss_clip->setObjectName("viewsCheck");
    m_sss_clip->setToolTip(tr("Show sonar only inside polygons drawn with the feature tool"));
    connect(m_sss_clip, &QCheckBox::toggled, this, &ViewsPanel::sssClipPolygonsToggled);
    gl->addWidget(m_sss_clip, 4, 0, 1, 2);

    m_sss_beams = new QCheckBox(tr("Show side scan beams"), page);
    m_sss_beams->setObjectName("viewsCheck");
    m_sss_beams->setToolTip(tr(
        "Draw the across-track beam fan over the mosaic.\n"
        "Port beams are drawn in red, starboard in green."));
    connect(m_sss_beams, &QCheckBox::toggled, this, &ViewsPanel::sssShowBeamsToggled);
    gl->addWidget(m_sss_beams, 5, 0, 1, 2);

    m_sss_beam_spacing = new QSpinBox(page);
    m_sss_beam_spacing->setRange(1, 50);
    m_sss_beam_spacing->setValue(10);
    m_sss_beam_spacing->setSuffix(tr(" pings"));
    m_sss_beam_spacing->setToolTip(tr("Draw one beam line every N pings (1 = densest, 50 = sparsest)"));
    m_sss_beam_spacing->setEnabled(false);
    connect(m_sss_beams, &QCheckBox::toggled,
            m_sss_beam_spacing, &QSpinBox::setEnabled);
    connect(m_sss_beam_spacing, qOverload<int>(&QSpinBox::valueChanged),
            this, &ViewsPanel::sssBeamSpacingChanged);
    gl->addWidget(makeRowLabel(tr("Beam spacing"), page), 6, 0);
    gl->addWidget(m_sss_beam_spacing, 6, 1);

    // Dynamic range — black/white points on the amplitude histogram. The
    // handles set SonarDisplayParams::display_low/high; the caller re-rasters
    // on commit (drag release), keeping the histogram itself instant.
    gl->addWidget(makeRowLabel(tr("Dynamic range"), page), 7, 0, 1, 2);
    m_sss_hist = new HistogramRangeSlider(page);
    connect(m_sss_hist, &HistogramRangeSlider::rangeCommitted,
            this, &ViewsPanel::sssDynamicRangeCommitted);
    gl->addWidget(m_sss_hist, 8, 0, 1, 2);

    m_sss_hint = new QLabel(tr("Select a sidescan line to adjust it here."), page);
    m_sss_hint->setObjectName("viewsHint");
    m_sss_hint->setWordWrap(true);
    gl->addWidget(m_sss_hint, 9, 0, 1, 2);

    gl->setRowStretch(10, 1);
    setSssLayer(false, -1);
    return page;
}

QWidget* ViewsPanel::buildSbpPage()
{
    auto* page = new QWidget(this);
    auto* gl   = new QGridLayout(page);
    gl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3,
                           Theme::kSpacing3, Theme::kSpacing3);
    gl->setHorizontalSpacing(Theme::kSpacing3);
    gl->setVerticalSpacing(Theme::kSpacing2);
    gl->setColumnStretch(1, 1);

    m_sbp_palette = new QComboBox(page);
    m_sbp_palette->setObjectName("viewsCombo");
    m_sbp_palette->setFixedHeight(Theme::kSmallBtnSz + 4);
    m_sbp_palette->setToolTip(tr("Palette override for the active sub-bottom line"));
    for (int i = 0; i < SbpPalette::Count; ++i)
        m_sbp_palette->addItem(QLatin1String(SbpPalette::name(i)), i);
    connect(m_sbp_palette, &QComboBox::activated, this, [this](int i) {
        emit sbpPaletteSelected(m_sbp_palette->itemData(i).toInt());
    });
    gl->addWidget(makeRowLabel(tr("Palette"), page), 0, 0);
    gl->addWidget(m_sbp_palette, 0, 1);

    // Display controls (moved here from the right panel's Display section) —
    // live per-line: changes apply immediately to the active SBP line.
    auto emitDisplay = [this]() {
        emit sbpDisplayEdited(m_sbp_gain->value(), m_sbp_contrast->value(),
                              m_sbp_invert->isChecked());
    };

    m_sbp_gain = new QDoubleSpinBox(page);
    m_sbp_gain->setObjectName("viewsSpin");
    m_sbp_gain->setFixedHeight(Theme::kSmallBtnSz + 4);
    m_sbp_gain->setRange(0.1, 20.0);
    m_sbp_gain->setSingleStep(0.1);
    m_sbp_gain->setDecimals(1);
    m_sbp_gain->setSuffix(QStringLiteral(" ×"));
    m_sbp_gain->setToolTip(tr("Amplitude multiplier before palette mapping"));
    connect(m_sbp_gain, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [emitDisplay](double) { emitDisplay(); });
    gl->addWidget(makeRowLabel(tr("Gain"), page), 1, 0);
    gl->addWidget(m_sbp_gain, 1, 1);

    m_sbp_contrast = new QDoubleSpinBox(page);
    m_sbp_contrast->setObjectName("viewsSpin");
    m_sbp_contrast->setFixedHeight(Theme::kSmallBtnSz + 4);
    m_sbp_contrast->setRange(0.5, 3.0);
    m_sbp_contrast->setSingleStep(0.1);
    m_sbp_contrast->setDecimals(1);
    m_sbp_contrast->setToolTip(tr("Power-curve exponent; 1 = linear"));
    connect(m_sbp_contrast, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [emitDisplay](double) { emitDisplay(); });
    gl->addWidget(makeRowLabel(tr("Contrast"), page), 2, 0);
    gl->addWidget(m_sbp_contrast, 2, 1);

    m_sbp_invert = new QCheckBox(tr("Invert polarity"), page);
    m_sbp_invert->setObjectName("viewsCheck");
    m_sbp_invert->setToolTip(tr("Flip sample sign before palette mapping"));
    connect(m_sbp_invert, &QCheckBox::toggled,
            this, [emitDisplay](bool) { emitDisplay(); });
    gl->addWidget(m_sbp_invert, 3, 1);

    // Transparency (SSS/SBP parity with the SSS page above) — applies to the
    // 3D curtain; the SBP profile viewer itself has no map presence to fade.
    m_sbp_opacity = makeOpacitySpin(page);
    connect(m_sbp_opacity, qOverload<int>(&QSpinBox::valueChanged),
            this, &ViewsPanel::sbpOpacityEdited);
    gl->addWidget(makeRowLabel(tr("Transparency"), page), 4, 0);
    gl->addWidget(m_sbp_opacity, 4, 1);

    m_sbp_hint = new QLabel(tr("Select a sub-bottom line to adjust it here."), page);
    m_sbp_hint->setObjectName("viewsHint");
    m_sbp_hint->setWordWrap(true);
    gl->addWidget(m_sbp_hint, 5, 0, 1, 2);

    gl->setRowStretch(6, 1);
    setSbpLayer(false, -1);
    return page;
}

// -- Setters (no re-emit) ----------------------------------------------------

void ViewsPanel::setModalities(bool has_sss, bool has_sbp)
{
    if (auto* b = m_tabs->button(1)) { b->setVisible(has_sss); b->setEnabled(has_sss); }
    if (auto* b = m_tabs->button(2)) { b->setVisible(has_sbp); b->setEnabled(has_sbp); }
    // Current tab vanished → fall back to MAP.
    const int cur = m_tabs->currentId();
    if ((cur == 1 && !has_sss) || (cur == 2 && !has_sbp))
        setCurrentTab(0);
}

void ViewsPanel::setCurrentTab(int tab_id)
{
    if (tab_id < 0 || tab_id > 2) tab_id = 0;
    const auto* btn = m_tabs->button(tab_id);
    if (!btn || !btn->isVisible()) tab_id = 0;
    m_tabs->setCurrentId(tab_id);
    m_stack->setCurrentIndex(tab_id);
}

void ViewsPanel::setMapPalette(int palette_idx)
{
    const QSignalBlocker b(m_map_palette);
    const int at = m_map_palette->findData(palette_idx);
    if (at >= 0) m_map_palette->setCurrentIndex(at);
}

void ViewsPanel::setMapQuality(int quality_value)
{
    const QSignalBlocker b(m_map_preview);
    const int at = m_map_preview->findData(quality_value);
    if (at >= 0) m_map_preview->setCurrentIndex(at);
}

void ViewsPanel::setDrapingSurface(const QString& file_name)
{
    const bool has = !file_name.isEmpty();
    const QString shown = has ? file_name : tr("None");
    const QFontMetrics fm(m_drape_name->font());
    m_drape_name->setText(fm.elidedText(shown, Qt::ElideMiddle, 150));
    m_drape_name->setProperty("drapeSet", has);
    m_drape_name->style()->unpolish(m_drape_name);
    m_drape_name->style()->polish(m_drape_name);
    m_drape_clear->setVisible(has);
}

void ViewsPanel::setSssLayer(bool has_layer, int palette_idx,
                             int opacity_pct, int blend_mode,
                             bool clip_polygons, bool show_beams,
                             int beam_spacing)
{
    // Per-line controls follow the active line; draping surface is project-
    // global so it stays enabled whenever this page is shown.
    m_sss_palette->setEnabled(has_layer);
    m_sss_blend->setEnabled(has_layer);
    m_sss_opacity->setEnabled(has_layer);
    m_sss_clip->setEnabled(has_layer);
    m_sss_beams->setEnabled(has_layer);
    m_sss_beam_spacing->setEnabled(has_layer && show_beams);
    m_sss_hist->setEnabled(has_layer);
    m_sss_hint->setVisible(!has_layer);
    if (has_layer) {
        const QSignalBlocker b1(m_sss_palette);
        const QSignalBlocker b2(m_sss_opacity);
        const QSignalBlocker b3(m_sss_blend);
        const QSignalBlocker b4(m_sss_clip);
        const QSignalBlocker b5(m_sss_beams);
        const QSignalBlocker b6(m_sss_beam_spacing);
        const int at = m_sss_palette->findData(
            palette_idx >= 0 ? palette_idx : PaletteIndex::Greyscale);
        if (at >= 0) m_sss_palette->setCurrentIndex(at);
        const int bt = m_sss_blend->findData(blend_mode);
        if (bt >= 0) m_sss_blend->setCurrentIndex(bt);
        m_sss_opacity->setValue(opacity_pct);
        m_sss_clip->setChecked(clip_polygons);
        m_sss_beams->setChecked(show_beams);
        m_sss_beam_spacing->setValue(beam_spacing);
    } else {
        m_sss_hist->setHistogram({});   // clear bars → "select a line" hint
    }
}

void ViewsPanel::setSssDynamicRange(double low, double high)
{
    m_sss_hist->setRange(low, high);   // no signal on programmatic set
}

void ViewsPanel::setSssHistogram(std::vector<float> bins)
{
    m_sss_hist->setHistogram(std::move(bins));
}

void ViewsPanel::setSbpLayer(bool has_layer, int palette_idx,
                             double gain, double contrast, bool invert,
                             int opacity_pct)
{
    m_sbp_palette->setEnabled(has_layer);
    m_sbp_gain->setEnabled(has_layer);
    m_sbp_contrast->setEnabled(has_layer);
    m_sbp_invert->setEnabled(has_layer);
    m_sbp_opacity->setEnabled(has_layer);
    m_sbp_hint->setVisible(!has_layer);
    if (has_layer) {
        const QSignalBlocker b1(m_sbp_palette);
        const QSignalBlocker b2(m_sbp_gain);
        const QSignalBlocker b3(m_sbp_contrast);
        const QSignalBlocker b4(m_sbp_invert);
        const QSignalBlocker b5(m_sbp_opacity);
        const int at = m_sbp_palette->findData(
            palette_idx >= 0 ? palette_idx : SbpPalette::Greyscale);
        if (at >= 0) m_sbp_palette->setCurrentIndex(at);
        m_sbp_gain->setValue(gain);
        m_sbp_contrast->setValue(contrast);
        m_sbp_invert->setChecked(invert);
        m_sbp_opacity->setValue(opacity_pct);
    }
}

} // namespace dolphin::ui
