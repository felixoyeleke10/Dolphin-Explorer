// WaterfallWindow.Toolbar.cpp — toolbar build and command palette item provider.
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/shared/AppCommands.h"
#include "ui/shared/widgets/ViewerToolbar.h"
#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/shell/Theme.h"
#include "app/layers/DataLayer.h"

#include <QComboBox>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

using namespace Theme;

void WaterfallWindow::buildToolbar()
{
    m_toolbar = new ViewerToolbar(this);
    m_toolbar->setMetaButtonTip(
        tr("Open sidescan metadata, navigation, and channel information for this line."));
    m_toolbar->setCommandProvider([this] { return buildCommandItems(); });

    connect(m_toolbar, &ViewerToolbar::newRequested,      this, &WaterfallWindow::newFileRequested);
    connect(m_toolbar, &ViewerToolbar::openRequested,     this, &WaterfallWindow::openFileRequested);
    connect(m_toolbar, &ViewerToolbar::saveRequested,     this, &WaterfallWindow::saveFileRequested);
    connect(m_toolbar, &ViewerToolbar::metaRequested,     this, &WaterfallWindow::metadataRequested);
    connect(m_toolbar, &ViewerToolbar::settingsRequested, this, &WaterfallWindow::settingsRequested);

    // ── SSS-specific right section ────────────────────────────────────────────
    auto* btn_measure = m_toolbar->addButton(":/icons/measure.svg",
        tr("Measure distance on the waterfall. Shortcut: M. This tool is not enabled yet."));
    auto* btn_bttrack = m_toolbar->addButton(":/icons/bottom_track.svg",
        tr("Bottom Track tool for seabed picking/QC. Shortcut: B. Use the Seabed Picking panel for current controls."));
    auto* btn_range   = m_toolbar->addButton(":/icons/zoom.svg",
        tr("Range Gate tool for limiting inspection to a range interval. Shortcut: R. This tool is not enabled yet."));

    btn_measure->setEnabled(false);
    btn_bttrack->setEnabled(false);
    btn_range->setEnabled(false);

    m_freq_selector = new QComboBox(m_toolbar);
    m_freq_selector->setObjectName("wfFreqSelector");
    m_freq_selector->setFixedHeight(kWfFreqSelectorH);
    m_freq_selector->setMinimumWidth(kWfFreqSelectorW);
    m_freq_selector->setToolTip(
        tr("Select the sidescan frequency band to display.\n"
           "HF usually shows finer detail over shorter range; LF usually penetrates farther with coarser detail."));
    m_freq_selector->setVisible(false);
    m_toolbar->addWidget(m_freq_selector);
    m_toolbar->addSpacing(4);
    connect(m_freq_selector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WaterfallWindow::onFrequencyBandChanged);

    m_btn_contact = m_toolbar->addButton(":/icons/add_contact.svg",
        tr("Toggle contact picking. Shortcut: C.\n"
           "Click the waterfall on a target, shadow, cable, pipeline, debris, or anomaly to place a point contact."));
    m_btn_contact->setCheckable(true);

    qobject_cast<QVBoxLayout*>(layout())->insertWidget(0, m_toolbar);
}

QList<CommandPaletteItem> WaterfallWindow::buildCommandItems()
{
    QList<CommandPaletteItem> items;

    auto add = [&](const QString& cat, const QString& label,
                   const QString& sc,  const QString& aliases,
                   bool enabled, std::function<void()> fn)
    {
        items.append({cat, label, sc, aliases, enabled, false, std::move(fn)});
    };

    auto addCmd = [&](CommandId id, bool enabled, std::function<void()> fn) {
        items.append(toPaletteItem(id, enabled, std::move(fn)));
    };

    const bool has_layer = (m_layer != nullptr);

    add("Mode", tr("Navigate"), "1", "nav drive scroll",
        true, [this] { onModeChanged(ModeNavigate); });
    add("Mode", tr("Fix"),      "2", "fix edit position",
        true, [this] { onModeChanged(ModeFix); });
    add("Mode", tr("Review"),   "3", "review qc quality check inspect",
        true, [this] { onModeChanged(ModeReview); });
    add("Mode", tr("Analyze"),  "4", "analyze analysis image processing",
        true, [this] { onModeChanged(ModeAnalyze); });

    addCmd(CommandId::PrevLine, has_layer,
        [this] { emit prevLineRequested(m_layer ? m_layer->id : std::string{}); });
    addCmd(CommandId::NextLine, has_layer,
        [this] { emit nextLineRequested(m_layer ? m_layer->id : std::string{}); });

    add("Tools", tr("Add Contact"), "C", "contact pick mark",
        has_layer, [this] {
            if (m_btn_contact) m_btn_contact->setChecked(!m_btn_contact->isChecked());
        });

    add("Analysis", tr("Apply Display Params"),   "", "apply params gain",
        has_layer, [this] { pushParams(); flashProgress(); });
    add("Analysis", tr("Apply to All Lines"),     "", "apply all batch lines",
        has_layer, [this] {
            pushParams();
            flashProgress();
            m_status_left->setText(tr("Params applied to all lines"));
            emit paramsApplied();
            emit applyToAllRequested();
        });

    addCmd(CommandId::NewProject,  true, [this] { emit newFileRequested();  });
    addCmd(CommandId::OpenProject, true, [this] { emit openFileRequested(); });
    addCmd(CommandId::SaveProject, true, [this] { emit saveFileRequested(); });

    return items;
}

} // namespace dolphin::ui
