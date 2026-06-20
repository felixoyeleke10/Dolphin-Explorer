#pragma once
// CommandBar — drop-in toolbar widget that opens a CommandPaletteDialog on click.
//
// Usage:
//   auto* bar = new CommandBar(toolbar);
//   bar->setFixedHeight(kCmdBarH);
//   bar->setProvider([this] { return buildMyCommands(); });
//   layout->addWidget(bar);
//
// The palette dialog is created lazily on first open() and is anchored to the
// host window, not to the bar itself.  Width must still be managed by the
// caller (e.g. via setFixedWidth in a Resize eventFilter on the toolbar).
//
// Forwarded signals: QLineEdit::returnPressed is still emitted — connect it if
// you want to handle typed text when the bar is made editable in the future.

#include "ui/shared/dialogs/CommandPaletteDialog.h"
#include <QLineEdit>
#include <functional>

namespace dolphin::ui {

class CommandBar : public QLineEdit {
    Q_OBJECT
public:
    explicit CommandBar(QWidget* parent = nullptr);

    // Called once per window to register the item provider.
    // The lambda is invoked every time the palette is about to open.
    void setProvider(std::function<QList<CommandPaletteItem>()> fn);

    // Open the palette programmatically (e.g., from a keyboard shortcut).
    void open();

    // Override the anchor widget used to position and size the palette popup.
    // By default the palette anchors to this CommandBar widget.
    // Pass the enclosing pill/bar frame when you want the palette wider than
    // the search field alone (e.g. the unified bar in the main window).
    void setAnchorWidget(QWidget* anchor);

protected:
    bool event(QEvent* ev) override;
    bool eventFilter(QObject* watched, QEvent* ev) override;

signals:
    void activeChanged(bool active);

private:
    void setActive(bool active);

    CommandPaletteDialog*                      m_palette  = nullptr;
    std::function<QList<CommandPaletteItem>()> m_provider;
    QWidget*                                   m_anchor   = nullptr;
    bool                                       m_active   = false;
};

} // namespace dolphin::ui
