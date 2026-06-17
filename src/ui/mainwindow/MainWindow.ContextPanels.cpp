// MainWindow.ContextPanels.cpp — buildContextPanel, makeContextPlaceholder,
//   refreshSidebarSections.
#include "ui/mainwindow/MainWindow.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/AppInfo.h"
#include "ui/shell/Theme.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shared/widgets/CollapsibleSection.h"
#include "ui/shared/widgets/SidePanelShell.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPoint>
#include <QSettings>
#include <QTimer>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace dolphin::ui {

void MainWindow::buildContextPanel(QWidget* parent)
{
    m_context_stack = new QStackedWidget(parent);
    m_context_stack->setObjectName("contextPanel");
    m_context_stack->setFixedWidth(Theme::kContextPanelW);

    auto* page = new SidePanelShell(m_context_stack);

    // -- Panel header ----------------------------------------------------------
    auto* hdr   = new QFrame(page);
    hdr->setObjectName("panelHdr");
    auto* hdr_l = new QHBoxLayout(hdr);
    hdr_l->setContentsMargins(Theme::kSpacing4, 10, Theme::kSpacing3, 10);
    auto* hdr_icon = new QLabel(hdr);
    hdr_icon->setFixedSize(14, 14);
    hdr_icon->setScaledContents(true);
    hdr_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    hdr_icon->setPixmap(QIcon(QStringLiteral(":/icons/explorer.svg")).pixmap(14, 14));
    hdr_l->addWidget(hdr_icon);
    hdr_l->addSpacing(4);
    m_context_title = new QLabel(tr("File Explorer"), hdr);
    m_context_title->setObjectName("panelTitle");
    hdr_l->addWidget(m_context_title, 1);
    page->setHeader(hdr);

    // -- Body — project tree + collapsible sections ---------------------------
    auto* body   = new QWidget(page);
    auto* layout = makeCompactLayout<QVBoxLayout>(body);

    // -- Project tree ----------------------------------------------------------
    m_line_list = new LineListPanel(body, LineListPanel::ContentMode::Explorer);
    layout->addWidget(m_line_list, 1);

    // -- Recent Projects section -----------------------------------------------
    auto* recent_sec = new CollapsibleSection(tr("Recent Projects"), body);
    recent_sec->setIcon(QStringLiteral(":/icons/recent_projects.svg"));
    m_sidebar_recent_list = new QListWidget(recent_sec);
    m_sidebar_recent_list->setObjectName("emptyStateRecentList");
    m_sidebar_recent_list->setFrameShape(QFrame::NoFrame);
    m_sidebar_recent_list->setMaximumHeight(8 * 24);
    m_sidebar_recent_list->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_sidebar_recent_list, &QListWidget::itemClicked,
            this, [this](QListWidgetItem* item) {
                // Defer so the mouse-release event fully unwinds before loadProject
                // starts calling setVisible() inside bindProjectUi().  On Windows,
                // ShowWindow mid-click-handler flushes the Win32 message queue and
                // can cause the main window to blink or lose focus.
                const QString path = item->data(Qt::UserRole).toString();
                QTimer::singleShot(0, this, [this, path]() {
                    m_session_ctrl->openProjectPath(path.toStdString());
                });
            });

    connect(m_sidebar_recent_list, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
                QListWidgetItem* item = m_sidebar_recent_list->itemAt(pos);
                const QString path = item ? item->data(Qt::UserRole).toString() : QString{};

                QMenu menu(m_sidebar_recent_list);

                if (item) {
                    menu.addAction(tr("Open"), this, [this, path]() {
                        QTimer::singleShot(0, this, [this, path]() {
                            m_session_ctrl->openProjectPath(path.toStdString());
                        });
                    });
                    menu.addAction(tr("Open Project Folder"), this, [path]() {
                        QDesktopServices::openUrl(
                            QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
                    });
                    menu.addSeparator();
                    menu.addAction(tr("Remove from Recent"), this, [this, path]() {
                        QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
                        QStringList list = s.value(QStringLiteral("recentProjects")).toStringList();
                        list.removeAll(path);
                        s.setValue(QStringLiteral("recentProjects"), list);
                        refreshSidebarSections(list);
                        rebuildRecentMenu();
                    });
                    menu.addSeparator();
                }

                menu.addAction(tr("Clear All Recent"), this, [this]() {
                    QSettings s(AppInfo::kOrgName, AppInfo::kSettingsApp);
                    s.remove(QStringLiteral("recentProjects"));
                    refreshSidebarSections({});
                    rebuildRecentMenu();
                });

                menu.exec(m_sidebar_recent_list->viewport()->mapToGlobal(pos));
            });
    recent_sec->setContent(m_sidebar_recent_list);
    layout->addWidget(recent_sec);

    // -- Recycle Bin section ---------------------------------------------------
    auto* recycle_sec = new CollapsibleSection(tr("Recycle Bin"), body);
    recycle_sec->setIcon(QStringLiteral(":/icons/recycle_bin.svg"));
    recycle_sec->setExpanded(false);
    auto* recycle_empty = new QLabel(tr("No deleted items."), recycle_sec);
    recycle_empty->setObjectName("ctrlEmptyHint");
    recycle_empty->setAlignment(Qt::AlignCenter);
    recycle_sec->setContent(recycle_empty);
    layout->addWidget(recycle_sec);

    page->setBody(body);

    m_context_stack->addWidget(page);  // index 0 — File Explorer
    m_context_stack->setCurrentIndex(0);

    QSettings s_init(AppInfo::kOrgName, AppInfo::kSettingsApp);
    refreshSidebarSections(s_init.value(QStringLiteral("recentProjects")).toStringList());
}

void MainWindow::refreshSidebarSections(const QStringList& paths)
{
    if (!m_sidebar_recent_list) return;
    m_sidebar_recent_list->clear();
    for (const QString& path : paths) {
        auto* item = new QListWidgetItem(QFileInfo(path).baseName(), m_sidebar_recent_list);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
    }
}

QWidget* MainWindow::makeContextPlaceholder(const QString& title, const QString& body)
{
    auto* page = new QWidget(m_context_stack);
    auto* layout = makeCompactLayout<QVBoxLayout>(page);

    auto* hdr = new QFrame(page);
    hdr->setObjectName("panelHdr");
    auto* hdr_l = new QHBoxLayout(hdr);
    hdr_l->setContentsMargins(Theme::kSpacing4, 10, Theme::kSpacing4, 10);
    auto* ttl = new QLabel(title, hdr);
    ttl->setObjectName("panelTitle");
    hdr_l->addWidget(ttl);
    layout->addWidget(hdr);

    auto* body_lbl = new QLabel(body, page);
    body_lbl->setObjectName("panelPlaceholder");
    body_lbl->setWordWrap(true);
    body_lbl->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(body_lbl);
    layout->addStretch();
    return page;
}

} // namespace dolphin::ui
