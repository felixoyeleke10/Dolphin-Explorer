// BottomDockPanel.Layout.cpp — constructor, layout build, header, badge update.
#include "ui/bottom/BottomDockPanel.h"
#include "ui/shell/Theme.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QSize>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kBadgeH = 14;  // panel tab badge pill height

// -- Resize grip ---------------------------------------------------------------

namespace {

class HeightResizeHandle : public QWidget {
    std::function<void(int)> m_on_drag;
    int  m_press_y  = 0;
    bool m_dragging = false;
public:
    explicit HeightResizeHandle(std::function<void(int)> on_drag, QWidget* parent)
        : QWidget(parent), m_on_drag(std::move(on_drag))
    {
        setObjectName("panelResizeHandle");
        setFixedHeight(BottomDockPanel::kHandleH);
        setCursor(Qt::SizeVerCursor);
        setAttribute(Qt::WA_Hover, true);
    }
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            m_press_y  = e->globalPosition().toPoint().y();
            setProperty("dragging", true);
            style()->unpolish(this);
            style()->polish(this);
            e->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!m_dragging) return;
        const int y     = e->globalPosition().toPoint().y();
        const int delta = m_press_y - y;
        m_press_y = y;
        m_on_drag(delta);
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        m_dragging = false;
        setProperty("dragging", false);
        style()->unpolish(this);
        style()->polish(this);
        e->accept();
    }
};

} // namespace

// -- Constructor ---------------------------------------------------------------

BottomDockPanel::BottomDockPanel(DiagnosticsHub* hub, QWidget* parent)
    : QWidget(parent), m_hub(hub)
{
    setObjectName("bottomDockPanel");

    buildLayout();
    restoreState();

    connect(hub, &DiagnosticsHub::problemPosted,
            this, &BottomDockPanel::onProblemPosted);
    connect(hub, &DiagnosticsHub::problemsCleared,
            this, &BottomDockPanel::onProblemsCleared);
    connect(hub, &DiagnosticsHub::outputLogged,
            this, &BottomDockPanel::onOutputLogged);
    connect(hub, &DiagnosticsHub::jobChanged,
            this, &BottomDockPanel::onJobChanged);

    populateFromHub();
}

void BottomDockPanel::buildLayout()
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    m_handle = new HeightResizeHandle([this](int delta) {
        if (m_collapsed) return;
        m_content_h = std::clamp(m_content_h + delta, kMinContentH, 600);
        setFixedHeight(kHandleH + kHeaderH + m_content_h);
        saveState();
    }, this);
    vbox->addWidget(m_handle);

    m_header = new QWidget(this);
    m_header->setObjectName("bottomPanelHeader");
    m_header->setFixedHeight(kHeaderH);
    buildHeader(m_header);
    vbox->addWidget(m_header);

    m_stack = new QStackedWidget(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    buildProblemsTab(m_stack);
    buildOutputTab(m_stack);
    buildJobsTab(m_stack);
    buildTerminalTab(m_stack);
    vbox->addWidget(m_stack, 1);

    switchTab(m_active_tab);
}

void BottomDockPanel::buildHeader(QWidget* parent)
{
    auto* hbox = new QHBoxLayout(parent);
    hbox->setContentsMargins(Theme::kSpacing1, 0, Theme::kSpacing1, 0);
    hbox->setSpacing(0);

    const char* kTabNames[] = { "Problems", "Output", "Jobs", "Terminal" };

    for (int i = 0; i < kTabCount; ++i) {
        auto* btn = new QToolButton(parent);
        btn->setObjectName("panelTabBtn");
        btn->setText(tr(kTabNames[i]));
        btn->setCheckable(true);
        btn->setAutoRaise(true);
        m_tab_btns[i] = btn;

        auto* badge = new QLabel(parent);
        badge->setObjectName("panelBadge");
        badge->setProperty("severity", QStringLiteral("warning"));
        badge->setFixedHeight(kBadgeH);
        badge->hide();
        m_badges[i] = badge;

        hbox->addWidget(btn);
        hbox->addWidget(badge);
        hbox->addSpacing(6);

        const int tab_idx = i;
        connect(btn, &QToolButton::clicked, this, [this, tab_idx]() {
            switchTab(tab_idx);
        });
    }

    hbox->addStretch(1);

    m_collapse_btn = new QToolButton(parent);
    m_collapse_btn->setObjectName("panelCollapseBtn");
    m_collapse_btn->setAutoRaise(true);
    m_collapse_btn->setFixedSize(Theme::kMediumBtnSz, Theme::kMediumBtnSz);
    m_collapse_btn->setIcon(QIcon(":/icons/panel_chevron.svg"));
    m_collapse_btn->setIconSize(QSize(14, 14));
    m_collapse_btn->setToolTip(tr("Minimize panel"));
    hbox->addWidget(m_collapse_btn);

    m_close_btn = new QToolButton(parent);
    m_close_btn->setObjectName("panelCloseBtn");
    m_close_btn->setAutoRaise(true);
    m_close_btn->setFixedSize(Theme::kMediumBtnSz, Theme::kMediumBtnSz);
    m_close_btn->setIcon(QIcon(":/icons/panel_close.svg"));
    m_close_btn->setIconSize(QSize(14, 14));
    m_close_btn->setToolTip(tr("Close panel"));
    hbox->addWidget(m_close_btn);

    connect(m_collapse_btn, &QToolButton::clicked, this, [this]() {
        setCollapsed(!m_collapsed);
    });
    connect(m_close_btn, &QToolButton::clicked, this, [this]() {
        emit closeRequested();
    });
}

void BottomDockPanel::updateBadge(int tab_idx, int count, bool is_error)
{
    auto* badge = m_badges[tab_idx];
    if (!badge) return;
    if (count > 0) {
        badge->setText(count > 99 ? "99+" : QString::number(count));
        badge->setProperty("severity", is_error ? QStringLiteral("error")
                                                : QStringLiteral("warning"));
        badge->style()->unpolish(badge);
        badge->style()->polish(badge);
        badge->show();
    } else {
        badge->hide();
    }
}

} // namespace dolphin::ui
