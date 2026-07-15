#include "app/project/Project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <string>

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    if (condition) {
        ++g_pass;
        return;
    }

    ++g_fail;
    std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expression);
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

void testCreatesProjectDataDirectory()
{
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid()) return;

    const QString project_dir = temporary.filePath("survey");
    const std::string manifest =
        QDir(project_dir).filePath("survey.dlp").toStdString();

    const auto project = dolphin::app::Project::create("survey", manifest);
    CHECK(project != nullptr);
    CHECK(QDir(QDir(project_dir).filePath("data")).exists());
}

void testRefusesExistingManifestWithoutChangingIt()
{
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid()) return;

    const QString manifest = temporary.filePath("existing.dlp");
    QFile file(manifest);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write("existing project") == 16);
    file.close();

    const auto project = dolphin::app::Project::create(
        "replacement", manifest.toStdString());
    CHECK(project == nullptr);

    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == QByteArray("existing project"));
}

void testRefusesUncreatableProjectDirectory()
{
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid()) return;

    const QString blocker = temporary.filePath("not_a_directory");
    QFile file(blocker);
    CHECK(file.open(QIODevice::WriteOnly));
    file.close();

    const QString manifest = QDir(blocker).filePath("survey.dlp");
    const auto project = dolphin::app::Project::create(
        "survey", manifest.toStdString());
    CHECK(project == nullptr);
}

void testRefusesDataPathOccupiedByFile()
{
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    if (!temporary.isValid()) return;

    const QString project_dir = temporary.filePath("survey");
    CHECK(QDir().mkpath(project_dir));

    QFile data_file(QDir(project_dir).filePath("data"));
    CHECK(data_file.open(QIODevice::WriteOnly));
    data_file.close();

    const QString manifest = QDir(project_dir).filePath("survey.dlp");
    const auto project = dolphin::app::Project::create(
        "survey", manifest.toStdString());
    CHECK(project == nullptr);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testCreatesProjectDataDirectory();
    testRefusesExistingManifestWithoutChangingIt();
    testRefusesUncreatableProjectDirectory();
    testRefusesDataPathOccupiedByFile();

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
