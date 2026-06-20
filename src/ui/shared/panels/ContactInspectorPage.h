#pragma once
#include <QWidget>
#include "core/Contact.h"

class QLabel;
class QVBoxLayout;

namespace dolphin::ui {

class ElidingLabel;
class CollapsibleSection;

// Self-contained page widget shown in InspectorPanel when a Contact is selected.
// Renders the same key/value row style as the layer Info module so contact and
// line properties look identical.
class ContactInspectorPage : public QWidget {
    Q_OBJECT
public:
    explicit ContactInspectorPage(QWidget* parent = nullptr);
    void refresh(const core::Contact* contact);

private:
    void     build();
    QWidget* makeRow(QVBoxLayout* vl, const QString& key, ElidingLabel*& val_out);
    void     setOptionalRow(QWidget* row, ElidingLabel* val, const QString& value);

    ElidingLabel* m_label = nullptr;
    ElidingLabel* m_class = nullptr;
    ElidingLabel* m_conf  = nullptr;
    ElidingLabel* m_lat   = nullptr;
    ElidingLabel* m_lon   = nullptr;
    ElidingLabel* m_depth = nullptr;
    ElidingLabel* m_range = nullptr;
    ElidingLabel* m_width = nullptr;
    ElidingLabel* m_notes = nullptr;

    QWidget* m_depth_row = nullptr;
    QWidget* m_range_row = nullptr;
    QWidget* m_width_row = nullptr;
    QWidget* m_notes_row = nullptr;
};

} // namespace dolphin::ui
