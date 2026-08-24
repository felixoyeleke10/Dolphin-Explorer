#include "MapEmptyStateLauncher.h"

#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

MapEmptyStateLauncher::MapEmptyStateLauncher(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = makeCompactLayout<QVBoxLayout>(this);
    outer->addStretch(40);

    auto* card = new QFrame(this);
    card->setObjectName("launcherCard");
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setFixedWidth(420);
    auto* content = new QVBoxLayout(card);
    content->setContentsMargins(24, 30, 24, 14);
    content->setSpacing(0);

    auto* logo = new QLabel(card);
    logo->setPixmap(QIcon(QStringLiteral(":/icons/dolphin_logo.svg")).pixmap(48, 48));
    logo->setAlignment(Qt::AlignCenter);
    content->addWidget(logo, 0, Qt::AlignHCenter);
    content->addSpacing(10);

    auto* title = new QLabel(tr("Dolphin Explorer"), card);
    title->setObjectName("launcherTitle");
    content->addWidget(title, 0, Qt::AlignHCenter);
    content->addSpacing(2);
    auto* subtitle = new QLabel(tr("Marine survey workstation"), card);
    subtitle->setObjectName("launcherSub");
    content->addWidget(subtitle, 0, Qt::AlignHCenter);
    content->addSpacing(22);

    auto* actions = new QWidget(card);
    auto* action_layout = makeCompactLayout<QHBoxLayout>(actions);
    action_layout->setSpacing(Theme::kSpacing3);
    auto* import_button = new QPushButton(tr("Import Files…"), actions);
    import_button->setObjectName("mapImportHintBtn");
    import_button->setCursor(Qt::PointingHandCursor);
    connect(import_button, &QPushButton::clicked, this,
            &MapEmptyStateLauncher::importFilesRequested);
    auto* new_button = new QPushButton(tr("New Project"), actions);
    new_button->setObjectName("launcherNewBtn");
    new_button->setCursor(Qt::PointingHandCursor);
    connect(new_button, &QPushButton::clicked, this,
            &MapEmptyStateLauncher::newProjectRequested);
    action_layout->addWidget(import_button);
    action_layout->addWidget(new_button);
    content->addWidget(actions, 0, Qt::AlignHCenter);
    content->addSpacing(24);

    m_recent_box = new QFrame(card);
    m_recent_box->setObjectName("mapRecentBox");
    auto* recent_layout = new QVBoxLayout(m_recent_box);
    recent_layout->setContentsMargins(0, 0, 0, 0);
    recent_layout->setSpacing(2);
    auto* heading = new QLabel(tr("RECENT"), m_recent_box);
    heading->setObjectName("mapRecentHdr");
    recent_layout->addWidget(heading);
    m_recent_items = new QVBoxLayout();
    m_recent_items->setContentsMargins(0, 2, 0, 0);
    m_recent_items->setSpacing(1);
    recent_layout->addLayout(m_recent_items);
    m_recent_box->hide();
    content->addWidget(m_recent_box);

    outer->addWidget(card, 0, Qt::AlignHCenter);
    outer->addStretch(52);
}

void MapEmptyStateLauncher::setRecentProjects(const QStringList& names,
                                              const QStringList& paths)
{
    while (auto* item = m_recent_items->takeAt(0)) {
        if (auto* widget = item->widget()) widget->deleteLater();
        delete item;
    }

    const int count = std::min({static_cast<int>(names.size()),
                                static_cast<int>(paths.size()), 5});
    for (int i = 0; i < count; ++i) {
        const QString path = paths[i];
        auto* button = new QPushButton(m_recent_box);
        button->setObjectName("mapRecentBtn");
        button->setFlat(true);
        button->setFixedHeight(46);
        button->setToolTip(path);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this,
                [this, path]() { emit openProjectRequested(path); });

        auto* row = new QHBoxLayout(button);
        row->setContentsMargins(8, 0, 10, 0);
        row->setSpacing(10);
        auto* chip = new QLabel(button);
        chip->setObjectName("mapRecentChip");
        chip->setFixedSize(28, 28);
        chip->setAlignment(Qt::AlignCenter);
        chip->setPixmap(Theme::icon(QStringLiteral(":/icons/recent_projects.svg")).pixmap(14, 14));
        chip->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto* text_column = new QWidget(button);
        text_column->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* labels = makeCompactLayout<QVBoxLayout>(text_column);
        labels->setSpacing(1);
        auto* name = new QLabel(names[i], text_column);
        name->setObjectName("mapRecentName");
        const QFileInfo info(path);
        auto* metadata = new QLabel(
            info.exists() ? QLocale().toString(info.lastModified().date(), QLocale::ShortFormat)
                          : tr("Not found"), text_column);
        metadata->setObjectName("mapRecentMeta");
        labels->addStretch(1);
        labels->addWidget(name);
        labels->addWidget(metadata);
        labels->addStretch(1);
        row->addWidget(chip);
        row->addWidget(text_column, 1);
        m_recent_items->addWidget(button);
    }
    m_recent_box->setVisible(count > 0);
}

} // namespace dolphin::ui
