#include <gtest/gtest.h>
#include <SPI.h>
#include <Wire.h>
#include "sim_runtime.h"

// sim_log writes to stderr; we capture it via dup2.
#include <unistd.h>
#include <fcntl.h>

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

TEST(SPIStub, BeginAndTransferLog) {
    CaptureStderr cap;
    SPI.begin();
    SPI.transfer(0xAB);
    auto log = cap.read_all();
    EXPECT_NE(log.find("SPI.begin"), std::string::npos);
    EXPECT_NE(log.find("SPI.transfer 0xAB"), std::string::npos);
}

TEST(WireStub, BeginTransmissionAndWriteLog) {
    CaptureStderr cap;
    Wire.begin();
    Wire.beginTransmission(0x3C);
    Wire.write(0xFE);
    Wire.endTransmission();
    auto log = cap.read_all();
    EXPECT_NE(log.find("Wire.begin"), std::string::npos);
    EXPECT_NE(log.find("Wire.beginTransmission 0x3C"), std::string::npos);
    EXPECT_NE(log.find("Wire.write 0xFE"), std::string::npos);
}
