#pragma once
// MenuSpec — a tiny declarative model for building QMenus from data.
//
// Instead of imperatively `menu.addAction(...)->setEnabled(...)` line by line, a menu
// is described as a std::vector<MenuEntry> and rendered by buildMenu(). Benefits:
//   • the menu becomes data (add/remove/reorder = edit the list, not surgery);
//   • separators auto-collapse (leading/trailing/duplicate separators are dropped),
//     so removing items never leaves a stray divider;
//   • `visible=false` omits an entry entirely (D-05: hide unfinished actions, don't
//     leave dead clickable stubs).
//
// Context menus across the app can share this renderer; each surface supplies its own
// entry list built from its context (clicked item, modality, mode, …).
#include <QKeySequence>
#include <QMenu>
#include <QString>

#include <functional>
#include <utility>
#include <vector>

namespace dolphin::ui {

struct MenuEntry {
    enum class Kind { Action, Separator, Submenu };

    Kind                  kind      = Kind::Action;
    QString               title;
    QKeySequence          shortcut;
    bool                  enabled   = true;
    bool                  visible   = true;
    bool                  checkable = false;
    bool                  checked   = false;
    std::function<void()> run;                 // Action handler
    std::vector<MenuEntry> children;            // Submenu contents

    // -- Readable factories ----------------------------------------------------
    static MenuEntry action(QString title, std::function<void()> run,
                            bool enabled = true, QKeySequence shortcut = {}) {
        MenuEntry e;
        e.title = std::move(title);
        e.run = std::move(run);
        e.enabled = enabled;
        e.shortcut = std::move(shortcut);
        return e;
    }
    static MenuEntry check(QString title, bool checked, std::function<void()> run,
                           bool enabled = true) {
        MenuEntry e = action(std::move(title), std::move(run), enabled);
        e.checkable = true;
        e.checked = checked;
        return e;
    }
    static MenuEntry separator() {
        MenuEntry e; e.kind = Kind::Separator; return e;
    }
    static MenuEntry submenu(QString title, std::vector<MenuEntry> children,
                             bool enabled = true, bool visible = true) {
        MenuEntry e;
        e.kind = Kind::Submenu;
        e.title = std::move(title);
        e.children = std::move(children);
        e.enabled = enabled;
        e.visible = visible;
        return e;
    }
};

// Render entries into `menu`. Recurses for submenus; auto-collapses separators.
inline void buildMenu(QMenu& menu, const std::vector<MenuEntry>& entries) {
    bool pending_sep = false;   // a separator requested but not yet emitted
    bool any_added   = false;   // at least one real item emitted before it

    for (const auto& e : entries) {
        if (!e.visible) continue;

        if (e.kind == MenuEntry::Kind::Separator) {
            pending_sep = any_added;   // ignore leading separators
            continue;
        }
        if (pending_sep) { menu.addSeparator(); pending_sep = false; }

        if (e.kind == MenuEntry::Kind::Submenu) {
            QMenu* sub = menu.addMenu(e.title);
            sub->setEnabled(e.enabled);
            buildMenu(*sub, e.children);
        } else {  // Action
            QAction* a = menu.addAction(e.title);
            a->setEnabled(e.enabled);
            if (e.checkable) { a->setCheckable(true); a->setChecked(e.checked); }
            if (!e.shortcut.isEmpty()) a->setShortcut(e.shortcut);
            if (e.run)
                QObject::connect(a, &QAction::triggered, &menu,
                                 [r = e.run]() { r(); });
        }
        any_added = true;
    }
}

} // namespace dolphin::ui
