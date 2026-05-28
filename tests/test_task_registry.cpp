// test_task_registry.cpp — unit tests for TaskRegistry.
// CancellationToken tests live in test_cancellation_token.cpp.
#include "app/tasks/CancellationToken.h"
#include "app/tasks/TaskRegistry.h"
#include <cassert>
#include <string>

using dolphin::app::CancellationToken;
using dolphin::app::TaskRegistry;

static void test_registry_empty()
{
    TaskRegistry reg;
    assert(reg.activeCount() == 0);
    assert(reg.activeNames().empty());
}

static void test_registry_register_and_complete()
{
    TaskRegistry reg;
    CancellationToken t;
    reg.registerTask("load:layer1", t);
    assert(reg.activeCount() == 1);
    assert(!reg.activeNames().empty());

    reg.completeTask("load:layer1");
    assert(reg.activeCount() == 0);
}

static void test_registry_cancel_all()
{
    TaskRegistry reg;
    CancellationToken t1, t2;
    CancellationToken c1 = t1, c2 = t2;   // held by "background thread"
    reg.registerTask("load:a", t1);
    reg.registerTask("proc:a", t2);
    assert(reg.activeCount() == 2);

    reg.cancelAll();
    assert(reg.activeCount() == 0);
    assert(c1.isCancelled());
    assert(c2.isCancelled());
}

static void test_registry_overwrite()
{
    TaskRegistry reg;
    CancellationToken t1;
    reg.registerTask("load:x", t1);
    CancellationToken copy1 = t1;

    CancellationToken t2;
    reg.registerTask("load:x", t2);   // replaces t1
    assert(reg.activeCount() == 1);

    reg.cancelAll();
    assert(!copy1.isCancelled());  // t1 was replaced, not cancelled through registry
    assert(t2.isCancelled());      // t2 was the active entry, now cancelled
}

int main()
{
    test_registry_empty();
    test_registry_register_and_complete();
    test_registry_cancel_all();
    test_registry_overwrite();
    return 0;
}
