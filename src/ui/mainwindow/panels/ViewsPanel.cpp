// ViewsPanel.cpp — left-panel per-viewer display settings (MAP | SSS | SBP).
#include "ui/mainwindow/panels/ViewsPanel.h"
#include "ui/features/map/MapTypes.h"
#include "ui/features/subbottom/SubBottomPalette.h"
#include "ui/shared/UiUtils.h"
#include "ui/shared/widgets/PanelTabBar.h"
#include "ui/shell/Theme.h"
#include "render/sonar/SSSPalette.h"
#include "render/sonar/SonarDisplayParams.h"

#include <QComboBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
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

    // Draping surface — bathymetry file the 3D view drapes/renders as terrain.
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

    gl->addWidget(makeRowLabel(tr("Draping surface"), page), 2, 0);
    auto* drape_row = new QWidget(page);
    auto* dl = makeCompactLayout<QHBoxLayout>(drape_row);
    dl->setSpacing(Theme::kSpacing1);
    dl->addWidget(m_drape_name, 1);
    dl->addWidget(m_drape_clear);
    dl->addWidget(m_drape_browse);
    gl->addWidget(drape_row, 2, 1, 1, 2);

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
    m_sss_palette->setToolTip(tr("Palette override for the active sidescan line"));
    connect(m_sss_palette, &QComboBox::activated, this, [this](int i) {
        emit sssPaletteSelected(m_sss_palette->itemData(i).toInt());
    });
    gl->addWidget(makeRowLabel(tr("Palette"), page), 0, 0);
    gl->addWidget(m_sss_palette, 0, 1);

    m_sss_hint = new QLabel(tr("Select a sidescan line to adjust it here."), page);
    m_sss_hint->setObjectName("viewsHint");
    m_sss_hint->setWordWrap(true);
    gl->addWidget(m_sss_hint, 1, 0, 1, 2);

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

    m_sbp_hint = new QLabel(tr("Select a sub-bottom line to adjust it here."), page);
    m_sbp_hint->setObjectName("viewsHint");
    m_sbp_hint->setWordWrap(true);
    gl->addWidget(m_sbp_hint, 1, 0, 1, 2);

    setSbpLayer(false, -1);
    return page;
}

// -- Setters (no re-emit) ----------------------------------------------------

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

void ViewsPanel::setSssLayer(bool has_layer, int palette_idx)
{
    m_sss_palette->setEnabled(has_layer);
    m_sss_hint->setVisible(!has_layer);
    if (has_layer) {
        const QSignalBlocker b(m_sss_palette);
        const int at = m_sss_palette->findData(
            palette_idx >= 0 ? palette_idx : PaletteIndex::Greyscale);
        if (at >= 0) m_sss_palette->setCurrentIndex(at);
    }
}

void ViewsPanel::setSbpLayer(bool has_layer, int palette_idx)
{
    m_sbp_palette->setEnabled(has_layer);
    m_sbp_hint->setVisible(!has_layer);
    if (has_layer) {
        const QSignalBlocker b(m_sbp_palette);
        const int at = m_sbp_palette->findData(
            palette_idx >= 0 ? palette_idx : SbpPalette::Greyscale);
        if (at >= 0) m_sbp_palette->setCurrentIndex(at);
    }
}

} // namespace dolphin::ui
