// MainWindow.Chrome.cpp — title/menu bar and status bar chrome.
//   setupStatusBar()  — MainStatusBar construction
//   setupTitleBar()   — MenuBarFilter, CommandBar (centred search), window buttons
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/mainwindow/MenuBarFilter.h"
#include "ui/mainwindow/ChatPanel.h"   // ConversationPanel — file kept as ChatPanel.h
#include "ui/bottom/BottomDockPanel.h"
#include "ui/shared/widgets/CommandBar.h"
#include "ui/shell/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>

namespace dolphin::ui {

static constexpr int kInnerSepH  = 14;  // height of VLine separators inside the unified bar
static constexpr int kCornerSepH = 16;  // height of VLine separator before window controls

void MainWindow::setupStatusBar()
{
    m_status_bar = new MainStatusBar(this);
    setStatusBar(m_status_bar);
}

void MainWindow::setupTitleBar()
{
    auto* mb_filter = new MenuBarFilter(this);
    menuBar()->installEventFilter(mb_filter);

    // ── Left flank: back / forward navigation arrows ──────────────────────────
    {
        auto* nav = new QWidget(menuBar());
        auto* l   = new QHBoxLayout(nav);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(1);

        auto mkNav = [&](const QString& glyph, const QString& tip) {
            auto* b = new QPushButton(glyph, nav);
            b->setObjectName("navBtn");
            b->setFixedSize(Theme::kNavBtnW, Theme::kNavBtnH);
            b->setFlat(true);
            b->setToolTip(tip);
            b->setEnabled(false);
            return b;
        };
        m_btn_nav_back = mkNav(QStringLiteral("←"), tr("Go Back"));
        m_btn_nav_forward = mkNav(QStringLiteral("→"), tr("Go Forward"));
        connect(m_btn_nav_back, &QPushButton::clicked,
                this, &MainWindow::onNavigateBack);
        connect(m_btn_nav_forward, &QPushButton::clicked,
                this, &MainWindow::onNavigateForward);
        l->addWidget(m_btn_nav_back);
        l->addWidget(m_btn_nav_forward);
        nav->adjustSize();
        mb_filter->setLeftFlank(nav);
    }

    // ── Centre: unified command / Conversation / Assistant pill ─────────────────
    {
        auto* uni = new QFrame(menuBar());
        uni->setObjectName("uniBar");
        auto* ul = new QHBoxLayout(uni);
        ul->setContentsMargins(Theme::kSpacing1, 0, Theme::kSpacing1, 0);
        ul->setSpacing(0);

        m_cmd_bar = new CommandBar(uni);
        m_cmd_bar->setObjectName("titleSearch");
        m_cmd_bar->setFixedHeight(Theme::kCmdBarH);
        m_cmd_bar->setProvider([this] { return buildCommandItems(); });
        m_cmd_bar->setAnchorWidget(uni);  // palette spans the full pill, not just the search field

        auto mkInnerSep = [&]() -> QFrame* {
            auto* s = new QFrame(uni);
            s->setObjectName("uniBarSep");
            s->setFixedSize(Theme::kSepSz, kInnerSepH);
            return s;
        };

        auto* btn_chat = new QPushButton(uni);
        btn_chat->setObjectName("uniBarBtn");
        btn_chat->setIcon(QIcon(QStringLiteral(":/icons/chat.svg")));
        btn_chat->setIconSize(QSize(15, 15));
        btn_chat->setFixedSize(Theme::kChromeConvBtnW, Theme::kCmdBarH);
        btn_chat->setFlat(true);
        btn_chat->setToolTip(tr("Conversation"));
        QObject::connect(btn_chat, &QPushButton::clicked, btn_chat, [this, btn_chat]() {
            if (!m_conv_panel)
                m_conv_panel = new ConversationPanel(this);
            if (m_conv_panel->isVisible())
                m_conv_panel->hide();
            else
                m_conv_panel->popupBelow(btn_chat);
        });

        auto* btn_agent = new QPushButton(QStringLiteral("✦  ") + tr("Assistant") + QStringLiteral("  ▾"), uni);
        btn_agent->setObjectName("uniBarBtnAccent");
        btn_agent->setFlat(true);
        btn_agent->setToolTip(tr("Open Assistant"));
        QObject::connect(btn_agent, &QPushButton::clicked, btn_agent, [btn_agent]() {
            QMenu menu(btn_agent);
            menu.addAction(QStringLiteral("New Conversation"));
            menu.addAction(QStringLiteral("Configure..."));
            menu.addSeparator();
            menu.addAction(QStringLiteral("Manage..."));
            menu.exec(btn_agent->mapToGlobal(QPoint(0, btn_agent->height())));
        });

        ul->addWidget(m_cmd_bar, 1);
        ul->addSpacing(4);
        ul->addWidget(mkInnerSep());
        ul->addSpacing(4);
        ul->addWidget(btn_chat);
        ul->addSpacing(2);
        ul->addWidget(mkInnerSep());
        ul->addSpacing(4);
        ul->addWidget(btn_agent);
        ul->addSpacing(2);

        mb_filter->setSearchWidget(uni);
    }

    auto* corner = new QWidget(this);
    corner->setObjectName("titleBarCorner");
    auto* cl     = new QHBoxLayout(corner);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);

    // ── Layout toggle buttons ─────────────────────────────────────────────────
    auto makeLayoutBtn = [&](const char* icon_path, const QString& tip,
                              bool checkable) -> QPushButton* {
        auto* btn = new QPushButton(corner);
        btn->setObjectName("layoutToggleBtn");
        btn->setIcon(QIcon(QString::fromLatin1(icon_path)));
        btn->setIconSize(QSize(18, 18));
        btn->setFixedSize(Theme::kLayoutBtnW, Theme::kWinBtnH);
        btn->setFlat(true);
        btn->setCheckable(checkable);
        btn->setToolTip(tip);
        return btn;
    };

