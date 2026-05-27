#include <gtest/gtest.h>
#include "sim_runtime.h"

TEST(Smoke, RuntimeInitDoesNotCrash) {
    sim_runtime_init(0, nullptr);
    EXPECT_EQ(sim_should_quit(), 0);
    sim_runtime_shutdown();
}
