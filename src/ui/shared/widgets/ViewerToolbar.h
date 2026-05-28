#pragma once
// ViewerToolbar — shared toolbar chrome for acoustic viewer windows.
//
// Owns the five standard left buttons (new/open/save/meta/settings), the
// centred CommandBar, and the responsive-width event filter. Each viewer
// appends its own right-section widgets via addButton() / addWidget().
//
// Usage:
//   m_toolbar = new ViewerToolbar(this);
//   m_toolbar->setCommandProvider([this] { return buildCommandItems(); });
//   m_toolbar->setMetaButtonTip(tr("Open sidescan metadata…"));
//   connect(m_toolbar, &ViewerToolbar::newRequested, this, &MyWindow::newFileRequested);
//   auto* btn = m_toolbar->addButton(":/icons/measure.svg", tr("Measure…"));
//   btn->setEnabled(false);
//   layout()->insertWidget(0, m_toolbar);

#include "ui/shared/dialogs/CommandPaletteDialog.h"
#include <QFrame>
#include <functional>

class QHBoxLayout;
class QToolButton;

namespace dolphin::ui {

class CommandBar;

class ViewerToolbar : public QFrame {
    Q_OBJECT
public:
    explicit ViewerToolbar(QWidget* parent = nullptr);

    void setCommandProvider(std::function<QList<CommandPaletteItem>()> fn);

    // Override the default meta-button tooltip with viewer-specific wording.
    void setMetaButtonTip(const QString& tip);

    // Append a standard-styled avQuickBtn to the right section.
    // Returns the button so the caller can setCheckable(), setEnabled(), etc.
    QToolButton* addButton(const QString& icon_path, const QString& tip);

    // Append an arbitrary widget (e.g. QComboBox) to the right section.
    void addWidget(QWidget* w);
    void addSpacing(int px);

    CommandBar* commandBar() const { return m_cmd_bar; }

signals:
    void newRequested();
    void openRequested();
    void saveRequested();
    void metaRequested();
    void settingsRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override;

private:
    QToolButton* makeBtn(const QString& icon_path, const QString& tip);

    CommandBar*  m_cmd_bar  = nullptr;
    QToolButton* m_btn_meta = nullptr;
    QHBoxLayout* m_layout   = nullptr;
};

} // namespace dolphin::ui
