#include "ui/features/contacts/ContactReport.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

static void check(bool condition, const char* expression, const char* file, int line)
{
    if (condition) {
        ++g_pass;
    } else {
        ++g_fail;
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expression);
    }
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid());

    dolphin::core::Contact contact;
    contact.label = "Target, \"Alpha\"";
    contact.lat = 48.123456789;
    contact.lon = -52.987654321;
    contact.depth_m = 10.25f;
    contact.range_m = 42.5f;
    contact.width_m = 3.0f;
    contact.height_m = 1.5f;
    contact.classification = "Debris";
    contact.confidence = dolphin::core::Confidence::Certain;
    contact.line_id = "line-7";
    contact.notes = "first line\nsecond line";
    const std::vector<dolphin::core::Contact> contacts{contact};

    const QString path = dir.filePath(QStringLiteral("contacts.csv"));
    CHECK(dolphin::ui::ContactReport::writeCsv(
        path, QStringLiteral("Contacts"), contacts, nullptr));

    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    CHECK(bytes.startsWith(QByteArrayLiteral("\xEF\xBB\xBF")));
    const QString csv = QString::fromUtf8(bytes.mid(3));
    CHECK(csv.startsWith(QStringLiteral(
        "Label,Latitude,Longitude,Depth_m,Range_m,Width_m,Height_m,"
        "Classification,Confidence,Line,Notes\n")));
    CHECK(csv.contains(QStringLiteral("\"Target, \"\"Alpha\"\"\"")));
    CHECK(csv.contains(QStringLiteral("48.12345679,-52.98765432")));
    CHECK(csv.contains(QStringLiteral(",10.25,42.50,3.00,1.50,")));
    CHECK(csv.contains(QStringLiteral(",Debris,Certain,line-7,")));
    CHECK(csv.contains(QStringLiteral("\"first line\nsecond line\"")));

    // QSaveFile must preserve an existing file if the candidate cannot open.
    const QString missing_parent = dir.filePath(QStringLiteral("missing/contacts.csv"));
    CHECK(!dolphin::ui::ContactReport::writeCsv(
        missing_parent, QStringLiteral("Contacts"), contacts, nullptr));
    CHECK(!QFile::exists(missing_parent));

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