    m_btn_primary_sidebar   = makeLayoutBtn(":/icons/sidebar_left.svg",
                                            tr("Toggle Primary Side Bar"), true);
    m_btn_bottom_panel      = makeLayoutBtn(":/icons/panel_bottom.svg",
                                            tr("Toggle Panel"), true);
    m_btn_secondary_sidebar = makeLayoutBtn(":/icons/sidebar_right.svg",
                                            tr("Toggle Secondary Side Bar"), true);
    auto* btn_customize     = makeLayoutBtn(":/icons/layout_customize.svg",
                                            tr("Customize Layout"), false);

    // Initial checked states reflect current panel visibility
    m_btn_primary_sidebar->setChecked(!m_context_collapsed);
    m_btn_secondary_sidebar->setChecked(!m_props_collapsed);
    if (m_bottom_panel)
        m_btn_bottom_panel->setChecked(!m_bottom_panel->isCollapsed());

    connect(m_btn_primary_sidebar,   &QPushButton::clicked,
            this, &MainWindow::onToggleContextPanel);
    connect(m_btn_secondary_sidebar, &QPushButton::clicked,
            this, &MainWindow::onTogglePropertiesPanel);
    connect(m_btn_bottom_panel, &QPushButton::clicked, this, &MainWindow::toggleBottomPanel);
    if (m_bottom_panel) {
        connect(m_bottom_panel, &BottomDockPanel::collapsedChanged, this,
                [this](bool collapsed) {
                    if (m_btn_bottom_panel) m_btn_bottom_panel->setChecked(!collapsed);
                });
        connect(m_bottom_panel, &BottomDockPanel::closeRequested, this, [this]() {
            m_bottom_panel->hide();
            if (m_btn_bottom_panel) m_btn_bottom_panel->setChecked(false);
        });
    }

    connect(btn_customize, &QPushButton::clicked, this, [this, btn_customize]() {
        QMenu menu(btn_customize);
        auto* a_primary = menu.addAction(tr("Primary Side Bar"));
        a_primary->setCheckable(true);
        a_primary->setChecked(!m_context_collapsed);
        connect(a_primary, &QAction::triggered, this, &MainWindow::onToggleContextPanel);

        auto* a_panel = menu.addAction(tr("Panel"));
        a_panel->setCheckable(true);
        a_panel->setChecked(m_bottom_panel && m_bottom_panel->isVisible() && !m_bottom_panel->isCollapsed());
        connect(a_panel, &QAction::triggered, this, &MainWindow::toggleBottomPanel);

        auto* a_secondary = menu.addAction(tr("Secondary Side Bar"));
        a_secondary->setCheckable(true);
        a_secondary->setChecked(!m_props_collapsed);
        connect(a_secondary, &QAction::triggered, this, &MainWindow::onTogglePropertiesPanel);

        menu.exec(btn_customize->mapToGlobal(btn_customize->rect().bottomLeft()));
    });

    cl->addWidget(m_btn_primary_sidebar);
    cl->addWidget(m_btn_bottom_panel);
    cl->addWidget(m_btn_secondary_sidebar);
    cl->addWidget(btn_customize);

    // Visual separator before window controls
    auto* sep = new QFrame(corner);
    sep->setObjectName("cornerSep");
    sep->setFixedSize(Theme::kSepSz, kCornerSepH);
    cl->addWidget(sep);

    // ── Window control buttons ────────────────────────────────────────────────
    auto makeWinBtn = [&](const QString& glyph, const QString& tip,
                           bool is_close = false) -> QPushButton* {
        auto* btn = new QPushButton(glyph, corner);
        btn->setToolTip(tip);
        btn->setFixedSize(Theme::kWinBtnW, Theme::kWinBtnH);
        btn->setFlat(true);
        btn->setObjectName(is_close ? "winBtnClose" : "winBtnStd");
        return btn;
    };

    auto* btn_min   = makeWinBtn(QStringLiteral("−"), tr("Minimise"));
    auto* btn_max   = makeWinBtn(QStringLiteral("❒"),  tr("Maximise"));
    auto* btn_close = makeWinBtn(QStringLiteral("✕"),  tr("Close"), true);

    connect(btn_min,   &QPushButton::clicked, this, &QMainWindow::showMinimized);
    connect(btn_close, &QPushButton::clicked, this, &QMainWindow::close);
    connect(btn_max, &QPushButton::clicked, this, [this, btn_max]() {
        if (isMaximized()) {
            showNormal();
            btn_max->setText(QStringLiteral("❒"));
            btn_max->setToolTip(tr("Maximise"));
        } else {
            showMaximized();
            btn_max->setText(QStringLiteral("□"));
            btn_max->setToolTip(tr("Restore"));
        }
    });

    cl->addWidget(btn_min);
    cl->addWidget(btn_max);
    cl->addWidget(btn_close);
    menuBar()->setCornerWidget(corner, Qt::TopRightCorner);
}

void MainWindow::toggleBottomPanel()
{
    if (!m_bottom_panel) return;
    if (!m_bottom_panel->isVisible()) {
        m_bottom_panel->show();
        m_bottom_panel->setCollapsed(false);
        if (m_btn_bottom_panel) m_btn_bottom_panel->setChecked(true);
    } else {
        m_bottom_panel->setCollapsed(!m_bottom_panel->isCollapsed());
    }
}

} // namespace dolphin::ui
