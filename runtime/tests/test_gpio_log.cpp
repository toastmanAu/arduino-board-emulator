#include <gtest/gtest.h>
#include <Arduino.h>
#include "sim_runtime.h"
#include <unistd.h>
#include <fcntl.h>

// Mirrors the CaptureStderr helper from test_bus_logging.cpp.
class CaptureStderr {
public:
    CaptureStderr() {
        old_ = dup(STDERR_FILENO);
        ::pipe(fds_);
        dup2(fds_[1], STDERR_FILENO);
        close(fds_[1]);
        int flags = fcntl(fds_[0], F_GETFL, 0);
        fcntl(fds_[0], F_SETFL, flags | O_NONBLOCK);
    }
    ~CaptureStderr() {
        fflush(stderr);
        dup2(old_, STDERR_FILENO);
        close(old_);
        close(fds_[0]);
    }
    std::string read_all() {
        fflush(stderr);
        std::string out;
        char buf[256];
        ssize_t n;
        while ((n = ::read(fds_[0], buf, sizeof(buf))) > 0) out.append(buf, n);
        return out;
    }
private:
    int old_;
    int fds_[2];
};

TEST(GpioLog, PinModeAndDigitalWriteEmit) {
    CaptureStderr cap;
    sim_runtime_init(0, nullptr);
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    digitalWrite(5, LOW);
    auto log = cap.read_all();
    sim_runtime_shutdown();

    EXPECT_NE(log.find("[gpio] mode 5 OUTPUT"), std::string::npos) << log;
    EXPECT_NE(log.find("[gpio] write 5 1"),    std::string::npos) << log;
    EXPECT_NE(log.find("[gpio] write 5 0"),    std::string::npos) << log;
}

TEST(GpioLog, AnalogWriteEmits) {
    CaptureStderr cap;
    sim_runtime_init(0, nullptr);
    analogWrite(9, 127);
    auto log = cap.read_all();
    sim_runtime_shutdown();
    EXPECT_NE(log.find("[gpio] pwm 9 127"), std::string::npos) << log;
}
