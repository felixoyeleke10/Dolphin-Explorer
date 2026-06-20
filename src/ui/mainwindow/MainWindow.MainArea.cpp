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
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/mainwindow/rightpanel/PanelChatWidget.h"
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
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {

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

        if (idx.data(kHRoleHint).toBool()) {
            // -- Empty-state message (centred, wrapped, muted) ------------
            QFont f = opt.font;
            f.setPixelSize(11);
            p->setFont(f);
            p->setPen(QColor("#8E8E93"));
            p->drawText(opt.rect.adjusted(16, 0, -16, 0),
                        Qt::AlignCenter | Qt::TextWordWrap,
                        idx.data(Qt::DisplayRole).toString());
        } else if (idx.data(kHRoleSect).toBool()) {
            // -- Section header (Today / Yesterday / …) -------------------
            QFont f = opt.font;
            f.setPixelSize(9);
            f.setWeight(QFont::DemiBold);
            p->setFont(f);
            p->setPen(QColor("#636366"));
            p->drawText(opt.rect.adjusted(12, 0, -8, 0),
                        Qt::AlignVCenter | Qt::AlignLeft,
                        idx.data(Qt::DisplayRole).toString().toUpper());
        } else {
            // -- Activity row ----------------------------------------------
            const bool sel  = opt.state & QStyle::State_Selected;
            const bool hov  = opt.state & QStyle::State_MouseOver;

            if (sel)
                p->fillRect(opt.rect, QColor(10, 132, 255, 28));
            else if (hov)
                p->fillRect(opt.rect, QColor(255, 255, 255, 9));

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
            p->setPen(QColor("#e5e5ea"));
            p->drawText(QRect(r.x(), r.y() + 9, r.width(), 18),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        idx.data(Qt::DisplayRole).toString());

            // Timestamp
            QFont timeF = opt.font;
            timeF.setPixelSize(10);
            p->setFont(timeF);
            p->setPen(QColor("#636366"));
            p->drawText(QRect(r.x(), r.y() + 29, r.width(), 16),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        idx.data(kHRoleTime).toString());

            // Bottom divider
            p->setPen(QPen(QColor("#2d2d2f"), 1));
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

    // Dialog is a top-level window parented to the MainWindow — not added to layout.
    m_import_overlay = new ExecutionProgressDialog(this);

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

    // =====================================================================
    // UPPER SHELL — Properties | Chats | History + generic content
    // =====================================================================
    auto* upper = new SidePanelShell(splitter);

    // -- Tab bar — Properties | Chats | History ---------------------------
    auto* upper_tabs = new PanelTabBar(upper);
    m_props_tab_tools   = upper_tabs->addTab(tr("Properties"), 0);
    m_props_tab_chats   = upper_tabs->addTab(tr("Chats"),      1);
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

    // Page 1 — Chats: embedded AI conversation
    m_chat_widget = new PanelChatWidget(m_props_stack);
    m_props_stack->addWidget(m_chat_widget);    // index 1

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

    // Sensor tab bar — SSS | SBP | Map | MAG
    auto* sensor_tabs = new PanelTabBar(lower);
    m_sensor_bar = sensor_tabs;
    m_tab_sss = sensor_tabs->addTab(tr("SSS"), 0);
    m_tab_sbp = sensor_tabs->addTab(tr("SBP"), 1);
    m_tab_map = sensor_tabs->addTab(tr("Map"), 2);
    m_tab_mag = sensor_tabs->addTab(tr("MAG"), 3);

    m_tab_map->setChecked(true);
    m_tab_sss->setEnabled(false);
    m_tab_sbp->setEnabled(false);
    m_tab_mag->setEnabled(false);

    using M = app::Modality;
    static const M kTabModality[] = { M::Sidescan, M::SubBottom, M::Unknown, M::Magnetometer };
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
    lower->setBody(modal_scroll);
    splitter->addWidget(lower);

    // Initial split: ~55% upper / ~45% lower (sizes are hints, user can drag)
    splitter->setSizes({ 320, 260 });

    content_l->addWidget(splitter, 1);

    // -- Pull panel pointers for signal wiring ----------------------------
    // Navigation / Geometry are now per-modality modal sections (SSS + SBP).
    m_nav_panel         = m_modal_host->navPanel(app::Modality::Sidescan);
    m_heading_panel     = m_modal_host->headingPanel(app::Modality::Sidescan);
    m_sbp_nav_panel     = m_modal_host->navPanel(app::Modality::SubBottom);
    m_sbp_heading_panel = m_modal_host->headingPanel(app::Modality::SubBottom);
    m_gain_panel        = m_modal_host->gainPanel();      // modal (Sidescan)
    m_imaging_panel     = m_modal_host->imagingPanel();   // modal (Sidescan)

    // Wire the Navigation / Geometry Apply buttons at construction — NOT in
    // onWaterfallOpen()/onSubBottomOpen() — so they work from the main/map view
    // even when the viewer window is closed (root cause of "nav buttons do nothing
    // in the map view"). They route to model-owned MainWindow slots that store on
    // DataLayer::nav_state, mark the project dirty, rebuild the map, and refresh
    // the viewer only if it is open.
    if (m_nav_panel) {
        connect(m_nav_panel, &NavInfoPanel::applyToLineRequested,
                this, &MainWindow::onWaterfallNavProcessLine);
        connect(m_nav_panel, &NavInfoPanel::applyToAllRequested,
                this, &MainWindow::onWaterfallNavProcessAllLines);
    }
    if (m_heading_panel) {
        connect(m_heading_panel, &HeadingInfoPanel::applyToLineRequested,
                this, &MainWindow::onWaterfallNavProcessLine);
        connect(m_heading_panel, &HeadingInfoPanel::applyToAllRequested,
                this, &MainWindow::onWaterfallNavProcessAllLines);
    }
    if (m_sbp_nav_panel) {
        connect(m_sbp_nav_panel, &NavInfoPanel::applyToLineRequested,
                this, &MainWindow::applySbpNavToLine);
        connect(m_sbp_nav_panel, &NavInfoPanel::applyToAllRequested,
                this, &MainWindow::applySbpNavToAll);
    }
    if (m_sbp_heading_panel) {
        connect(m_sbp_heading_panel, &HeadingInfoPanel::applyToLineRequested,
                this, &MainWindow::applySbpNavToLine);
        connect(m_sbp_heading_panel, &HeadingInfoPanel::applyToAllRequested,
                this, &MainWindow::applySbpNavToAll);
    }

    outer->addWidget(content, 1);
}

} // namespace dolphin::ui
