#pragma once
#include <QWidget>

class QComboBox;
class QToolButton;

namespace dolphin::ui {

// ContactPickingPanel — the content of a "Contact Picking" tool section.
//
// A reusable section widget (used by the main-window right panel and the SBP
// viewer; the waterfall has its own inline section). It only emits intent — the
// owning surface decides how to activate its contact-pick tool. Contacts are
// POINT picks, distinct from feature shape annotations.
class ContactPickingPanel : public QWidget {
    Q_OBJECT
public:
    explicit ContactPickingPanel(QWidget* parent = nullptr);

    // Reflect the tool's active state back onto the toggle (signal-blocked).
    void    setPickActive(bool active);
    QString classification() const;

signals:
    void pickToggled(bool active);
    void classificationChanged(const QString& classification);
    void clearRequested();

private:
    QToolButton* m_pick_btn   = nullptr;
    QComboBox*   m_class_combo = nullptr;
};

} // namespace dolphin::ui
