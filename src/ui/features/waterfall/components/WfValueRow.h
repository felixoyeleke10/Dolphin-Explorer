#pragma once
#include <QWidget>
#include <QString>

class QLineEdit;
class QTimer;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  WfValueRow — professional parameter row for instrument panels.
//
//  Visual layout:
//    Label text                    [ value suffix  | ▲ ]
//                                  [               | ▼ ]
//
//  Interaction:
//    • Click ▲ / ▼               — step +1 / -1; hold for auto-repeat
//    • Drag up/down on value area — continuous change (Shift = ×0.1 fine)
//    • Click value area           — inline line-edit (Enter commits, Esc cancels)
//    • Mouse wheel                — always ignored; can't be changed by accident
//
//  No native widget chrome — entirely custom-painted.
// -----------------------------------------------------------------------------

class WfValueRow : public QWidget {
    Q_OBJECT
public:
    // Shared form geometry for companion controls (combos, selectors).  Keeping
    // these here prevents mixed rows from drifting onto different columns.
    static constexpr int controlWidth() { return 116; }
    static constexpr int controlRightMargin() { return 10; }

    WfValueRow(const QString& label,
               double lo, double hi, double value,
               double step,
               int    decimals,
               const QString& suffix = {},
               QWidget* parent = nullptr);

    double value()       const { return m_value; }
    int    intValue()    const;
    bool   isAtDefault() const;
    void   setValue(double v);

    // Shows a custom string when value == trigger (e.g. 0.0 → "Off").
    void setSpecialValueText(double trigger, const QString& text);

signals:
    void valueChanged(double v);

protected:
    void paintEvent       (QPaintEvent*)  override;
    void mousePressEvent  (QMouseEvent*)  override;
    void mouseMoveEvent   (QMouseEvent*)  override;
    void mouseReleaseEvent(QMouseEvent*)  override;
    void wheelEvent       (QWheelEvent*)  override;
    void leaveEvent       (QEvent*)       override;
    bool eventFilter      (QObject*, QEvent*) override;

private slots:
    void onRepeatTick();

private:
    // -- Geometry ----------------------------------------------------------
    QRect pillRect()      const;
    QRect arrowAreaRect() const;
    QRect upArrowRect()   const;
    QRect downArrowRect() const;
    QRect valueAreaRect() const;

    // -- Helpers -----------------------------------------------------------
    void    step(int dir);
    void    startEdit();
    void    commitEdit();
    void    cancelEdit();
    QString formatValue() const;
    double  clamp(double v) const;

    // -- Hit zones ---------------------------------------------------------
    enum class Zone { None, Value, UpArrow, DownArrow };
    Zone zoneAt(const QPoint& pos) const;

    // -- Value state -------------------------------------------------------
    QString m_label;
    double  m_lo, m_hi, m_step, m_value, m_default;
    int     m_decimals;
    QString m_suffix;

    // -- Special display ---------------------------------------------------
    bool    m_has_special  = false;
    double  m_special_trig = 0.0;
    QString m_special_text;

    // -- Interaction state -------------------------------------------------
    Zone    m_hover_zone      = Zone::None;
    Zone    m_press_zone      = Zone::None;
    bool    m_dragging        = false;
    double  m_drag_origin_y   = 0.0;
    double  m_drag_origin_v   = 0.0;
    QTimer* m_repeat_timer    = nullptr;

    // -- Edit overlay ------------------------------------------------------
    QLineEdit* m_edit = nullptr;
};

} // namespace dolphin::ui
