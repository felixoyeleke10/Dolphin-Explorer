#pragma once

#include <QStringList>
#include <QWidget>

class QVBoxLayout;
class QFrame;

namespace dolphin::ui {

class MapEmptyStateLauncher : public QWidget {
    Q_OBJECT
public:
    explicit MapEmptyStateLauncher(QWidget* parent = nullptr);

    void setRecentProjects(const QStringList& names, const QStringList& paths);

signals:
    void importFilesRequested();
    void newProjectRequested();
    void openProjectRequested(const QString& path);

private:
    QFrame* m_recent_box = nullptr;
    QVBoxLayout* m_recent_items = nullptr;
};

} // namespace dolphin::ui
