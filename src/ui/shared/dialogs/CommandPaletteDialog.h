#pragma once
#include <QDialog>
#include <QList>
#include <functional>
#include <QString>

class QLineEdit;
class QListWidget;

namespace dolphin::ui {

struct CommandPaletteItem {
    QString   category;
    QString   label;
    QString   shortcut;
    QString   aliases;      // space-separated shorthands, e.g. "qc contacts review"
    bool      enabled    = true;
    bool      searchOnly = false;  // if true, hidden when filter is empty
    std::function<void()> action;
};

class CommandPaletteDialog : public QDialog {
    Q_OBJECT
public:
    explicit CommandPaletteDialog(QWidget* parent = nullptr);

    void setItems(QList<CommandPaletteItem> items);
    void popup(QWidget* anchor);

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override;
    void paintEvent(QPaintEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

private:
    void applyFilter(const QString& text);
    void executeSelected();
    void moveHighlight(int delta);
    void updateHeight();

    QLineEdit*   m_input;
    QListWidget* m_list;
    QList<CommandPaletteItem> m_all;
};

} // namespace dolphin::ui
