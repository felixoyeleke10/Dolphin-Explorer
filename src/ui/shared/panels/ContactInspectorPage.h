#pragma once
#include <QWidget>
#include "core/Contact.h"

class QComboBox;
class QLineEdit;
class QTextEdit;

namespace dolphin::ui {

// Self-contained page widget shown in InspectorPanel when a Contact is selected.
class ContactInspectorPage : public QWidget {
    Q_OBJECT
public:
    explicit ContactInspectorPage(QWidget* parent = nullptr);
    void refresh(const core::Contact* contact);

private:
    void build();

    QLineEdit* m_label = nullptr;
    QLineEdit* m_lat   = nullptr;
    QLineEdit* m_lon   = nullptr;
    QLineEdit* m_depth = nullptr;
    QLineEdit* m_range = nullptr;
    QLineEdit* m_width = nullptr;
    QComboBox* m_class = nullptr;
    QComboBox* m_conf  = nullptr;
    QTextEdit* m_notes = nullptr;
};

} // namespace dolphin::ui
