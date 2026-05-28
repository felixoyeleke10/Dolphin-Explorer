#pragma once
#include <QWidget>
#include "app/project/Project.h"

class QTableWidget;

namespace dolphin::ui {

class ContactListPanel : public QWidget {
    Q_OBJECT
public:
    explicit ContactListPanel(QWidget* parent = nullptr);

    void setProject(app::Project* project);
    void refresh();

signals:
    void contactSelected(uint64_t contact_id);
    void removeContactRequested(uint64_t contact_id);
    void exportContactsRequested();

private:
    app::Project*  m_project = nullptr;
    QTableWidget*  m_table   = nullptr;
};

} // namespace dolphin::ui
