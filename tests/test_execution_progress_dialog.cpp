#include "ui/features/import/ImportProgressDialog.h"

#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>

#include <cstdio>

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    ++failures; } } while (false)

QLabel* label(dolphin::ui::ExecutionProgressDialog& dialog, const char* name)
{
    return dialog.findChild<QLabel*>(QString::fromLatin1(name));
}

QPushButton* button(dolphin::ui::ExecutionProgressDialog& dialog, const char* name)
{
    return dialog.findChild<QPushButton*>(QString::fromLatin1(name));
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // The shared surface follows the initiating viewer and its header is a real
    // drag handle while active. Rehosting must preserve visibility/state.
    {
        QWidget main_host;
        QWidget waterfall_host;
        main_host.resize(900, 700);
        waterfall_host.resize(1000, 760);
        main_host.show();
        waterfall_host.show();

        dolphin::ui::ExecutionProgressDialog owned;
        owned.attachTo(&main_host);
        owned.addJob("owner", "Owner", "RUN", 0.f);
        CHECK(owned.parentWidget() == &main_host);
        CHECK(owned.isVisible());

        owned.attachTo(&waterfall_host);
        CHECK(owned.parentWidget() == &waterfall_host);
        CHECK(owned.isVisible());

        auto* header = owned.findChild<QWidget*>("epdHeader");
        CHECK(header != nullptr);
        if (header) {
            const QPoint before = owned.pos();
            const QPoint press_global = header->mapToGlobal(QPoint(20, 12));
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(20, 12),
                              QPointF(press_global), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(header, &press);
            const QPoint move_global = press_global
                                     + QPoint(waterfall_host.width() + 250, -120);
            QMouseEvent move(QEvent::MouseMove, QPointF(100, -23),
                             QPointF(move_global), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(header, &move);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(100, -23),
                                QPointF(move_global), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(header, &release);
            CHECK(owned.pos() != before);
            const QRect owner_global(waterfall_host.mapToGlobal(QPoint(0, 0)),
                                     waterfall_host.size());
            CHECK(!owner_global.contains(owned.frameGeometry().center()));
        }

        bool hidden_reported = false;
        QObject::connect(&owned,
                         &dolphin::ui::ExecutionProgressDialog::hostHidden,
                         [&hidden_reported](QWidget*) { hidden_reported = true; });
        waterfall_host.hide();
        QApplication::processEvents();
        CHECK(hidden_reported);
    }

    dolphin::ui::ExecutionProgressDialog dialog;

    // A capped queue must not reach 100% or All Done while undispatched jobs remain.
    dialog.setQueueTotal(3);
    dialog.addJob("a", "A", "XTF", 0.f);
    dialog.addJob("b", "B", "XTF", 0.f);
    dialog.updateJob("a", 150);
    CHECK(dialog.findChild<QProgressBar*>("overallBar")->value() == 33);
    dialog.finishJob("a", "Done");
    dialog.finishJob("b", "Done");
    CHECK(label(dialog, "titleLabel")->text() != QStringLiteral("All Done"));
    CHECK(dialog.findChild<QProgressBar*>("overallBar")->value() == 66);
    CHECK(!button(dialog, "closeBtn")->isEnabled());

    dialog.addJob("c", "C", "XTF", 0.f);
    dialog.finishJob("c", "Done");
    CHECK(label(dialog, "titleLabel")->text() == QStringLiteral("All Done"));
    CHECK(button(dialog, "closeBtn")->isEnabled());

    // Extending an active queue adds to its existing total; a completed batch resets.
    dialog.addJob("extend-a", "Extend A", "XTF", 0.f);
    dialog.addToQueueTotal(2);
    dialog.finishJob("extend-a", "Done");
    CHECK(label(dialog, "titleLabel")->text() != QStringLiteral("All Done"));
    dialog.addJob("extend-b", "Extend B", "XTF", 0.f);
    dialog.addJob("extend-c", "Extend C", "XTF", 0.f);
    dialog.finishJob("extend-b", "Done");
    dialog.finishJob("extend-c", "Done");
    CHECK(label(dialog, "titleLabel")->text() == QStringLiteral("All Done"));

    // A direct job must not inherit the previous queue's total.
    dialog.addJob("direct", "Direct", "RUN", 0.f);
    dialog.finishJob("direct", "Done");
    CHECK(label(dialog, "titleLabel")->text() == QStringLiteral("All Done"));

    // A later map-only batch must discard old import rows and stage state.
    dialog.onMapLoadPending(9);
    CHECK(label(dialog, "titleLabel")->text() == QStringLiteral("Opening project"));
    CHECK(!label(dialog, "subtitleLabel")->text().contains(QStringLiteral("line(s)")));
    dialog.onMapLoadDone(9);

    // Completion identity prevents an old project's task from consuming a new one.
    dialog.resetState();
    CHECK(!dialog.hasDisplayableState());
    dialog.onMapLoadPending(10);
    dialog.resetState();
    dialog.onMapLoadPending(20);
    dialog.onMapLoadDone(10);
    CHECK(label(dialog, "subtitleLabel")->text().contains(QStringLiteral("0 of 1")));
    dialog.onMapLoadDone(20);
    CHECK(!dialog.isVisible());

    // A hidden active batch can always be reopened from the View menu hook.
    dialog.addJob("visible", "Visible", "XTF", 0.f);
    CHECK(dialog.hasDisplayableState());
    dialog.updateJob("visible", 64, QStringLiteral("Georeferencing... 64%"));
    bool task_progress_visible = false;
    for (auto* bar : dialog.findChildren<QProgressBar*>("fileBar"))
        task_progress_visible |= bar->isVisible() && bar->value() == 64;
    CHECK(task_progress_visible);
    bool phase_visible = false;
    for (auto* item : dialog.findChildren<QLabel*>("fileStatus"))
        phase_visible |= item->text().contains(QStringLiteral("Georeferencing"));
    CHECK(phase_visible);
    button(dialog, "bgBtn")->click();
    CHECK(!dialog.isVisible());
    dialog.reopen();
    CHECK(dialog.isVisible());

    dialog.resetState();
    dialog.onMapLoadPending(30, QStringLiteral("Survey Line 004"));
    dialog.onMapLoadProgress(76);
    CHECK(label(dialog, "subtitleLabel")->text().contains(QStringLiteral("Georeferencing")));
    CHECK(label(dialog, "subtitleLabel")->text().contains(QStringLiteral("Survey Line 004")));
    CHECK(dialog.findChild<QProgressBar*>("overallBar")->value() == 76);
    dialog.onMapLoadDone(30);

    dialog.addJob("op:42", "Building SBP profile", "RUN", 0.f, false);
    CHECK(dialog.hasDisplayableState());
    dialog.cancelJob("op:42");
    CHECK(label(dialog, "titleLabel")->text() == QStringLiteral("All Done"));
    CHECK(button(dialog, "closeBtn")->isEnabled());

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
