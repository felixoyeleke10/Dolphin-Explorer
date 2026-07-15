// MainWindow.Tools.cpp - application key filter plus project-aware tool commands.
#include "ui/mainwindow/MainWindow.h"

#include "ui/mainwindow/coordinators/ToolController.h"
#include "ui/mainwindow/commands/LayerCommands.h"
#include "ui/features/map/sidescan/SidescanViewController.h"
#include "ui/features/map/sidescan/SidescanCorrectionDialog.h"
#include "ui/shell/WindowChrome.h"
#include "app/project/Project.h"
#include "core/Contact.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace dolphin::ui {

bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    // Apply the native dark frame when any top-level window first appears.
    if (ev->type() == QEvent::Show) {
        if (auto* window = qobject_cast<QWidget*>(obj); window && window->isWindow())
            applyDarkTitleBar(window);
    }

    // Single-letter tool keys remain app-wide but yield to text editors, spin
    // boxes, editable combos, dialogs, and popups.
    if (ev->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(ev);
        if (key->modifiers() == Qt::NoModifier
                && !QApplication::activeModalWidget()
                && !QApplication::activePopupWidget()) {
            QWidget* focus = QApplication::focusWidget();
            const bool typing = focus
                && (qobject_cast<QLineEdit*>(focus)
                    || qobject_cast<QTextEdit*>(focus)
                    || qobject_cast<QPlainTextEdit*>(focus)
                    || qobject_cast<QAbstractSpinBox*>(focus)
                    || (qobject_cast<QComboBox*>(focus)
                        && static_cast<QComboBox*>(focus)->isEditable()));
            if (!typing) {
                switch (key->key()) {
                case Qt::Key_V: onToolCursor();  return true;
                case Qt::Key_S: onToolSelect();  return true;
                case Qt::Key_Z: onToolZoom();    return true;
                case Qt::Key_M: onToolMeasure(); return true;
                case Qt::Key_C: onAddContact();  return true;
                default: break;
                }
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::onToolCursor()
{
    if (m_tool_ctrl) m_tool_ctrl->activatePan();
}

void MainWindow::onToolSelect()
{
    if (m_tool_ctrl) m_tool_ctrl->activateSelect();
}

void MainWindow::onToolZoom()
{
    if (m_tool_ctrl) m_tool_ctrl->activateZoom();
}

void MainWindow::onToolMeasure()
{
    if (m_tool_ctrl) m_tool_ctrl->activateMeasure();
}

void MainWindow::onBottomTrack()
{
    onSubBottomOpen();
}

void MainWindow::onAddContact()
{
    if (!currentProject()) {
        appendJobMessage(tr("Open a project before placing contacts."));
        onToolCursor();
        return;
    }
    if (m_tool_ctrl) m_tool_ctrl->activateContactPick();
}

void MainWindow::onToggle3D()
{
    if (m_tool_ctrl) m_tool_ctrl->toggle3D();
}

void MainWindow::onMeasurementUpdated(double metres)
{
    if (metres < 0.0) {
        appendJobMessage({});
        return;
    }
    appendJobMessage(metres >= 1000.0
        ? tr("Distance: %1 km").arg(metres / 1000.0, 0, 'f', 3)
        : tr("Distance: %1 m").arg(metres, 0, 'f', 1));
}

void MainWindow::onContactPickedOnMap(double lon, double lat)
{
    if (!currentProject()) return;
    core::Contact contact;
    contact.lat = lat;
    contact.lon = lon;
    contact.classification = m_pending_contact_class;
    contact.spatial_ref = currentProject()->displaySpatialRef();
    auto* command = new AddContactCommand(currentProject(), contact, [] {});
    m_undo_stack->push(command);

    QString label;
    for (const auto& existing : currentProject()->contacts())
        if (existing.id == command->assignedId()) {
            label = QString::fromStdString(existing.label);
            break;
        }
    appendJobMessage(tr("Contact %1 placed at %2, %3")
        .arg(label).arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6));
    recordActivity(ActivityKind::ContactPick, tr("Contact %1 placed").arg(label));
}

void MainWindow::onDrawFeature(int tool)
{
    if (!currentProject()) {
        appendJobMessage(tr("Open a project before drawing features."));
        onToolCursor();
        return;
    }
    if (m_tool_ctrl) m_tool_ctrl->activateFeature(tool);
}

void MainWindow::onFeatureDrawn(const std::vector<QPointF>& vertices, bool polygon)
{
    if (!currentProject() || vertices.size() < 2) return;

    core::Feature feature;
    feature.type = polygon ? core::FeatureType::Polygon : core::FeatureType::Polyline;
    feature.spatial_ref = currentProject()->displaySpatialRef();
    feature.vertices.reserve(vertices.size());
    for (const QPointF& vertex : vertices)
        feature.vertices.push_back(core::GeoVertex{vertex.y(), vertex.x()});

    auto* command = new AddFeatureCommand(currentProject(), feature, [] {});
    m_undo_stack->push(command);
    QString label;
    for (const auto& existing : currentProject()->features())
        if (existing.id == command->assignedId()) {
            label = QString::fromStdString(existing.label);
            break;
        }
    appendJobMessage(
        tr("Feature %1 added (%n point(s))", "", static_cast<int>(vertices.size()))
            .arg(label));
}

void MainWindow::showSidescanCorrectionDialog(bool navigation_mode)
{
    const QString layer_name = (!activeLayerId().empty() && currentProject())
        ? [this] {
              const auto* layer = currentProject()->findLayer(activeLayerId());
              return layer ? QString::fromStdString(layer->label)
                           : tr("No layer selected");
          }()
        : tr("No layer selected");

    auto* dialog = new SidescanCorrectionDialog(
        navigation_mode ? SidescanCorrectionDialog::Mode::Navigation
                        : SidescanCorrectionDialog::Mode::Heading,
        layer_name, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &SidescanCorrectionDialog::applyRequested,
            this, [this, dialog](SidescanCorrectionDialog::ApplyScope) {
                if (!m_sss_ctrl) return;
                m_sss_ctrl->setGeorefParams(dialog->headingParams());
                m_sss_ctrl->reloadCurrentLayer();
            });
    connect(dialog, &SidescanCorrectionDialog::previewRequested,
            this, [this, dialog] {
                if (!m_sss_ctrl) return;
                m_sss_ctrl->setGeorefParams(dialog->headingParams());
                m_sss_ctrl->reloadLayer(activeLayerId());
            });
    connect(dialog, &SidescanCorrectionDialog::resetRequested, this, [this] {
        if (!m_sss_ctrl) return;
        m_sss_ctrl->setGeorefParams({});
        m_sss_ctrl->reloadLayer(activeLayerId());
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::onRenumberContacts()
{
    showSidescanCorrectionDialog(true);
}

void MainWindow::onLineProps()
{
    showSidescanCorrectionDialog(false);
}

void MainWindow::onResetRaw()
{
    if (!m_sss_ctrl || activeLayerId().empty()) {
        appendJobMessage(tr("Select a layer first."));
        return;
    }
    m_sss_ctrl->setGeorefParams({});
    m_sss_ctrl->reloadCurrentLayer();
    appendJobMessage(tr("Layer reloaded with raw navigation."));
}

void MainWindow::onClearContacts()
{
    if (!currentProject()) return;
    const auto& contacts = currentProject()->contacts();
    if (contacts.empty()) return;
    if (QMessageBox::question(this, tr("Clear Contacts"),
            tr("Move all contacts to the Recycle Bin?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    std::vector<uint64_t> ids;
    ids.reserve(contacts.size());
    for (const auto& contact : contacts) ids.push_back(contact.id);
    m_undo_stack->beginMacro(tr("Clear Contacts"));
    for (const uint64_t id : ids)
        m_undo_stack->push(new RecycleContactCommand(currentProject(), id));
    m_undo_stack->endMacro();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About Dolphin Explorer"),
        tr("<b>Dolphin Explorer</b><br>"
           "Version 0.1<br><br>"
           "Marine survey data review platform.<br>"
           "© Astra Marine"));
}

} // namespace dolphin::ui
