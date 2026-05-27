#include <gtest/gtest.h>
#include <Arduino.h>
#include "sim_runtime.h"
#include <thread>
#include <chrono>

class TimingTest : public ::testing::Test {
protected:
    void SetUp() override { sim_runtime_init(0, nullptr); }
    void TearDown() override { sim_runtime_shutdown(); }
};

TEST_F(TimingTest, MillisStartsNearZero) {
    auto m = millis();
    // SDL_Init (called in sim_runtime_init since Task 9) can take ~100ms
    // on some hosts. 500ms is a generous bound that still catches gross bugs
    // (e.g., g_start not being set in init).
    EXPECT_LT(m, 500u);
}

TEST_F(TimingTest, MillisAdvancesMonotonically) {
    auto a = millis();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto b = millis();
    EXPECT_GT(b, a);
    EXPECT_GE(b - a, 15u);
}

TEST_F(TimingTest, MicrosAdvancesFasterThanMillis) {
    auto m1 = micros();
    std::this_thread::sleep_for(std::chrono::microseconds(500));
    auto m2 = micros();
    EXPECT_GE(m2 - m1, 250u);
}

TEST_F(TimingTest, DelayBlocksRoughly) {
    auto start = millis();
    delay(30);
    auto elapsed = millis() - start;
    EXPECT_GE(elapsed, 28u);
    EXPECT_LT(elapsed, 100u);  // generous upper bound for CI noise
}
