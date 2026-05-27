#include <gtest/gtest.h>
#include <Arduino.h>
#include <unistd.h>
#include <fcntl.h>

// Redirect stdout to a pipe so we can capture Serial.print output.
class CaptureStdout {
public:
    CaptureStdout() {
        old_ = dup(STDOUT_FILENO);
        ::pipe(fds_);
        dup2(fds_[1], STDOUT_FILENO);
        close(fds_[1]);
        int flags = fcntl(fds_[0], F_GETFL, 0);
        fcntl(fds_[0], F_SETFL, flags | O_NONBLOCK);
    }
    ~CaptureStdout() {
        fflush(stdout);
        dup2(old_, STDOUT_FILENO);
        close(old_);
        close(fds_[0]);
    }
    std::string read_all() {
        fflush(stdout);
        std::string out;
        char buf[256];
        ssize_t n;
        while ((n = ::read(fds_[0], buf, sizeof(buf))) > 0) {
            out.append(buf, n);
        }
        return out;
    }
private:
    int old_;
    int fds_[2];
};

TEST(SerialTest, PrintAndPrintln) {
    CaptureStdout cap;
    Serial.print("hello ");
    Serial.println("world");
    auto out = cap.read_all();
    EXPECT_EQ(out, "hello world\n");
}

TEST(SerialTest, PrintInt) {
    CaptureStdout cap;
    Serial.println(42);
    EXPECT_EQ(cap.read_all(), "42\n");
}

TEST(SerialTest, PrintfFormats) {
    CaptureStdout cap;
    Serial.printf("v=%d s=%s\n", 7, "ok");
    EXPECT_EQ(cap.read_all(), "v=7 s=ok\n");
}

TEST(SerialTest, BeginIsNoOp) {
    Serial.begin(115200);
    SUCCEED();
}
