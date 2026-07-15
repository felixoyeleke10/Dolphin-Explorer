// Shared tag and group submenus used by LineListPanel context-menu aspects.
#include "ui/shared/panels/LineListPanel.ContextMenu_p.h"
#include "ui/shared/panels/LineListPanel_p.h"

#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <utility>

namespace dolphin::ui::detail {

namespace {

QIcon makePaletteIcon(const TagPaletteEntry& entry)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(entry.r, entry.g, entry.b));
        painter.drawEllipse(1, 1, 12, 12);
    }
    return QIcon(pixmap);
}

} // namespace

QMenu* buildTagMenu(QWidget* parent,
                    const std::vector<std::string>& current_tags,
                    std::function<void(std::vector<std::string>)> apply)
{
    auto* menu = new QMenu(QObject::tr("Tags"), parent);

    for (const auto& entry : kTagPalette) {
        const std::string tag_id(entry.id);
        const bool active = std::find(current_tags.begin(), current_tags.end(), tag_id)
                            != current_tags.end();

        auto* action = menu->addAction(
            makePaletteIcon(entry), QObject::tr(entry.label), parent,
            [current_tags, tag_id, apply]() {
                auto tags = current_tags;
                const auto it = std::find(tags.begin(), tags.end(), tag_id);
                if (it != tags.end()) tags.erase(it);
                else                  tags.push_back(tag_id);
                apply(std::move(tags));
            });
        action->setCheckable(true);
        action->setChecked(active);
    }

    if (!current_tags.empty()) {
        menu->addSeparator();
        menu->addAction(QObject::tr("Clear All Tags"), parent,
                        [apply]() { apply({}); });
    }

    return menu;
}

QMenu* buildGroupMenu(QWidget* parent,
                      const std::vector<app::ItemGroup>& groups,
                      const std::string& current_group_id,
                      std::function<void(std::string)> apply_group,
                      std::function<app::ItemGroup*(const std::string&)> create_group)
{
    auto* menu = new QMenu(QObject::tr("Group"), parent);

    if (!current_group_id.empty()) {
        menu->addAction(QObject::tr("Remove from Group"), parent,
                        [apply_group]() { apply_group({}); });
        menu->addSeparator();
    }

    for (const auto& group : groups) {
        auto* action = menu->addAction(
            QString::fromStdString(group.name), parent,
            [group_id = group.id, apply_group]() { apply_group(group_id); });
        if (group.id == current_group_id) {
            action->setCheckable(true);
            action->setChecked(true);
        }
    }

    menu->addSeparator();
    menu->addAction(QObject::tr("New Group…"), parent,
                    [parent, create_group, apply_group]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            parent, QObject::tr("New Group"), QObject::tr("Group name:"),
            QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        auto* group = create_group(name.toStdString());
        if (group) apply_group(group->id);
    });

    return menu;
}

QMenu* buildBulkLayerGroupMenu(
    QWidget* parent,
    const std::vector<app::ItemGroup>& groups,
    int layer_count,
    std::function<void(std::string)> apply_group,
    std::function<app::ItemGroup*(const std::string&)> create_group)
{
    auto* menu = new QMenu(QObject::tr("Group"), parent);

    menu->addAction(QObject::tr("New Group from Selection..."), parent,
                    [parent, layer_count, apply_group, create_group]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            parent, QObject::tr("New Group"), QObject::tr("Group name:"),
            QLineEdit::Normal,
            QObject::tr("%n selected lines", nullptr, layer_count), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        auto* group = create_group(name.toStdString());
        if (group) apply_group(group->id);
    });

    menu->addSeparator();

    if (groups.empty()) {
        menu->addAction(QObject::tr("No existing groups"))->setEnabled(false);
    } else {
        for (const auto& group : groups) {
            menu->addAction(
                QString::fromStdString(group.name), parent,
                [group_id = group.id, apply_group]() { apply_group(group_id); });
        }
    }

    menu->addSeparator();
    menu->addAction(QObject::tr("Remove from Group"), parent,
                    [apply_group]() { apply_group({}); });

    return menu;
}

} // namespace dolphin::ui::detail
