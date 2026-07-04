// MainWindow.MainArea.cpp — buildMainArea and buildPropertiesPanel.
//
// buildPropertiesPanel builds the right-side panel as a scroll area hosting
// InspectorPanel, which internally manages all right-panel modules via RightPanelHost.
#include "ui/mainwindow/MainWindow.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"
#include "ui/bottom/BottomDockPanel.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/shared/widgets/PanelResizeHandle.h"
#include "ui/shared/widgets/PanelTabBar.h"
#include "ui/shared/widgets/SidePanelShell.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include <algorithm>
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/mainwindow/rightpanel/PanelChatWidget.h"
#include "ui/mainwindow/panels/MapDisplayPanel.h"
#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "ui/mainwindow/panels/GainControlPanel.h"
#include "ui/mainwindow/panels/ImagingControlPanel.h"

#include "app/layers/LayerUtils.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {

namespace Theme = dolphin::ui::Theme;   // delegate colours follow the theme mode

// UserRole map for history list items:
//   Qt::UserRole     — unused (kept for compat)
//   Qt::UserRole + 1 — formatted timestamp string
//   Qt::UserRole + 2 — true = section header (non-selectable), false = entry row
//   Qt::UserRole + 3 — ActivityKind int value (for colored dot)
static constexpr int kHRoleTime = Qt::UserRole + 1;
static constexpr int kHRoleSect = Qt::UserRole + 2;
static constexpr int kHRoleKind = Qt::UserRole + 3;
static constexpr int kHRoleHint = Qt::UserRole + 4;   // empty-state message row

// Color per ActivityKind int value (Import=0 … GroupChange=8)
static QColor activityKindColor(int kind)
{
    switch (kind) {
        case 0: return QColor("#FF9500"); // Import — orange
        case 1: return QColor("#30D158"); // Processing — green
        case 2: return QColor("#0A84FF"); // Palette — blue
        case 3: return QColor("#5AC8FA"); // DisplayParams — teal
        case 4: return QColor("#BF5AF2"); // NavCorrection — purple
        case 5: return QColor("#8E8E93"); // Visibility — gray
        case 6: return QColor("#64D2FF"); // CrsChange — light blue
        case 7: return QColor("#FF6B6B"); // TagChange — coral
        case 8: return QColor("#A8E6CF"); // GroupChange — mint
        case 9: return QColor("#FFD60A"); // Export — yellow
        case 10: return QColor("#FF9F0A"); // ContactPick — amber
        default: return QColor("#8E8E93");
    }
}

class HistoryItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&,
                   const QModelIndex& idx) const override
    {
        if (idx.data(kHRoleHint).toBool()) return QSize(0, 80);
        return idx.data(kHRoleSect).toBool() ? QSize(0, 26) : QSize(0, 52);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        // Colours resolve through the Theme runtime getters so the delegate
        // follows Light/Dark (dark values are identical to the old literals).
        if (idx.data(kHRoleHint).toBool()) {
            // -- Empty-state message (centred, wrapped, muted) ------------
            QFont f = opt.font;
            f.setPixelSize(11);
            p->setFont(f);
            p->setPen(Theme::textSubtleColor());
            p->drawText(opt.rect.adjusted(16, 0, -16, 0),
                        Qt::AlignCenter | Qt::TextWordWrap,
                        idx.data(Qt::DisplayRole).toString());
        } else if (idx.data(kHRoleSect).toBool()) {
            // -- Section header (Today / Yesterday / …) -------------------
            QFont f = opt.font;
            f.setPixelSize(9);
            f.setWeight(QFont::DemiBold);
            p->setFont(f);
            p->setPen(Theme::textMutedColor());
            p->drawText(opt.rect.adjusted(12, 0, -8, 0),
                        Qt::AlignVCenter | Qt::AlignLeft,
                        idx.data(Qt::DisplayRole).toString().toUpper());
        } else {
            // -- Activity row ----------------------------------------------
            const bool sel  = opt.state & QStyle::State_Selected;
            const bool hov  = opt.state & QStyle::State_MouseOver;
            const bool light = Theme::mode() == Theme::Mode::Light;

            if (sel)
                p->fillRect(opt.rect, QColor(10, 132, 255, 28));
            else if (hov)
                p->fillRect(opt.rect, light ? QColor(0, 0, 0, 12)
                                            : QColor(255, 255, 255, 9));

            // Kind dot — 4px radius circle on left margin
            const QColor dotClr = activityKindColor(idx.data(kHRoleKind).toInt());
            p->setBrush(dotClr);
            p->setPen(Qt::NoPen);
            p->drawEllipse(QPoint(15, opt.rect.y() + opt.rect.height() / 2), 4, 4);

            const QRect r = opt.rect.adjusted(28, 0, -10, 0);

            // Description text
            QFont nameF = opt.font;
            nameF.setPixelSize(12);
            p->setFont(nameF);
            p->setPen(Theme::textSecondColor());
            p->drawText(QRect(r.x(), r.y() + 9, r.width(), 18),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        idx.data(Qt::DisplayRole).toString());

            // Timestamp
            QFont timeF = opt.font;
            timeF.setPixelSize(10);
            p->setFont(timeF);
            p->setPen(Theme::textMutedColor());
            p->drawText(QRect(r.x(), r.y() + 29, r.width(), 16),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        idx.data(kHRoleTime).toString());

            // Bottom divider
            p->setPen(QPen(Theme::borderColor(), 1));
            p->drawLine(12, opt.rect.bottom(), opt.rect.right(), opt.rect.bottom());
        }

        p->restore();
    }
};

} // anonymous namespace

