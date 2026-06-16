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

TEST_F(TimingTest, MillisStartsSeededToPreventUnderflow) {
    auto m = millis();
    // The runtime deliberately seeds millis() ~1 hour (3,600,000 ms) in the
    // past so ESP32 staleness patterns like `millis() - 300000` don't break
    // on 64-bit Linux (where `long` underflow zero-extends instead of
    // wrapping like 32-bit ESP32). See the kMillisOffset rationale in
    // sim_runtime.cpp. So at startup millis() must already be >= 1 hour.
    //
    // Lower bound: catches the seed being absent / g_start not initialised
    // (which would return ~0). Upper bound: 1h plus a generous slack for
    // SDL_Init (~100ms) and CI scheduling jitter — catches a wrong offset.
    constexpr uint32_t kOneHourMs = 60u * 60u * 1000u;       // 3,600,000
    EXPECT_GE(m, kOneHourMs);
    EXPECT_LT(m, kOneHourMs + 60u * 1000u);                  // within a minute of the seed
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
