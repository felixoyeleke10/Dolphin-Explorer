#include "ui/shared/widgets/ViewerToolbar.h"
#include "ui/shared/widgets/CommandBar.h"
#include "ui/shell/Theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QSize>
#include <QToolButton>

namespace dolphin::ui {

using namespace Theme;

ViewerToolbar::ViewerToolbar(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("av_toolbar");
    setFixedHeight(kAvToolBarH);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(10, 0, 10, 0);
    m_layout->setSpacing(2);

    // ── Standard left buttons ─────────────────────────────────────────────────
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

    m_layout->addWidget(btn_new);
    m_layout->addWidget(btn_open);
    m_layout->addWidget(btn_save);
    m_layout->addWidget(m_btn_meta);
    m_layout->addWidget(btn_settings);
    m_layout->addSpacing(6);

    connect(btn_new,      &QToolButton::clicked, this, &ViewerToolbar::newRequested);
    connect(btn_open,     &QToolButton::clicked, this, &ViewerToolbar::openRequested);
    connect(btn_save,     &QToolButton::clicked, this, &ViewerToolbar::saveRequested);
    connect(m_btn_meta,   &QToolButton::clicked, this, &ViewerToolbar::metaRequested);
    connect(btn_settings, &QToolButton::clicked, this, &ViewerToolbar::settingsRequested);

    // ── Centred command bar ───────────────────────────────────────────────────
    m_layout->addStretch(1);

    m_cmd_bar = new CommandBar(this);
    m_cmd_bar->setObjectName("avCommandBar");
    m_cmd_bar->setFixedHeight(kCmdBarH);
    m_cmd_bar->setToolTip(
        tr("Search and run viewer commands.\n"
           "Click or press the command shortcut, then type a command name."));
    m_layout->addWidget(m_cmd_bar);

    m_layout->addStretch(1);
    m_layout->addSpacing(6);

    // Right section: callers append buttons/widgets via addButton()/addWidget().

    installEventFilter(this);
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
    m_layout->addWidget(b);
    return b;
}

void ViewerToolbar::addWidget(QWidget* w)
{
    if (w) m_layout->addWidget(w);
}

void ViewerToolbar::addSpacing(int px)
{
    m_layout->addSpacing(px);
}

bool ViewerToolbar::eventFilter(QObject* watched, QEvent* ev)
{
    if (watched == this
            && (ev->type() == QEvent::Resize || ev->type() == QEvent::Show)) {
        const int sw = qBound(kCmdBarMinW, width() * kCmdBarPct / 100, kCmdBarMaxW);
        m_cmd_bar->setFixedWidth(sw);
        return false;
    }
    return QFrame::eventFilter(watched, ev);
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
