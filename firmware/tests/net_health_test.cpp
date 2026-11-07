#include <doctest.h>

#include <chan/core/net_health.h>

using namespace chan;

TEST_CASE("fetch health goes down after three consecutive failures")
{
    FetchHealth h;
    CHECK_FALSE(h.down());
    h.record(false);
    h.record(false);
    CHECK_FALSE(h.down());
    h.record(false);
    CHECK(h.down());
    CHECK(h.consecutive_failures() == 3);
}

TEST_CASE("one success clears the streak and the down state")
{
    FetchHealth h;
    for (int i = 0; i < 5; ++i) h.record(false);
    CHECK(h.down());
    h.record(true);
    CHECK_FALSE(h.down());
    CHECK(h.consecutive_failures() == 0);
    h.record(false);
    h.record(false);
    CHECK_FALSE(h.down());  // the streak restarted from zero
}

TEST_CASE("threshold is configurable and the counter does not overflow")
{
    FetchHealth h(1);
    h.record(false);
    CHECK(h.down());
    FetchHealth big(3);
    for (int i = 0; i < 100000; ++i) big.record(false);
    CHECK(big.down());
    CHECK(big.consecutive_failures() >= 3);
}
