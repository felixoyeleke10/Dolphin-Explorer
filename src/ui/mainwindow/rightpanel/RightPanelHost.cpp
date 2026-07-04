#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/mainwindow/rightpanel/RightPanel.Info.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpGain.h"
#include "ui/mainwindow/rightpanel/RightPanel.SbpSignal.h"
#include "ui/mainwindow/rightpanel/RightPanel.Navigation.h"
#include "ui/mainwindow/rightpanel/RightPanel.Geometry.h"
#include "ui/mainwindow/rightpanel/RightPanel.Radiometry.h"
#include "ui/mainwindow/rightpanel/RightPanel.Enhancement.h"
#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "ui/mainwindow/panels/GainControlPanel.h"
#include "ui/mainwindow/panels/ImagingControlPanel.h"
#include "ui/shared/widgets/CollapsibleSection.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"
#include <QAction>
#include <QColor>
#include <QCursor>
#include <QFrame>
#include <QLayoutItem>
#include <QMenu>
#include <QSettings>
#include <QVBoxLayout>

namespace dolphin::ui {

RightPanelHost::RightPanelHost(ShowMode mode, QWidget* parent)
    : QWidget(parent)
    , m_show_mode(mode)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested,
            this, &RightPanelHost::showContextMenu);
    m_layout = makeCompactLayout<QVBoxLayout>(this);

    using M = app::Modality;
    if (m_show_mode == ShowMode::UniversalOnly) {
        // Universal modules: always relevant, never modality-filtered.
        m_info = new InfoModule(this);
        addModule(m_info);
    } else {
        // Modal modules: sensor-specific, shown/hidden by modality filter.
        // Removed on user direction: SBP Display (its view controls live in
        // the left Views panel's SBP tab) and Contact Picking (the top-toolbar
        // Contact tool + viewer toolbars are the picking surfaces).
        m_sbp_gain    = new SbpGainModule(this);
        m_sbp_signal  = new SbpSignalModule(this);
        m_radiometry  = std::make_unique<RadiometryModule>();
        m_enhancement = std::make_unique<EnhancementModule>();
        // Navigation + Geometry are per-modality: one instance per sensor tab so
        // SSS and SBP each get their own section (and their own panel for wiring).
        m_navigation_sss = std::make_unique<NavigationModule>(M::Sidescan);
        m_navigation_sbp = std::make_unique<NavigationModule>(M::SubBottom);
        m_geometry_sss   = std::make_unique<GeometryModule>(M::Sidescan);
        m_geometry_sbp   = std::make_unique<GeometryModule>(M::SubBottom);

        addModule(m_sbp_gain);
        addModule(m_sbp_signal);
        addModule(m_radiometry.get());
        addModule(m_enhancement.get());
        addModule(m_navigation_sss.get());
        addModule(m_geometry_sss.get());
        addModule(m_navigation_sbp.get());
        addModule(m_geometry_sbp.get());
    }

    m_layout->addStretch(1);
    m_default_order = m_modules;  // construction order — target for "Reset"
    applySavedOrder();  // restore the user's persisted section order
    clearLayer();  // start with all sections hidden
}

RightPanelHost::~RightPanelHost() = default;

void RightPanelHost::addModule(IRightPanelModule* mod)
{
    auto* sec = new CollapsibleSection(mod->title(), this);
    sec->setContent(mod->widget());
    // No per-section modality badge (SSS/SBP): the panel only ever shows the active
    // layer's tools, so the tag is redundant noise on every section header.
    sec->setIcon(mod->icon());

    // Drag-to-reorder: the user can rearrange the tool sections; the order persists.
    sec->setReorderable(true);
    connect(sec, &CollapsibleSection::reorderStarted, this,
            [this, sec]() { onSectionReorderStarted(sec); });
    connect(sec, &CollapsibleSection::reorderMoved, this,
            [this, sec](const QPoint& g) { onSectionReorderMoved(sec, g); });
    connect(sec, &CollapsibleSection::reorderFinished, this,
            [this, sec]() { onSectionReorderFinished(sec); });

    m_layout->addWidget(sec);
    m_modules.append(mod);
    m_sections.append(sec);
}

void RightPanelHost::setModalityFilter(app::Modality filter)
{
    m_modality_filter = filter;
    if (!m_current_layer) return;
    for (int i = 0; i < m_modules.size(); ++i)
        m_sections[i]->setVisible(computeFilterVisible(m_modules[i]->primaryModality()));
}

bool RightPanelHost::computeFilterVisible(app::Modality primary) const
{
    if (m_show_mode == ShowMode::UniversalOnly)
        return true;  // all modules in this host are Unknown-primary
    // ModalOnly: Unknown-primary modules are universal annotation tools (Contact
    // Picking / Feature Drawing) — shown on every tab including Map. Sensor-specific
    // sections show only under their own tab.
    if (primary == app::Modality::Unknown)
        return true;
    return primary == m_modality_filter;
}

