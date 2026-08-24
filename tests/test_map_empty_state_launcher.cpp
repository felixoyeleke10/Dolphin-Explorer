#include "ui/features/map/MapEmptyStateLauncher.h"

#include <QApplication>
#include <QPushButton>

#include <iostream>

using namespace dolphin;

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    int failed = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failed; }
    };

    ui::MapEmptyStateLauncher launcher;
    int imports = 0;
    int new_projects = 0;
    QString opened;
    QObject::connect(&launcher, &ui::MapEmptyStateLauncher::importFilesRequested,
                     [&]() { ++imports; });
    QObject::connect(&launcher, &ui::MapEmptyStateLauncher::newProjectRequested,
                     [&]() { ++new_projects; });
    QObject::connect(&launcher, &ui::MapEmptyStateLauncher::openProjectRequested,
                     [&](const QString& path) { opened = path; });

    auto* import_button = launcher.findChild<QPushButton*>("mapImportHintBtn");
    auto* new_button = launcher.findChild<QPushButton*>("launcherNewBtn");
    check(import_button && new_button, "launcher actions are present");
    if (import_button) import_button->click();
    if (new_button) new_button->click();
    check(imports == 1 && new_projects == 1, "launcher action signals are emitted");

    launcher.setRecentProjects(
        {"One", "Two", "Three", "Four", "Five", "Six"},
        {"one.dlp", "two.dlp", "three.dlp", "four.dlp", "five.dlp", "six.dlp"});
    const auto recent = launcher.findChildren<QPushButton*>("mapRecentBtn");
    check(recent.size() == 5, "recent projects are capped at five");
    if (!recent.empty()) recent.front()->click();
    check(opened == "one.dlp", "recent row emits its project path");

    launcher.setRecentProjects({}, {});
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(launcher.findChildren<QPushButton*>("mapRecentBtn").empty(),
          "clearing recent projects removes all rows");

    if (failed == 0) std::cout << "test_map_empty_state_launcher: ALL PASS\n";
    return failed == 0 ? 0 : 1;
}