namespace dolphin::ui {

QWidget* MainWindow::buildMainArea(QWidget* parent)
{
    auto* area = new QWidget(parent);
    area->setObjectName("mainArea");
    auto* layout = makeCompactLayout<QVBoxLayout>(area);

    m_viewport_host = new MapViewportHost(area);
    m_map_view = m_viewport_host->view2D();
    layout->addWidget(m_viewport_host, 1);

    m_bottom_panel = new BottomDockPanel(m_diag_hub, area);
    layout->addWidget(m_bottom_panel, 0);  // no stretch — panel controls its own height

    // AI chat lives in the bottom dock (Problems / Output / Jobs / Terminal / Chat).
    // Injected here because the dock layer must not link the mainwindow lib.
    m_chat_widget = new PanelChatWidget(m_bottom_panel);
    m_bottom_panel->setChatWidget(m_chat_widget);

    // Background Tasks panel: embedded as a child overlay of the viewport (anchored
    // bottom-centre), NOT a top-level window. A popup over the frameless main window
    // blinks it when shown during project open; an in-window overlay cannot.
    m_import_overlay = new ExecutionProgressDialog(m_viewport_host);
    m_import_overlay->embedIn(m_viewport_host);

    return area;
}

void MainWindow::buildPropertiesPanel(QWidget* parent)
{
    m_props_panel = new QFrame(parent);
    m_props_panel->setObjectName("propertiesPanel");
    m_props_panel->setFixedWidth(m_props_width);

    auto* outer = makeCompactLayout<QHBoxLayout>(m_props_panel);

    // Left drag-to-resize handle.
    auto* resize_handle = new PanelResizeHandle(
        &m_props_width,
        [this]() { m_props_panel->setFixedWidth(m_props_width); },
        m_props_panel);
    resize_handle->setObjectName("propsResizeHandle");
    outer->addWidget(resize_handle);

    auto* content = new QWidget(m_props_panel);
    content->setObjectName("propsContent");
    auto* content_l = makeCompactLayout<QVBoxLayout>(content);

    // -- Vertical splitter: upper (generic) / lower (sensor-adaptive) ------
    auto* splitter = new QSplitter(Qt::Vertical, content);
    splitter->setHandleWidth(4);
    splitter->setChildrenCollapsible(false);
    splitter->setObjectName("propsSplitter");
    m_props_splitter = splitter;

    // =====================================================================
    // UPPER SHELL — Properties | Map | History + generic content
    // =====================================================================
    auto* upper = new SidePanelShell(splitter);

    // -- Tab bar — Properties | Map | History ------------------------------
    auto* upper_tabs = new PanelTabBar(upper);
    m_props_tab_tools   = upper_tabs->addTab(tr("Properties"), 0);
    m_props_tab_map     = upper_tabs->addTab(tr("Map"),        1);
    m_props_tab_history = upper_tabs->addTab(tr("History"),    2);
    m_props_tab_tools->setChecked(true);
    connect(upper_tabs, &PanelTabBar::tabChanged,
            this, &MainWindow::onPropsTabChanged);
    upper->setHeader(upper_tabs);

    // -- Stacked body — one page per tab ----------------------------------
    m_props_stack = new QStackedWidget(upper);
    m_props_stack->setObjectName("propsStack");

    // Page 0 — Properties: scrollable universal inspector modules
    m_props_scroll = new QScrollArea(m_props_stack);
    m_props_scroll->setObjectName("propsScroll");
    m_props_scroll->setWidgetResizable(true);
    m_props_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_props_scroll->setFrameShape(QFrame::NoFrame);
    {
        auto* scroll_body = new QWidget();
        scroll_body->setObjectName("propsScrollBody");
        auto* scroll_l = makeCompactLayout<QVBoxLayout>(scroll_body);
        m_inspector = new InspectorPanel(scroll_body);
        m_inspector->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        scroll_l->addWidget(m_inspector);
        scroll_l->addStretch(1);
        m_props_scroll->setWidget(scroll_body);
    }
    m_props_stack->addWidget(m_props_scroll);   // index 0

    // Page 1 — Map: view working options (tooltips, hover highlight, camera
    // azimuth/height, mosaic spotlight). buildMainArea ran first, so the
    // viewport host exists for wiring.
    m_map_display_panel = new MapDisplayPanel(m_props_stack);
    m_props_stack->addWidget(m_map_display_panel);   // index 1
    {
        auto* mp = m_map_display_panel;
        connect(mp, &MapDisplayPanel::tooltipsToggled, this, [this](bool on) {
            if (m_map_view) m_map_view->setHoverTooltipsEnabled(on);
        });
        connect(mp, &MapDisplayPanel::hoverHighlightToggled, this, [this](bool on) {
            if (m_map_view) m_map_view->setHoverHighlightEnabled(on);
        });
        connect(mp, &MapDisplayPanel::azimuthEdited, this, [this](double deg) {
            if (m_viewport_host) m_viewport_host->setRotationDeg(deg);
        });
        // Height (m) ↔ scale uses the host's own 3D approximation
        // (distance ≈ mpp · viewport_h / 2), so spin and camera stay consistent.
        connect(mp, &MapDisplayPanel::heightEdited, this, [this](double h) {
            if (!m_viewport_host) return;
            const int vh = std::max(1, m_viewport_host->height());
            m_viewport_host->setViewportScale(2.0 * h / vh);
        });
        connect(m_viewport_host, &MapViewportHost::viewportChanged, mp,
                [this, mp](double mpp, double rot) {
                    const int vh = m_viewport_host ? std::max(1, m_viewport_host->height()) : 1;
                    mp->setCameraReadout(rot, mpp * vh / 2.0);
                });
        mp->broadcastState();   // push persisted view options into the map view
    }

    // Page 2 — History: recently inspected layers
    m_props_history_list = new QListWidget(m_props_stack);
    m_props_history_list->setObjectName("propsHistoryList");
    m_props_history_list->setFrameShape(QFrame::NoFrame);
    m_props_history_list->setMouseTracking(true);
    m_props_history_list->setItemDelegate(new HistoryItemDelegate(m_props_history_list));
    m_props_stack->addWidget(m_props_history_list); // index 2

    upper->setBody(m_props_stack);
    splitter->addWidget(upper);

    // =====================================================================
    // LOWER SHELL — Sensor/modality tab bar + adaptive modal inspector
    // =====================================================================
    auto* lower = new SidePanelShell(splitter);

    // Sensor tab bar — SSS | SBP | MAG. No Map tab: the universal sections
    // (Contact Picking) show under every tab and with no tab checked, and map
    // settings live in the upper Properties | Map | History strip. No tab is
    // checked until a sensor layer exists.
    auto* sensor_tabs = new PanelTabBar(lower);
    m_sensor_bar = sensor_tabs;
    m_tab_sss = sensor_tabs->addTab(tr("SSS"), 0);
    m_tab_sbp = sensor_tabs->addTab(tr("SBP"), 1);
    m_tab_mag = sensor_tabs->addTab(tr("MAG"), 2);

    m_tab_sss->setEnabled(false);
    m_tab_sbp->setEnabled(false);
    m_tab_mag->setEnabled(false);

    using M = app::Modality;
    static const M kTabModality[] = { M::Sidescan, M::SubBottom, M::Magnetometer };
    connect(sensor_tabs, &PanelTabBar::tabChanged, this, [this](int id) {
        refreshSensorTab(kTabModality[id]);
    });
    lower->setHeader(sensor_tabs);

    // Modal scroll area — holds modality-specific modules
    auto* modal_scroll = new QScrollArea(lower);
    modal_scroll->setObjectName("modalScroll");
    modal_scroll->setWidgetResizable(true);
    modal_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    modal_scroll->setFrameShape(QFrame::NoFrame);
    {
        auto* modal_body = new QWidget();
        modal_body->setObjectName("modalScrollBody");
        auto* modal_l = makeCompactLayout<QVBoxLayout>(modal_body);
        m_modal_host = new RightPanelHost(RightPanelHost::ShowMode::ModalOnly, modal_body);
        m_modal_host->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        modal_l->addWidget(m_modal_host);
        modal_l->addStretch(1);
        modal_scroll->setWidget(modal_body);
    }

    // -- Shared Apply bar (one for the whole tools panel, not per-section) ------
    // Gathers every visible tool section's settings and applies them in a single
    // rebuild. Shown only when a sensor (SSS/SBP) layer is active.
    auto* lower_body = new QWidget(lower);
    auto* lower_l = makeCompactLayout<QVBoxLayout>(lower_body);
    lower_l->addWidget(modal_scroll, 1);

    m_tools_apply_bar = new QWidget(lower_body);
    m_tools_apply_bar->setObjectName("toolsApplyBar");
    auto* bar_l = new QHBoxLayout(m_tools_apply_bar);
    bar_l->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2,
                              Theme::kSpacing3, Theme::kSpacing3);
    bar_l->setSpacing(Theme::kSpacing1);
    m_tools_apply_line = new QPushButton(tr("Apply to Line"), m_tools_apply_bar);
    m_tools_apply_line->setObjectName("ctrlApplyBtn");
    m_tools_apply_all  = new QPushButton(tr("Apply to All"), m_tools_apply_bar);
    m_tools_apply_all->setObjectName("ctrlApplyBtnSecondary");
    // Expanding so the buttons fill and split the bar; the layout re-flows when the
    // primary label grows ("Apply to Line" → "Apply to Selected (N)"), and the
    // Expanding minimum (content) means the text never clips.
    m_tools_apply_line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_tools_apply_all->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bar_l->addWidget(m_tools_apply_line, 1);
    bar_l->addWidget(m_tools_apply_all, 1);
    lower_l->addWidget(m_tools_apply_bar);
    connect(m_tools_apply_line, &QPushButton::clicked,
            this, &MainWindow::onApplyToolsToLine);
    connect(m_tools_apply_all, &QPushButton::clicked,
            this, &MainWindow::onApplyToolsToAll);
    m_tools_apply_bar->setVisible(false);   // shown when a sensor layer is active

