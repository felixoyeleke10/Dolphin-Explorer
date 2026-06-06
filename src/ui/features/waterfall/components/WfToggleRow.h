#pragma once
#include <QWidget>
#include <QString>

class QPropertyAnimation;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  WfToggleRow — animated iOS-style toggle switch row.
//
//  Visual layout:
//    Label text                                   [  ●      ]
//
//  Click anywhere to toggle. Thumb slides with eased animation.
//  setChecked() snaps without animation (for programmatic init).
// -----------------------------------------------------------------------------

class WfToggleRow : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal animPos READ animPos WRITE setAnimPos)

public:
    explicit WfToggleRow(const QString& label,
                         bool checked = false,
                         QWidget* parent = nullptr);

    bool isChecked()     const { return m_checked; }
    bool isAtDefault()   const { return m_checked == m_default_checked; }
    void setChecked(bool v);   // no signal emitted

    // Q_PROPERTY accessors
    qreal animPos() const          { return m_anim_pos; }
    void  setAnimPos(qreal v)      { m_anim_pos = v; update(); }

signals:
    void toggled(bool checked);

protected:
    void paintEvent  (QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void enterEvent  (QEnterEvent*)  override;
    void leaveEvent  (QEvent*)       override;

private:
    QString             m_label;
    bool                m_checked         = false;
    bool                m_default_checked = false;
    bool                m_hovered         = false;
    qreal               m_anim_pos = 0.0;
    QPropertyAnimation* m_animation = nullptr;
};

} // namespace dolphin::ui
