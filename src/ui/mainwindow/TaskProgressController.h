#pragma once
#include <QObject>
#include <QString>

namespace dolphin::app { class OperationManager; }
class QWidget;

namespace dolphin::ui {

class ProcessingDialog;

// Owns the app-wide ProcessingDialog lifecycle.
// Lazy-creates the dialog on first taskBegin(), auto-clears on dialog close.
// Connects cancelRequested → OperationManager::cancelAll() internally.
class TaskProgressController : public QObject {
    Q_OBJECT
public:
    explicit TaskProgressController(QWidget*               dialog_parent,
                                    app::OperationManager* op_mgr,
                                    QObject*               parent = nullptr);

public slots:
    void taskBegin(const QString& id, const QString& label);
    void taskDone (const QString& id);
    void taskFail (const QString& id, const QString& error = {});

private:
    void onDialogDestroyed();
    void onCancelRequested();

    QWidget*               m_parent;
    app::OperationManager* m_op_mgr;
    ProcessingDialog*      m_dlg = nullptr;
};

} // namespace dolphin::ui
