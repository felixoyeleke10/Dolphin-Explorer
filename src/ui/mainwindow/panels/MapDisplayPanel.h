#pragma once
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  MapDisplayPanel — the "Map" page of the right panel's upper tabs.
//
//  Map view working options (SonarWiz-style), not app settings:
//    GENERAL           — Show tooltips · Highlight items under cursor
//    CAMERA PROPERTIES — Azimuth (°) · Height/Depth (m)
//
//  The view options persist (QSettings "map/…"); camera values are live view
//  state — the panel edits them and mirrors changes made by mouse navigation
//  (MainWindow feeds viewportChanged back via setCameraReadout).
// -----------------------------------------------------------------------------
class MapDisplayPanel : public QWidget {
    Q_OBJECT
public:
    explicit MapDisplayPanel(QWidget* parent = nullptr);

    // Mirror live camera state (signal-blocked; called on viewportChanged).
    void setCameraReadout(double azimuth_deg, double height_m);

    // Re-emit every option once — MainWindow calls this after wiring so the
    // views start with the persisted values.
    void broadcastState();

signals:
    void tooltipsToggled(bool on);
    void hoverHighlightToggled(bool on);
    void azimuthEdited(double deg);
    void heightEdited(double metres);

private:
    void persist() const;

    QCheckBox*      m_tooltips_check  = nullptr;
    QCheckBox*      m_highlight_check = nullptr;
    QDoubleSpinBox* m_azimuth_spin    = nullptr;
    QDoubleSpinBox* m_height_spin     = nullptr;
    bool            m_loading         = false;
};

} // namespace dolphin::ui
