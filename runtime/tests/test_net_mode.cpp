#include <gtest/gtest.h>
#include "sim_net.h"
#include <cstdlib>

// sim_net_mode caches on first read. To test all branches in one binary
// we'd need to expose a reset hook — but tests run in independent gtest
// processes via gtest_discover_tests, so the cache is fresh per process.
// We exercise only one mode per test process via TEST_F + setenv before
// any call.

TEST(NetMode, RealSupportedReturnsZeroOrOne) {
    int r = sim_net_real_supported();
    EXPECT_TRUE(r == 0 || r == 1);
}

TEST(NetMode, DefaultsToFake) {
    unsetenv("BOARDGHOST_NET");
    EXPECT_EQ(sim_net_mode(), BOARDGHOST_NET_FAKE);
}
