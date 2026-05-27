#include <gtest/gtest.h>
#include <Arduino.h>
#include "sim_runtime.h"
#include <cstdlib>

class PinStateTest : public ::testing::Test {
protected:
    void SetUp() override { sim_runtime_init(0, nullptr); }
    void TearDown() override { sim_runtime_shutdown(); }
};

TEST_F(PinStateTest, DigitalWriteReadRoundtrip) {
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    EXPECT_EQ(digitalRead(5), HIGH);
    digitalWrite(5, LOW);
    EXPECT_EQ(digitalRead(5), LOW);
}

TEST_F(PinStateTest, DigitalReadDefaultsLow) {
    pinMode(7, INPUT);
    EXPECT_EQ(digitalRead(7), LOW);
}

TEST_F(PinStateTest, AnalogReadDefaultsZero) {
    EXPECT_EQ(analogRead(34), 0);
}

TEST_F(PinStateTest, AnalogReadHonoursEnvOverride) {
    setenv("BOARDGHOST_ANALOG_34", "1234", 1);
    // Re-init so the env var is re-read.
    sim_runtime_shutdown();
    sim_runtime_init(0, nullptr);
    EXPECT_EQ(analogRead(34), 1234);
    unsetenv("BOARDGHOST_ANALOG_34");
}

TEST_F(PinStateTest, AnalogWriteStoresValue) {
    analogWrite(9, 127);
    // No public read, but we can verify via analogRead on the same pin
    // (sim treats analogRead as PWM duty mirror when no env override).
    EXPECT_EQ(analogRead(9), 127);
}

TEST_F(PinStateTest, OutOfRangePinReturnsLowSafely) {
    EXPECT_EQ(digitalRead(999), LOW);
    EXPECT_EQ(analogRead(999), 0);
}
