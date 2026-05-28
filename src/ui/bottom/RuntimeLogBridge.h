#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

namespace dolphin::ui {

class RuntimeLogBridge : public QObject {
    Q_OBJECT
public:
    static RuntimeLogBridge* instance();
    static void publish(QtMsgType type, const QString& message);

    void replayPending();

signals:
    void messageLogged(int type, const QString& message);

private:
    explicit RuntimeLogBridge(QObject* parent = nullptr);
};

} // namespace dolphin::ui
