#include "ui/systems/AppState.h"
#include "ui/mainwindow/AppSettingsDialog.h"
#include "ui/shared/CoordFormat.h"

namespace dolphin::ui {

AppState::AppState(QObject* parent)
    : QObject(parent)
{
    load();
}

void AppState::load()
{
    m_current = AppSettingsDialog::loadDefaults();
    setCoordinateDisplayFormat(m_current.coord_format);
}

void AppState::apply(const AppSettings& s)
{
    const AppSettings prev = m_current;
    m_current = s;
    setCoordinateDisplayFormat(s.coord_format);

    if (s.sound_velocity  != prev.sound_velocity)  emit soundVelocityChanged(s.sound_velocity);
    if (s.default_palette != prev.default_palette) emit defaultPaletteChanged(s.default_palette);
    if (s.auto_stretch    != prev.auto_stretch)    emit autoStretchChanged(s.auto_stretch);

    emit settingsChanged(s);
}

void AppState::setSelection(const SelectionState& s)
{
    if (s == m_selection) return;
    m_selection = s;
    emit selectionChanged(m_selection);
}

void AppState::setToolMode(ToolMode mode)
{
    if (mode == m_tool_mode) return;
    m_tool_mode = mode;
    emit toolModeChanged(m_tool_mode);
}

void AppState::postNotification(const Notification& n)
{
    emit notificationPosted(n);
}

} // namespace dolphin::ui
