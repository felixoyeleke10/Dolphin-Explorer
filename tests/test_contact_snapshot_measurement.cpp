#include "ui/features/contacts/ContactSnapshotView.h"
#include "ui/features/contacts/ContactEditorDialog.h"
#include "app/project/Project.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPixmap>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

namespace {
int failures = 0;

void check(bool ok, const char* message)
{
    if (ok) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

void drag(QWidget& widget, QPointF from, QPointF to)
{
    QMouseEvent press(QEvent::MouseButtonPress, from, from, from,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QMouseEvent move(QEvent::MouseMove, to, to, to,
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, to, to, to,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    dolphin::ui::ContactSnapshotView view;
    view.resize(400, 400);
    QPixmap image(100, 100);
    image.fill(Qt::black);
    view.setPixmap(image);
    view.setMeasurementScale(2.f, 3.f);

    int mode = 0;
    double metres = 0.0;
    int emissions = 0;
    QObject::connect(&view, &dolphin::ui::ContactSnapshotView::measurementCompleted,
                     [&](int m, double value) {
        mode = m; metres = value; ++emissions;
    });

    view.setMeasurementMode(dolphin::ui::ContactSnapshotView::MeasureHeight);
    drag(view, {200, 160}, {300, 220});
    check(mode == dolphin::ui::ContactSnapshotView::MeasureHeight,
          "height drag emits height mode");
    check(std::abs(metres - 45.0) < 0.01,
          "height ignores horizontal motion and uses along-track scale");

    view.setMeasurementMode(dolphin::ui::ContactSnapshotView::MeasureWidth);
    drag(view, {200, 200}, {300, 300});
    check(mode == dolphin::ui::ContactSnapshotView::MeasureWidth,
          "width drag emits width mode");
    check(std::abs(metres - 50.0) < 0.01,
          "width ignores vertical motion and uses across-track scale");

    view.setContactSide(-1); // port shadows must extend left, away from nadir
    view.setMeasurementMode(dolphin::ui::ContactSnapshotView::MeasureShadow);
    const int before = emissions;
    drag(view, {200, 200}, {300, 200});
    check(emissions == before,
          "port shadow dragged toward nadir is rejected as zero length");
    drag(view, {200, 200}, {100, 200});
    check(mode == dolphin::ui::ContactSnapshotView::MeasureShadow,
          "outward port shadow emits shadow mode");
    check(std::abs(metres - 50.0) < 0.01,
          "outward shadow uses across-track scale");

    view.setContactSide(1); // starboard shadows must extend right
    view.setMeasurementMode(dolphin::ui::ContactSnapshotView::MeasureShadow);
    const int before_starboard = emissions;
    drag(view, {200, 200}, {100, 200});
    check(emissions == before_starboard,
          "starboard shadow dragged toward nadir is rejected as zero length");
    drag(view, {200, 200}, {300, 200});
    check(emissions == before_starboard + 1,
          "outward starboard shadow emits exactly once");

    view.setMeasurementMode(dolphin::ui::ContactSnapshotView::MeasureWidth);
    view.setScalePercent(50);
    const int before_margin = emissions;
    drag(view, {10, 10}, {200, 200});
    check(emissions == before_margin,
          "a drag beginning outside the displayed sonar image is ignored");
    view.setScalePercent(100);

    view.setMeasurementScale(0.f, 0.f);
    view.setMeasurementMode(dolphin::ui::ContactSnapshotView::MeasureHeight);
    const int before_uncalibrated = emissions;
    drag(view, {200, 160}, {200, 240});
    check(emissions == before_uncalibrated,
          "uncalibrated imagery cannot emit misleading dimensions");

    // Full editor path: the existing left-side label must arm the canvas and
    // the resulting drag must update the existing numeric field.
    QTemporaryDir temp;
    check(temp.isValid(), "temporary project directory is available");
    auto project = dolphin::app::Project::create(
        "Measurements", temp.filePath("measurements.dlp").toStdString());
    dolphin::core::Contact contact;
    contact.range_m = 40.f;
    project->addContact(contact);
    const uint64_t contact_id = project->contacts().front().id;

    dolphin::ui::ContactEditorDialog editor(project.get(), {contact_id}, contact_id);
    int provider_calls = 0;
    editor.setSnapshotProvider([&](const dolphin::core::Contact&) {
        ++provider_calls;
        dolphin::ui::ContactSnapshotData data;
        data.pixmap = QPixmap(100, 100);
        data.pixmap.fill(Qt::black);
        data.across_m_per_px = 2.f;
        data.along_m_per_px = 3.f;
        return data;
    });
    editor.show();
    QApplication::processEvents();
    check(provider_calls == 1,
          "legacy uncalibrated contact requests one atomic image+calibration package");

    QLabel* height_label = nullptr;
    for (QLabel* label : editor.findChildren<QLabel*>())
        if (label->property("measurementKind").toString() == QStringLiteral("height"))
            height_label = label;
    QDoubleSpinBox* height_field = nullptr;
    for (QDoubleSpinBox* field : editor.findChildren<QDoubleSpinBox*>())
        if (field->property("measurementKind").toString() == QStringLiteral("height"))
            height_field = field;
    auto* editor_view = editor.findChild<dolphin::ui::ContactSnapshotView*>();
    QCheckBox* not_measurable = nullptr;
    for (QCheckBox* box : editor.findChildren<QCheckBox*>())
        if (box->text().contains(QStringLiteral("Not measurable")))
            not_measurable = box;
    check(height_label && height_field && editor_view,
          "editor exposes the existing height row and measurement canvas");
    if (height_label && height_field && editor_view) {
        check(not_measurable != nullptr,
              "editor exposes the height not-measurable state");
        if (not_measurable) not_measurable->setChecked(true);
        QMouseEvent select_height(QEvent::MouseButtonPress, QPointF(3, 3),
                                  QPointF(3, 3), QPointF(3, 3),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(height_label, &select_height);
        QApplication::processEvents();
        check(!not_measurable || (!not_measurable->isChecked() && height_field->isEnabled()),
              "choosing Height clears the contradictory not-measurable state");
        check(editor_view->cursor().shape() == Qt::CrossCursor,
              "clicking the existing Height label arms drawing");
        check(height_field->property("measurementActive").toBool(),
              "the armed existing Height field has visible active state");
        const QPointF centre(editor_view->width() / 2.0, editor_view->height() / 2.0);
        drag(*editor_view, centre - QPointF(0, 30), centre + QPointF(50, 30));
        check(height_field->value() > 0.0,
              "a canvas drag writes the result into the existing Height field");
    }
    editor.close();

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
