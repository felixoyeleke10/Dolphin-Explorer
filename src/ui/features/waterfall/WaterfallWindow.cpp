// WaterfallWindow.cpp — window layout, content assembly, and signal wiring.
//
// Toolbar build    → WaterfallWindow.Toolbar.cpp
// Status/progress  → WaterfallWindow.Status.cpp
// Data loading     → WaterfallWindowLoad.cpp
// Context menu     → WaterfallWindow.ContextMenu.cpp

#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/shared/UiUtils.h"
#include "ui/systems/AppState.h"
#include "ui/shared/widgets/CommandBar.h"

#include <QLabel>
#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/waterfall/WaterfallQcStrip.h"
#include "ui/features/waterfall/panels/WaterfallInspectorPanel.h"
#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/shell/Theme.h"
#include "app/layers/DataLayer.h"
#include "app/services/ImportService.h"
#include "core/Artifact.h"
#include "core/SpatialRef.h"
#include "geo/GeoUtils.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace dolphin::ui {

using namespace Theme;

static constexpr int kMinW     = 960;
static constexpr int kMinH     = 640;
static constexpr int kInitW    = 1340;
static constexpr int kInitH    = 820;

WaterfallWindow::WaterfallWindow(AppState* app_state, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_app_state(app_state)
{
    setWindowTitle(tr("Waterfall — Dolphin Explorer"));
    setWindowIcon(Theme::icon(":/icons/waterfall.svg"));
    setMinimumSize(kMinW, kMinH);
    resize(kInitW, kInitH);
    setAttribute(Qt::WA_DeleteOnClose, false);

    // Restore persisted settings (overlay applied to m_view below after it's created).
    const auto init_settings = WaterfallSettingsDialog::loadDefaults();
    m_window_size     = init_settings.window_size;
    m_display_channel = init_settings.display_channel;

    auto* root = makeCompactLayout<QVBoxLayout>(this);

    buildToolbar();

    auto* content = makeCompactLayout<QHBoxLayout>();

    m_inspector = new WaterfallInspectorPanel(this);
    m_inspector->setObjectName("av_inspector");
    m_inspector->setFixedWidth(kAvInspectorW);
    content->addWidget(m_inspector);

    auto* div_l = new QFrame(this);
    div_l->setFixedWidth(Theme::kSepSz);
    div_l->setObjectName("av_divider");
    content->addWidget(div_l);

    m_view = new WaterfallView(this);
    m_view->setOverlayParams(init_settings.overlay);
    m_show_amp_bar = init_settings.show_amp_bar;
    m_view->setShowAmpBar(m_show_amp_bar);
    // Apply persisted GPU acceleration preference (false = force CPU fallback).
    if (!QSettings().value(QStringLiteral("app/gpuAccel"), true).toBool())
        m_view->setGpuAccel(false);
    content->addWidget(m_view, 1);

    m_qc_strip = new WaterfallQcStrip(this);
    content->addWidget(m_qc_strip);

    m_vscroll = new QScrollBar(Qt::Vertical, this);
    m_vscroll->setObjectName("wf_vscroll");
    m_vscroll->setToolTip(
        tr("Scroll through the loaded sidescan waterfall line.\n"
           "Dragging beyond the current window loads the next section from disk."));
    m_vscroll->setFixedWidth(kAvScrollBarW);
    m_vscroll->setSingleStep(10);
    m_vscroll->setPageStep(100);
    content->addWidget(m_vscroll);

    auto* div_r = new QFrame(this);
    div_r->setFixedWidth(Theme::kSepSz);
    div_r->setObjectName("av_divider");
    content->addWidget(div_r);

    m_analysis = new WaterfallAnalysisPanel(this);
    m_analysis->setObjectName("av_analysis");
    m_analysis->setFixedWidth(kAvAnalysisW);
    content->addWidget(m_analysis);

    root->addLayout(content, 1);

    buildBottomBar();

    // -- Signal wiring ---------------------------------------------------------

    connect(m_inspector, &WaterfallInspectorPanel::prevLineRequested,
            this,        &WaterfallWindow::onPrevFix);
    connect(m_inspector, &WaterfallInspectorPanel::nextLineRequested,
            this,        &WaterfallWindow::onNextFix);
    connect(m_inspector, &WaterfallInspectorPanel::paletteChanged,
            this, [this](int idx) {
                // No QSettings write here: DisplayStateManager is the palette
                // authority and persists "sss/paletteIdx" when the emitted
                // signal below reaches it (via MainWindow::onPaletteChanged).
                WaterfallParams p = m_view->params();
                p.palette = idx;
                m_view->setParams(p);
                emit paletteChanged(idx);
            });
    connect(m_inspector, &WaterfallInspectorPanel::verticalScaleChanged,
            m_view, &WaterfallView::setVerticalScale);
    connect(m_inspector, &WaterfallInspectorPanel::horizontalScaleChanged,
            m_view, &WaterfallView::setHorizontalScale);
    connect(m_inspector, &WaterfallInspectorPanel::setCrsRequested,
            this, [this] {
                emit setCrsRequested(m_layer ? m_layer->id : std::string{});
            });
    connect(m_inspector, &WaterfallInspectorPanel::layerChangeRequested,
            this, &WaterfallWindow::layerChangeRequested);

    connect(m_analysis, &WaterfallAnalysisPanel::applyToLineRequested,
            this, [this]() {
                m_view->redetectSeabed(m_analysis->currentSeabedAutoParams());
                pushParams();
                flashProgress();
                m_status_left->setText(tr("Params applied to this line"));
                emit paramsApplied();
            });
    connect(m_analysis, &WaterfallAnalysisPanel::applyToAllLinesRequested,
            this, [this]() {
                m_view->redetectSeabed(m_analysis->currentSeabedAutoParams());
                pushParams();
                flashProgress();
                m_status_left->setText(tr("Params applied to all lines"));
                emit applyToAllRequested();
            });
    connect(m_analysis, &WaterfallAnalysisPanel::slantRangeCorrectionChanged,
            this, [this](bool src_on) {
                if (!m_view) return;
                WaterfallParams p = m_view->params();
                p.slant_range_correction = src_on;
                if (src_on)
                    m_view->redetectSeabed(m_analysis->currentSeabedAutoParams());
                // Detection remains available to SRC as processing input. The
                // display hides its line once correction is on; activating a
                // seabed editing tool temporarily reveals it for QC/editing.
                m_view->setParams(p);
                emit paramsApplied();

                // SRC remaps each column against the seabed altitude. Surface what it
                // is actually working with — altitude span vs swath — so its effect is
                // verifiable rather than appearing to "do nothing" (the water-column
                // collapse is small when altitude is a small fraction of the swath).
                if (src_on && m_status_left) {
                    const auto s = m_view->srcStats();
                    if (s.total > 0 && s.with_alt == 0) {
                        m_status_left->setText(tr(
                            "Slant Range Correction has no reference — seabed detection "
                            "returned no bottom for this line."));
                    } else if (s.range_max_m > 0.f) {
                        const float pct = 100.f * s.alt_max_m / s.range_max_m;
                        m_status_left->setText(tr(
                            "SRC applied — seabed altitude %1–%2 m over %3 m swath "
                            "(~%4%% water column collapses), %5/%6 rows")
                            .arg(s.alt_min_m, 0, 'f', 1).arg(s.alt_max_m, 0, 'f', 1)
                            .arg(s.range_max_m, 0, 'f', 0).arg(pct, 0, 'f', 0)
                            .arg(s.with_alt).arg(s.total));
                    } else {
                        m_status_left->setText(tr("Slant Range Correction applied"));
                    }
                }
            });
    connect(m_analysis, &WaterfallAnalysisPanel::seabedChannelChanged,
            this, [this](int ch) { m_view->setSeabedChannel(ch); });
    connect(m_analysis, &WaterfallAnalysisPanel::seabedToolChanged,
            this, [this](int tool) {
                m_view->setSeabedTool(tool);
                if (tool != 0) syncFeatureToolButtons(0);
                static const char* kHints[] = {
                    nullptr,
                    QT_TR_NOOP("Seabed — Pen: drag the seabed line to reshape it"),
                    QT_TR_NOOP("Seabed — Insert: click anywhere to place a seabed pick"),
                    QT_TR_NOOP("Seabed — Eraser: click or drag to remove picks"),
                };
                m_status_left->setText(
                    (tool > 0 && tool < 4) ? tr(kHints[tool]) : QString{});
                if (tool != 0) {
                    m_analysis->setContactPickActive(false);
                    if (m_btn_contact) {
                        QSignalBlocker sb(m_btn_contact);
                        m_btn_contact->setChecked(false);
                    }
                }
            });
    connect(m_analysis, &WaterfallAnalysisPanel::contactToolChanged,
            this, [this](int tool) {
                m_view->setContactTool(tool);
                m_status_left->setText(
                    tool == 1 ? tr("Contact — click on the waterfall to place a point pick")
                              : QString{});
                if (m_btn_contact) {
                    QSignalBlocker sb(m_btn_contact);
                    m_btn_contact->setChecked(tool == 1);
                }
                if (tool != 0) {
                    m_analysis->setSeabedToolActive(0);
                    syncFeatureToolButtons(0);
                }
            });

    connect(m_btn_contact, &QToolButton::toggled,
            this, [this](bool checked) {
                m_view->setContactTool(checked ? 1 : 0);
                if (m_analysis) {
                    m_analysis->setContactPickActive(checked);
                    if (checked)
                        m_analysis->setSeabedToolActive(0);
                }
                if (checked) syncFeatureToolButtons(0);
                m_status_left->setText(
                    checked ? tr("Contact — click on the waterfall to place a point pick")
                            : QString{});
            });

    // Feature drawing toolbar toggles: manual exclusivity among the three;
    // clicking the active tool again turns drawing off. Activating one
    // deactivates contact/seabed (the view setter enforces it; the UI mirrors).
    {
        auto wireFeature = [this](QToolButton* btn, int kind) {
            connect(btn, &QToolButton::toggled, this, [this, btn, kind](bool on) {
                if (on) {
                    syncFeatureToolButtons(kind);          // uncheck the other two
                    m_view->setFeatureTool(kind);
                    if (m_btn_contact) { QSignalBlocker sb(m_btn_contact); m_btn_contact->setChecked(false); }
                    if (m_analysis) {
                        m_analysis->setContactPickActive(false);
                        m_analysis->setSeabedToolActive(0);
                    }
                    m_status_left->setText(
                        kind == 1 ? tr("Polygon: click points, double-click or Enter to close, Esc to cancel.")
                      : kind == 2 ? tr("Line: click points, double-click or Enter to finish, Esc to cancel.")
                                  : tr("Pen: press and drag to draw freehand; release to finish."));
                } else if (!m_btn_feat_poly->isChecked() && !m_btn_feat_line->isChecked()
                           && !m_btn_feat_pen->isChecked()) {
                    m_view->setFeatureTool(0);
                    m_status_left->setText(QString{});
                }
            });
        };
        wireFeature(m_btn_feat_poly, 1);
        wireFeature(m_btn_feat_line, 2);
        wireFeature(m_btn_feat_pen,  3);
    }

    connect(m_analysis, &WaterfallAnalysisPanel::contactClassChanged,
            m_view, &WaterfallView::setContactClass);

    // Marker double-click → the shared "Edit contact details" editor.
    connect(m_view, &WaterfallView::contactEditRequested,
            this, [this](uint64_t id) {
                const QString line = m_layer ? QString::fromStdString(m_layer->id) : QString{};
                emit contactEditRequested(id, line);
            });
    // "Edit Contacts…" in the Contact Picking section → first contact on this line.
    connect(m_analysis, &WaterfallAnalysisPanel::editContactsRequested,
            this, [this]() {
                const QString line = m_layer ? QString::fromStdString(m_layer->id) : QString{};
                emit contactEditRequested(0, line);
            });

    connect(m_analysis, &WaterfallAnalysisPanel::clearContactsRequested,
            this, [this]() {
                // Clear project contacts (confirmed + undoable in MainWindow), same as
                // the SBP viewer — the local overlay refreshes via the project signals.
                emit clearAllContactsRequested();
            });

    connect(m_analysis, &WaterfallAnalysisPanel::automaticContactScanRequested,
            this, [this](int sensitivity) {
                const int count = m_view->detectContactCandidates(sensitivity);
                m_status_left->setText(count > 0
                    ? tr("Automatic contact scan found %n candidate(s)", "", count)
                    : tr("Automatic contact scan found no candidates"));
            });


    connect(m_view, &WaterfallView::featureDrawn,
            this, [this](const std::vector<QPointF>& verts, bool polygon, bool is_projected) {
                const QString line_id = m_layer ? QString::fromStdString(m_layer->id) : QString{};
                m_status_left->setText(
                    tr("Feature drawn — %1 vertices").arg(static_cast<int>(verts.size())));
                emit featureCreated(verts, polygon, is_projected, QString{}, line_id);
            });

    connect(m_view, &WaterfallView::contactPicked,
            this, [this](int row_idx, core::SidescanChannel ch,
                         float range_m, double lat, double lon, bool is_projected,
                         const QPixmap& snapshot, float across_m_per_px,
                         float along_m_per_px, float altitude_m) {
                const QString side = (ch == core::SidescanChannel::Port) ? tr("Port") : tr("Stbd");
                m_status_left->setText(
                    tr("Contact placed — ping %1  ·  %2  %3 m")
                        .arg(row_idx + 1).arg(side).arg(range_m, 0, 'f', 1));

                const QString   cls      = m_analysis
                                         ? m_analysis->currentContactClassText()
                                         : tr("Unknown");
                const QString   line_id  = m_layer
                                         ? QString::fromStdString(m_layer->id)
                                         : QString{};
                const uint64_t  abs_row  = static_cast<uint64_t>(m_window_first_row + row_idx);
                const int       ch_idx   = (ch == core::SidescanChannel::Port) ? 0 : 1;
                emit contactCreated(range_m, lat, lon, is_projected, cls, line_id,
                                    abs_row, ch_idx, snapshot, across_m_per_px,
                                    along_m_per_px, altitude_m);
            });

    connect(m_view, &WaterfallView::cursorMoved,
            this,   &WaterfallWindow::onCursorMoved);

    connect(m_view, &WaterfallView::pingClicked,
            this, [this](int ping_idx, core::SidescanChannel ch, float range_m) {
                const QString side = (ch == core::SidescanChannel::Port) ? tr("Port") : tr("Stbd");
                m_status_left->setText(
                    tr("Ping %1  ·  %2  %3 m")
                        .arg(ping_idx + 1).arg(side).arg(range_m, 0, 'f', 1));
            });

    connect(m_view, &WaterfallView::scrollChanged,
            this,   &WaterfallWindow::onViewScrollChanged);

    connect(m_view, &WaterfallView::scrollBeyondBounds,
            this,   &WaterfallWindow::onScrollBeyondBounds);

    connect(m_view, &WaterfallView::contextMenuRequested,
            this,   &WaterfallWindow::onContextMenu);

    connect(m_vscroll, &QScrollBar::valueChanged,
            this,      &WaterfallWindow::onScrollbarMoved);

    connect(m_vscroll, &QScrollBar::sliderPressed,
            this, [this]() { m_scrollbar_dragging = true; });
    connect(m_vscroll, &QScrollBar::sliderReleased,
            this, [this]() {
                m_scrollbar_dragging = false;
                if (m_pending_abs_row < 0) return;
                m_scroll_debounce->stop();
                const int local_row = m_pending_abs_row - m_window_first_row;
                if (local_row >= 0 && local_row < m_view->rowCount())
                    m_view->scrollToRow(local_row);
                else
                    loadWindow(m_pending_abs_row);
                m_pending_abs_row = -1;
            });

    m_scroll_debounce = new QTimer(this);
    m_scroll_debounce->setSingleShot(true);
    m_scroll_debounce->setInterval(120);
    connect(m_scroll_debounce, &QTimer::timeout,
            this,               &WaterfallWindow::onScrollDebounce);

    m_repipe_debounce = new QTimer(this);
    m_repipe_debounce->setSingleShot(true);
    m_repipe_debounce->setInterval(80);
    connect(m_repipe_debounce, &QTimer::timeout,
            this,               &WaterfallWindow::onRepipeDebounce);

}

void WaterfallWindow::setGpuAccel(bool enabled)
{
    if (m_view) m_view->setGpuAccel(enabled);
}

void WaterfallWindow::setLineNavEnabled(bool has_prev, bool has_next)
{
    m_has_prev_line = has_prev;
    m_has_next_line = has_next;
    if (m_inspector) m_inspector->setNavEnabled(has_prev, has_next);
}

void WaterfallWindow::setProjectLayers(
    const std::vector<std::pair<std::string, std::string>>& layers)
{
    if (m_inspector) m_inspector->setProjectLayers(layers);
}

void WaterfallWindow::setActiveLine(const std::string& id)
{
    if (m_inspector) m_inspector->setActiveLine(id);
}


void WaterfallWindow::syncFeatureToolButtons(int tool)
{
    QToolButton* btns[] = { m_btn_feat_poly, m_btn_feat_line, m_btn_feat_pen };
    for (int i = 0; i < 3; ++i) {
        if (!btns[i]) continue;
        QSignalBlocker sb(btns[i]);
        btns[i]->setChecked(tool == i + 1);
    }
}

} // namespace dolphin::ui