void RightPanelHost::setLayer(app::DataLayer* layer)
{
    m_current_layer = layer;
    for (int i = 0; i < m_modules.size(); ++i) {
        const auto primary = m_modules[i]->primaryModality();
        // Always evaluate supports() so setApplicable is correct even for
        // sections that are currently hidden by the modality filter.
        const bool ok = m_modules[i]->supports(*layer);
        m_sections[i]->setApplicable(ok, m_modules[i]->notApplicableHint());
        if (ok) m_modules[i]->setLayer(layer);
        m_sections[i]->setVisible(computeFilterVisible(primary));
    }
}

void RightPanelHost::clearLayer()
{
    m_current_layer = nullptr;
    for (auto* sec : m_sections)
        sec->setVisible(false);
}

NavInfoPanel* RightPanelHost::navPanel(app::Modality m) const
{
    const auto& mod = (m == app::Modality::SubBottom) ? m_navigation_sbp : m_navigation_sss;
    return mod ? mod->panel() : nullptr;
}

HeadingInfoPanel* RightPanelHost::headingPanel(app::Modality m) const
{
    const auto& mod = (m == app::Modality::SubBottom) ? m_geometry_sbp : m_geometry_sss;
    return mod ? mod->panel() : nullptr;
}

GainControlPanel* RightPanelHost::gainPanel() const
{
    return m_radiometry ? m_radiometry->panel() : nullptr;
}

ImagingControlPanel* RightPanelHost::imagingPanel() const
{
    return m_enhancement ? m_enhancement->panel() : nullptr;
}

// The SSS palette moved to the status-bar picker; the right panel no longer hosts a
// palette control. These remain as no-ops so existing callers compile unchanged.
int RightPanelHost::currentPaletteIndex() const
{
    return 0;
}

void RightPanelHost::setPalette(int)
{
}

SbpGainModule* RightPanelHost::sbpGainModule() const
{
    return m_sbp_gain;
}

SbpSignalModule* RightPanelHost::sbpSignalModule() const
{
    return m_sbp_signal;
}

// -- Drag-to-reorder ----------------------------------------------------------

namespace {
QString orderSettingsKey(RightPanelHost::ShowMode mode)
{
    return QStringLiteral("rightPanel/sectionOrder/")
         + (mode == RightPanelHost::ShowMode::UniversalOnly
                ? QStringLiteral("universal") : QStringLiteral("modal"));
}
} // namespace

QString RightPanelHost::moduleKey(IRightPanelModule* mod) const
{
    if (!mod) return {};
    // Identity = modality + title (Navigation/Geometry exist per-modality with the
    // same title, so title alone is not unique).
    return QString::number(static_cast<int>(mod->primaryModality()))
         + QLatin1Char(':') + mod->title();
}

int RightPanelHost::dropTargetIndex(const QPoint& global_pos) const
{
    const int y = mapFromGlobal(global_pos).y();
    int target = -1, last_visible = -1;
    for (int i = 0; i < m_sections.size(); ++i) {
        auto* s = m_sections[i];
        if (!s->isVisible()) continue;
        last_visible = i;
        if (y < s->y() + s->height() / 2) { target = i; break; }
    }
    return target >= 0 ? target : last_visible + 1;   // -1+1 == 0 when nothing visible
}

void RightPanelHost::positionDropIndicator(int target_index)
{
    if (!m_drop_indicator) return;
    int y = 0;
    if (target_index >= 0 && target_index < m_sections.size()
            && m_sections[target_index]->isVisible()) {
        y = m_sections[target_index]->y();
    } else {
        int last = -1;
        for (int i = 0; i < m_sections.size(); ++i)
            if (m_sections[i]->isVisible()) last = i;
        y = (last >= 0) ? m_sections[last]->y() + m_sections[last]->height() : 0;
    }
    m_drop_indicator->setGeometry(0, qMax(0, y - 1), width(), 2);
    m_drop_indicator->raise();
    m_drop_indicator->show();
}

void RightPanelHost::onSectionReorderStarted(CollapsibleSection* sec)
{
    m_drag_section = sec;
    if (!m_drop_indicator) {
        m_drop_indicator = new QFrame(this);
        m_drop_indicator->setObjectName("sectionDropIndicator");
        m_drop_indicator->setStyleSheet(
            QStringLiteral("background:%1; border:none;")
                .arg(QColor(Theme::kAccent).name()));
        m_drop_indicator->hide();
    }
}

void RightPanelHost::onSectionReorderMoved(CollapsibleSection* sec, const QPoint& global_pos)
{
    if (m_drag_section != sec) return;
    positionDropIndicator(dropTargetIndex(global_pos));
}

void RightPanelHost::onSectionReorderFinished(CollapsibleSection* sec)
{
    if (m_drop_indicator) m_drop_indicator->hide();
    if (m_drag_section != sec) { m_drag_section = nullptr; return; }
    m_drag_section = nullptr;

    const int from = m_sections.indexOf(sec);
    const int target = dropTargetIndex(QCursor::pos());
    if (from < 0) return;
    moveSection(from, target);   // no-ops if the position is unchanged
}