    lower->setBody(lower_body);
    splitter->addWidget(lower);

    // The upper (Properties) pane hugs its content; the lower (sensor) pane
    // absorbs all slack so there's no dead gap under short property lists.
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 320, 260 });  // provisional; adjustPropsSplit() refines once laid out

    content_l->addWidget(splitter, 1);

    // Size the upper pane to its real content after the first layout pass.
    QTimer::singleShot(0, this, [this]() { adjustPropsSplit(); });

    // -- Pull panel pointers for signal wiring ----------------------------
    // Navigation / Geometry are now per-modality modal sections (SSS + SBP).
    m_nav_panel         = m_modal_host->navPanel(app::Modality::Sidescan);
    m_heading_panel     = m_modal_host->headingPanel(app::Modality::Sidescan);
    m_sbp_nav_panel     = m_modal_host->navPanel(app::Modality::SubBottom);
    m_sbp_heading_panel = m_modal_host->headingPanel(app::Modality::SubBottom);
    m_gain_panel        = m_modal_host->gainPanel();      // modal (Sidescan)
    m_imaging_panel     = m_modal_host->imagingPanel();   // modal (Sidescan)

    // (The right panel hosts no annotation sections any more — contact picking
    // lives on the top toolbar (C) and in the viewer toolbars, feature drawing
    // on the top toolbar. Both were removed on user direction.)

    // The tool sections (Gain/Imaging/Nav/Geometry, SSS + SBP) have no per-section
    // Apply buttons: they only edit values. The single shared Apply bar at the bottom
    // of the panel gathers every section's settings and applies them in one rebuild
    // (MainWindow::applyActiveTools). The panel pointers above are still used to read
    // the current control values (writeInto / currentParams) from that bar.

    outer->addWidget(content, 1);
}

} // namespace dolphin::ui
