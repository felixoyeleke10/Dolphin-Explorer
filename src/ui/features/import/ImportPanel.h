#pragma once
#include <QWidget>

class QPushButton;
class QLabel;

namespace dolphin::ui {

class ImportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ImportPanel(QWidget* parent = nullptr);

    void setProjectName(const QString& name);

signals:
    void newProjectRequested();
    void openProjectRequested();
    void importFileRequested();

private:
    QLabel*      m_project_label = nullptr;
    QPushButton* m_btn_new       = nullptr;
    QPushButton* m_btn_open      = nullptr;
    QPushButton* m_btn_import    = nullptr;
};

} // namespace dolphin::ui
