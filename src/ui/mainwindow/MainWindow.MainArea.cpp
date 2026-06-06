// MainWindow.MainArea.cpp — buildMainArea and buildPropertiesPanel.
//
// buildPropertiesPanel builds the right-side panel as a scroll area hosting
// InspectorPanel, which internally manages all right-panel modules via RightPanelHost.
#include "ui/mainwindow/MainWindow.h"
#include "ui/shell/Theme.h"
#include "ui/bottom/BottomDockPanel.h"
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/shared/widgets/PanelResizeHandle.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/mainwindow/panels/InspectorPanel.h"
#include "ui/mainwindow/rightpanel/RightPanelHost.h"
#include "ui/mainwindow/rightpanel/PanelChatWidget.h"
#include "ui/mainwindow/panels/NavInfoPanel.h"
#include "ui/mainwindow/panels/HeadingInfoPanel.h"
#include "ui/mainwindow/panels/GainControlPanel.h"
#include "ui/mainwindow/panels/ImagingControlPanel.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QScrollArea>
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
        return idx.data(kHRoleSect).toBool() ? QSize(0, 26) : QSize(0, 52);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        if (idx.data(kHRoleSect).toBool()) {
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
    auto* layout = new QVBoxLayout(area);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

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

    auto* outer = new QHBoxLayout(m_props_panel);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Left drag-to-resize handle.
    auto* resize_handle = new PanelResizeHandle(
        &m_props_width,
        [this]() { m_props_panel->setFixedWidth(m_props_width); },
        m_props_panel);
    resize_handle->setObjectName("propsResizeHandle");
    outer->addWidget(resize_handle);

    auto* content = new QWidget(m_props_panel);
    content->setObjectName("propsContent");
    auto* content_l = new QVBoxLayout(content);
    content_l->setContentsMargins(0, 0, 0, 0);
    content_l->setSpacing(0);

    // -- Tab bar — Tools | Chats | History --------------------------------
    auto* hdr = new QFrame(content);
    hdr->setObjectName("propsTabs");
    hdr->setFixedHeight(Theme::kPanelHdrH);
    auto* hdr_l = new QHBoxLayout(hdr);
    hdr_l->setContentsMargins(0, 0, 0, 0);
    hdr_l->setSpacing(0);

    auto* tab_group = new QButtonGroup(hdr);
    tab_group->setExclusive(true);

    auto makeTab = [&](const QString& label) {
        auto* btn = new QToolButton(hdr);
        btn->setText(label);
        btn->setCheckable(true);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        btn->setObjectName("propsPanelTab");
        return btn;
    };

    m_props_tab_tools   = makeTab(tr("Properties"));
    m_props_tab_chats   = makeTab(tr("Chats"));
    m_props_tab_history = makeTab(tr("History"));

    m_props_tab_tools->setChecked(true);

    tab_group->addButton(m_props_tab_tools,   0);
    tab_group->addButton(m_props_tab_chats,   1);
    tab_group->addButton(m_props_tab_history, 2);

    auto* sep1 = new QFrame(hdr);
    sep1->setObjectName("propsTabSep");
    auto* sep2 = new QFrame(hdr);
    sep2->setObjectName("propsTabSep");

    hdr_l->addWidget(m_props_tab_tools);
    hdr_l->addWidget(sep1);
    hdr_l->addWidget(m_props_tab_chats);
    hdr_l->addWidget(sep2);
    hdr_l->addWidget(m_props_tab_history);
    content_l->addWidget(hdr);

    // -- Stacked body — one page per tab ----------------------------------
    m_props_stack = new QStackedWidget(content);
    m_props_stack->setObjectName("propsStack");

    // Page 0 — Tools: scrollable inspector modules
    m_props_scroll = new QScrollArea(m_props_stack);
    m_props_scroll->setObjectName("propsScroll");
    m_props_scroll->setWidgetResizable(true);
    m_props_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_props_scroll->setFrameShape(QFrame::NoFrame);

    auto* scroll_body = new QWidget();
    scroll_body->setObjectName("propsScrollBody");
    auto* scroll_l = new QVBoxLayout(scroll_body);
    scroll_l->setContentsMargins(0, 0, 0, 0);
    scroll_l->setSpacing(0);

    m_inspector = new InspectorPanel(scroll_body);
    m_inspector->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    scroll_l->addWidget(m_inspector);
    scroll_l->addStretch(1);

    m_props_scroll->setWidget(scroll_body);
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

    content_l->addWidget(m_props_stack, 1);

    // Pull panel pointers from the host for waterfall signal wiring.
    auto* host  = m_inspector->rightPanelHost();
    m_nav_panel     = host->navPanel();
    m_heading_panel = host->headingPanel();
    m_gain_panel    = host->gainPanel();
    m_imaging_panel = host->imagingPanel();

    outer->addWidget(content, 1);
}

} // namespace dolphin::ui
