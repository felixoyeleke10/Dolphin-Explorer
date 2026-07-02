#include "ui/shared/widgets/ViewerToolbar.h"
#include "ui/shared/widgets/CommandBar.h"
#include "ui/shell/Theme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QSize>
#include <QStyle>
#include <QToolButton>

namespace dolphin::ui {

using namespace Theme;

ViewerToolbar::ViewerToolbar(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("av_toolbar");
    setFixedHeight(kAvToolBarH);

    // Three columns: [left buttons] [centred command pill] [right tools].
    // Columns 0 and 2 share equal stretch, so the pill in column 1 is centred on
    // the toolbar regardless of how many right-section tools a viewer adds — this
    // is why the SSS and SBP search bars line up identically, and match the main
    // window's centred command pill.
    //
    // The pill width is LAYOUT-DRIVEN (column-stretch ratio + a minimum width), not
    // set after show() via a resize hook — so it is correct on the very first paint
    // and never visibly jumps into place when the viewer opens.
    // Column ratio 36:28:36 ⇒ the pill targets ~28% of the toolbar (= kCmdBarPct).
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(10, 0, 10, 0);
    m_grid->setHorizontalSpacing(0);
    m_grid->setColumnStretch(0, 36);
    m_grid->setColumnStretch(1, kCmdBarPct);   // 28
    m_grid->setColumnStretch(2, 36);

    // -- Standard left buttons -------------------------------------------------
    auto* left = new QHBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(2);

    auto* btn_new      = makeBtn(":/icons/new.svg",
        tr("Create a new project or workspace. Shortcut: Ctrl+N."));
    auto* btn_open     = makeBtn(":/icons/open.svg",
        tr("Open an existing project or import source. Shortcut: Ctrl+O."));
    auto* btn_save     = makeBtn(":/icons/save.svg",
        tr("Save the current project. Shortcut: Ctrl+S."));
    m_btn_meta         = makeBtn(":/icons/properties.svg",
        tr("Open metadata and navigation information for this line."));
    auto* btn_settings = makeBtn(":/icons/settings2.svg",
        tr("Open application and processing settings."));

    left->addWidget(btn_new);
    left->addWidget(btn_open);
    left->addWidget(btn_save);
    left->addWidget(m_btn_meta);
    left->addWidget(btn_settings);
    m_grid->addLayout(left, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    connect(btn_new,      &QToolButton::clicked, this, &ViewerToolbar::newRequested);
    connect(btn_open,     &QToolButton::clicked, this, &ViewerToolbar::openRequested);
    connect(btn_save,     &QToolButton::clicked, this, &ViewerToolbar::saveRequested);
    connect(m_btn_meta,   &QToolButton::clicked, this, &ViewerToolbar::metaRequested);
    connect(btn_settings, &QToolButton::clicked, this, &ViewerToolbar::settingsRequested);

    // -- Centred command pill (matches the main window's uniBar "blue ribbon") -
    m_pill = new QFrame(this);
    m_pill->setObjectName("uniBar");
    // Expanding to fill its column (down to a sensible minimum) — the column ratio
    // sizes it to ~kCmdBarPct of the toolbar; the minimum keeps it usable on narrow
    // windows. No post-show fixup needed.
    m_pill->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pill->setMinimumWidth(kCmdBarMinW);
    auto* pill_l = new QHBoxLayout(m_pill);
    pill_l->setContentsMargins(Theme::kSpacing1, 0, Theme::kSpacing1, 0);
    pill_l->setSpacing(0);

    m_cmd_bar = new CommandBar(m_pill);
    m_cmd_bar->setObjectName("titleSearch");   // reuse the main-window search QSS
    m_cmd_bar->setFixedHeight(kCmdBarH);
    m_cmd_bar->setAnchorWidget(m_pill);        // palette spans the full pill
    m_cmd_bar->setToolTip(
        tr("Search and run viewer commands.\n"
           "Click or press the command shortcut, then type a command name."));
    pill_l->addWidget(m_cmd_bar, 1);

    // Focus turns the pill border accent-blue, exactly like the main window.
    connect(m_cmd_bar, &CommandBar::activeChanged, m_pill, [pill = m_pill](bool active) {
        if (pill->property("commandActive").toBool() == active) return;
        pill->setProperty("commandActive", active);
        pill->style()->unpolish(pill);
        pill->style()->polish(pill);
        pill->update();
    });
    // Fill column 1 (no horizontal alignment) so the pill width is the column width
    // — equal side columns keep it centred on the toolbar.
    m_grid->addWidget(m_pill, 0, 1, Qt::AlignVCenter);

    // -- Right section: callers append via addButton()/addWidget() -------------
    m_right = new QHBoxLayout;
    m_right->setContentsMargins(0, 0, 0, 0);
    m_right->setSpacing(2);
    m_grid->addLayout(m_right, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
}

void ViewerToolbar::setCommandProvider(std::function<QList<CommandPaletteItem>()> fn)
{
    m_cmd_bar->setProvider(std::move(fn));
}

void ViewerToolbar::setMetaButtonTip(const QString& tip)
{
    m_btn_meta->setToolTip(tip);
}

QToolButton* ViewerToolbar::addButton(const QString& icon_path, const QString& tip)
{
    auto* b = makeBtn(icon_path, tip);
    m_right->addWidget(b);
    return b;
}

void ViewerToolbar::addWidget(QWidget* w)
{
    if (w) m_right->addWidget(w);
}

void ViewerToolbar::addSpacing(int px)
{
    m_right->addSpacing(px);
}

QToolButton* ViewerToolbar::makeBtn(const QString& icon_path, const QString& tip)
{
    auto* b = new QToolButton(this);
    b->setIcon(QIcon(icon_path));
    b->setIconSize(QSize(kIconToolBar, kIconToolBar));
    b->setToolTip(tip);
    b->setObjectName("avQuickBtn");
    b->setFixedSize(kAvQuickBtnSz, kAvQuickBtnSz);
    return b;
}

} // namespace dolphin::ui
