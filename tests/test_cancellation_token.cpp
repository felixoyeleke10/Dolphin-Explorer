// test_cancellation_token.cpp — unit tests for CancellationToken.
#include "app/tasks/CancellationToken.h"
#include <cassert>
#include <thread>

using dolphin::app::CancellationToken;

static void test_default_not_cancelled()
{
    CancellationToken t;
    assert(!t.isCancelled());
}

static void test_cancel_sets_flag()
{
    CancellationToken t;
    t.cancel();
    assert(t.isCancelled());
}

static void test_copy_shares_flag()
{
    CancellationToken t;
    CancellationToken copy = t;   // shares underlying flag
    t.cancel();
    assert(copy.isCancelled());   // cancel on t is visible through copy
}

static void test_reset_creates_new_flag()
{
    CancellationToken t;
    CancellationToken old = t;    // holds the original flag
    t.cancel();
    t.reset();                    // t now points to a new, un-cancelled flag
    assert(!t.isCancelled());     // new flag is clean
    assert(old.isCancelled());    // original flag stays cancelled
}

static void test_cross_thread_cancel()
{
    CancellationToken t;
    CancellationToken shared = t;

    std::thread bg([shared]() mutable {
        // Background thread polls until cancelled.
        while (!shared.isCancelled()) { /* spin */ }
    });

    t.cancel();
    bg.join();  // must not hang
    assert(t.isCancelled());
}

int main()
{
    test_default_not_cancelled();
    test_cancel_sets_flag();
    test_copy_shares_flag();
    test_reset_creates_new_flag();
    test_cross_thread_cancel();
    return 0;
}