void RightPanelHost::moveSection(int from, int target)
{
    if (from < 0 || from >= m_sections.size()) return;
    int insert = target;
    if (insert > from) --insert;                 // account for the removal shift
    if (insert < 0) insert = 0;
    if (insert > m_sections.size() - 1) insert = m_sections.size() - 1;
    if (insert == from) return;                  // unchanged

    IRightPanelModule*  mod = m_modules[from];
    CollapsibleSection* sec = m_sections[from];
    m_modules.removeAt(from);
    m_sections.removeAt(from);
    m_modules.insert(insert, mod);
    m_sections.insert(insert, sec);

    relayoutSections();
    // Re-assert modality-filter visibility (re-adding to the layout must not reveal
    // hidden sections of another sensor).
    if (m_current_layer)
        for (int i = 0; i < m_sections.size(); ++i)
            m_sections[i]->setVisible(computeFilterVisible(m_modules[i]->primaryModality()));
    saveOrder();
}

void RightPanelHost::relayoutSections()
{
    // Pull every item out of the layout (widgets stay parented to this), then re-add
    // the sections in their new list order followed by the trailing stretch.
    while (QLayoutItem* it = m_layout->takeAt(0))
        delete it;                                // frees the layout item, not the widget
    for (auto* s : m_sections) m_layout->addWidget(s);
    m_layout->addStretch(1);
}

void RightPanelHost::saveOrder() const
{
    QStringList keys;
    keys.reserve(m_modules.size());
    for (auto* m : m_modules) keys << moduleKey(m);
    QSettings().setValue(orderSettingsKey(m_show_mode), keys);
}

void RightPanelHost::applySavedOrder()
{
    const QStringList saved =
        QSettings().value(orderSettingsKey(m_show_mode)).toStringList();
    if (saved.isEmpty()) return;

    QVector<IRightPanelModule*>  new_mods;
    QVector<CollapsibleSection*> new_secs;
    QVector<bool> used(m_modules.size(), false);

    // Saved keys first, in saved order…
    for (const QString& k : saved) {
        for (int i = 0; i < m_modules.size(); ++i) {
            if (used[i] || moduleKey(m_modules[i]) != k) continue;
            new_mods << m_modules[i];
            new_secs << m_sections[i];
            used[i] = true;
            break;
        }
    }
    // …then any modules not present in the saved list (newly added in this build).
    for (int i = 0; i < m_modules.size(); ++i)
        if (!used[i]) { new_mods << m_modules[i]; new_secs << m_sections[i]; }

    if (new_mods.size() != m_modules.size()) return;  // safety: never drop a section
    m_modules  = new_mods;
    m_sections = new_secs;
    relayoutSections();
}

// -- Context menu -------------------------------------------------------------

void RightPanelHost::showContextMenu(const QPoint& local_pos)
{
    QMenu menu(this);

    QAction* expand   = menu.addAction(tr("Expand All"));
    QAction* collapse = menu.addAction(tr("Collapse All"));
    menu.addSeparator();
    QAction* reset = menu.addAction(tr("Reset Section Order"));
    // Reset is only meaningful once the order actually differs from the default.
    reset->setEnabled(m_modules != m_default_order);

    QAction* chosen = menu.exec(mapToGlobal(local_pos));
    if      (chosen == expand)   setAllExpanded(true);
    else if (chosen == collapse) setAllExpanded(false);
    else if (chosen == reset)    resetSectionOrder();

    // NOTE: do NOT raise()/activateWindow() the top-level window here. A QMenu is a
    // Qt::Popup and does not deactivate its parent window, so no re-assert is needed —
    // and on the frameless main window raise() forces a Z-order re-composite that
    // shows as a visible blink on every right-click.
}

void RightPanelHost::setAllExpanded(bool expanded)
{
    for (int i = 0; i < m_sections.size(); ++i)
        if (m_sections[i]->isVisible())
            m_sections[i]->setExpanded(expanded);
}

void RightPanelHost::resetSectionOrder()
{
    if (m_default_order.size() != m_modules.size()) return;

    // Reorder m_sections to match the construction order of m_modules.
    QVector<CollapsibleSection*> new_secs;
    new_secs.reserve(m_default_order.size());
    for (auto* mod : m_default_order) {
        const int idx = m_modules.indexOf(mod);
        if (idx < 0) return;                       // safety: never drop a section
        new_secs << m_sections[idx];
    }
    m_modules  = m_default_order;
    m_sections = new_secs;

    relayoutSections();
    if (m_current_layer)
        for (int i = 0; i < m_sections.size(); ++i)
            m_sections[i]->setVisible(computeFilterVisible(m_modules[i]->primaryModality()));

    QSettings().remove(orderSettingsKey(m_show_mode));   // back to default on next launch
}

} // namespace dolphin::ui
