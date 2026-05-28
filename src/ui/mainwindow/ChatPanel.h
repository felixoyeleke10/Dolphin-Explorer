#pragma once
#include <QFrame>
#include <QPoint>

class QScrollArea;
class QVBoxLayout;
class QLineEdit;

namespace dolphin::ui {

// Floating, draggable conversation panel anchored below the unified bar.
// Parented to MainWindow so it moves with the window.
// The header is the drag handle — click-and-drag to reposition.
class ConversationPanel : public QFrame {
    Q_OBJECT
public:
    explicit ConversationPanel(QWidget* parent = nullptr);

    // Position below `anchor` (right-aligned) and show.
    void popupBelow(QWidget* anchor);

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override;

private slots:
    void onSend();

private:
    void appendMessage(const QString& text, bool is_user);

    QScrollArea* m_scroll     = nullptr;
    QVBoxLayout* m_msg_layout = nullptr;
    QLineEdit*   m_input      = nullptr;
    QWidget*     m_header     = nullptr;   // drag handle

    bool   m_dragging          = false;
    QPoint m_drag_start_global;            // global mouse pos on press
    QPoint m_drag_start_pos;               // panel pos() in parent on press
};

} // namespace dolphin::ui
