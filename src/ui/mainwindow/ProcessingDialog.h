#pragma once
#include <QDialog>
#include <QMap>
#include <QString>
#include <QPoint>

class QLabel;
class QTextEdit;
class QProgressBar;
class QPushButton;
class QTimer;

namespace dolphin::ui {

// App-wide processing modal. Frameless, dark-themed, draggable.
// MainWindow calls taskBegin/Done/Fail per operation; the dialog
// auto-closes when all active tasks finish.
class ProcessingDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProcessingDialog(QWidget* parent = nullptr);

    void taskBegin(const QString& id, const QString& label);
    void taskStep (const QString& id, const QString& label);
    void taskDone (const QString& id);
    void taskFail (const QString& id, const QString& error = {});

signals:
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;

private:
    void appendLog(const QString& msgHtml);
    void onCancelClicked();
    void tickSpinner();

    struct Task { QString label; bool active = true; };

    QLabel*       m_spinner    = nullptr;
    QLabel*       m_status     = nullptr;
    QProgressBar* m_bar        = nullptr;
    QTextEdit*    m_log        = nullptr;
    QPushButton*  m_cancel     = nullptr;
    QTimer*       m_spin_timer = nullptr;

    QMap<QString, Task> m_tasks;
    int    m_active_count = 0;
    int    m_spin_frame   = 0;
    bool   m_dragging     = false;
    QPoint m_drag_pos;
};

} // namespace dolphin::ui
