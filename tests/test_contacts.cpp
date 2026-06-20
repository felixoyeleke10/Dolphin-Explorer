// Regression tests for the contact model changes:
//   - monotonic, collision-free default labels (Cnnn from the contact id)
//   - explicit labels preserved (rename / paste)
//   - recycle bin: recycle / restore / purge / empty
//   - serialization round-trip of active + recycled contacts
//   - next-id continues after reload so labels never collide post-open
//
// Minimal CHECK harness (no external framework). Entry: ctest --output-on-failure
#include "app/project/Project.h"
#include "core/Contact.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* expr, const char* file, int line)
{
    if (cond) ++g_pass;
    else { ++g_fail; std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expr); }
}
#define CHECK(x) check((x), #x, __FILE__, __LINE__)

using dolphin::app::Project;
using dolphin::core::Contact;

// Add a contact (empty label → project assigns) and return its assigned id.
static uint64_t add(Project& p, const std::string& label = {})
{
    Contact c;
    c.label = label;
    c.lat = 10.0; c.lon = 20.0;
    p.addContact(c);
    return p.contacts().empty() ? 0 : p.contacts().back().id;
}

static const Contact* find(const std::vector<Contact>& v, uint64_t id)
{
    for (const auto& c : v) if (c.id == id) return &c;
    return nullptr;
}

// 1. Default labels are monotonic and never reused after a removal.
static void testMonotonicLabels()
{
    QTemporaryDir tmp; CHECK(tmp.isValid()); if (!tmp.isValid()) return;
    auto p = Project::create("P", (tmp.path() + "/P.dlp").toStdString());

    const uint64_t a = add(*p);
    const uint64_t b = add(*p);
    const uint64_t c = add(*p);
    CHECK(p->contacts().size() == 3);
    CHECK(find(p->contacts(), a)->label == "C001");
    CHECK(find(p->contacts(), b)->label == "C002");
    CHECK(find(p->contacts(), c)->label == "C003");

    // Remove the middle one, then add another — the new label must NOT collide.
    p->recycleContact(b);
    const uint64_t d = add(*p);
    CHECK(find(p->contacts(), d)->label == "C004");   // id-based, never "C002"/"C003"

    std::set<std::string> labels;
    for (const auto& ct : p->contacts())          labels.insert(ct.label);
    for (const auto& ct : p->recycledContacts())  labels.insert(ct.label);
    CHECK(labels.size() == 4);   // C001..C004 all distinct, no reuse
}

// 2. An explicit label (rename/paste) is preserved, not overwritten.
static void testExplicitLabelPreserved()
{
    QTemporaryDir tmp; CHECK(tmp.isValid()); if (!tmp.isValid()) return;
    auto p = Project::create("P", (tmp.path() + "/P.dlp").toStdString());
    const uint64_t id = add(*p, "Wreck near pier");
    CHECK(find(p->contacts(), id)->label == "Wreck near pier");
}

// 3. Recycle bin: recycle / restore / purge / empty.
static void testRecycleBin()
{
    QTemporaryDir tmp; CHECK(tmp.isValid()); if (!tmp.isValid()) return;
    auto p = Project::create("P", (tmp.path() + "/P.dlp").toStdString());
    const uint64_t a = add(*p), b = add(*p);

    p->recycleContact(a);
    CHECK(p->contacts().size() == 1);
    CHECK(p->recycledContacts().size() == 1);
    CHECK(find(p->recycledContacts(), a) != nullptr);

    p->restoreContact(a);
    CHECK(p->contacts().size() == 2);
    CHECK(p->recycledContacts().empty());

    p->recycleContact(a);
    p->purgeContact(a);                  // permanent
    CHECK(p->recycledContacts().empty());
    CHECK(find(p->contacts(), a) == nullptr);
    CHECK(find(p->contacts(), b) != nullptr);

    p->recycleContact(b);
    p->emptyRecycleBin();
    CHECK(p->recycledContacts().empty());
    CHECK(p->contacts().empty());
}

// 4. Round-trip active + recycled contacts; next id continues so no collision.
static void testSerializationRoundTrip()
{
    QTemporaryDir tmp; CHECK(tmp.isValid()); if (!tmp.isValid()) return;
    const std::string manifest = (tmp.path() + "/P.dlp").toStdString();

    uint64_t a = 0, b = 0, c = 0;
    {
        auto p = Project::create("P", manifest);
        a = add(*p);            // C001
        b = add(*p);            // C002
        c = add(*p);            // C003
        p->recycleContact(b);   // b → recycle bin
        CHECK(p->save());
    }

    auto p = Project::open(manifest);
    CHECK(p != nullptr); if (!p) return;
    CHECK(p->contacts().size() == 2);          // a, c
    CHECK(p->recycledContacts().size() == 1);  // b
    CHECK(find(p->contacts(), a) != nullptr);
    CHECK(find(p->contacts(), c) != nullptr);
    CHECK(find(p->recycledContacts(), b) != nullptr);
    CHECK(find(p->recycledContacts(), b)->label == "C002");

    // The next contact added after reload must not reuse any prior id/label.
    const uint64_t d = add(*p);
    CHECK(d > std::max({a, b, c}));
    std::set<std::string> labels;
    for (const auto& ct : p->contacts())         labels.insert(ct.label);
    for (const auto& ct : p->recycledContacts()) labels.insert(ct.label);
    CHECK(labels.count(find(p->contacts(), d)->label) == 1);
    CHECK(labels.size() == 4);   // C001, C002(recycled), C003, C00x — all distinct
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    testMonotonicLabels();
    testExplicitLabelPreserved();
    testRecycleBin();
    testSerializationRoundTrip();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
