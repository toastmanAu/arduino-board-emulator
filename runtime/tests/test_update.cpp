// Sim OTA Update — write small "firmware" buffers via the Update API and
// confirm the bytes actually land in BOARDGHOST_OTA_PATH on disk. Also pins
// the size + MD5 verification branches that gate real sketches' "OK"/"FAIL"
// reply on /update.

#include <gtest/gtest.h>
#include "Update.h"
#include "WiFiClient.h"   // for Stream-derived ByteStream in writeStream test

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;

namespace {

class UpdateTest : public ::testing::Test {
protected:
    fs::path ota_path;

    void SetUp() override {
        // Unique per-test path so concurrent test runs don't collide.
        std::random_device rd;
        std::mt19937 rng(rd());
        ota_path = fs::temp_directory_path() /
            ("bgh-ota-" + std::to_string(std::uniform_int_distribution<int>(10000, 99999)(rng)) + ".bin");
        setenv("BOARDGHOST_OTA_PATH", ota_path.c_str(), 1);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove(ota_path, ec);
        unsetenv("BOARDGHOST_OTA_PATH");
    }

    std::string read_file() {
        std::ifstream f(ota_path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    }
};

// Tiny in-memory Stream so writeStream has a real Stream subclass to read
// from. Mirrors what HTTPClient::getStreamPtr() would feed in a real OTA.
class ByteStream : public Stream {
public:
    explicit ByteStream(std::string data) : data_(std::move(data)) {}
    int  available() override { return (int)(data_.size() - pos_); }
    int  read() override { return pos_ < data_.size() ? (uint8_t)data_[pos_++] : -1; }
    int  read(uint8_t* buf, size_t n) override {
        size_t to_copy = std::min(n, data_.size() - pos_);
        if (to_copy == 0) return 0;
        std::memcpy(buf, data_.data() + pos_, to_copy);
        pos_ += to_copy;
        return (int)to_copy;
    }
    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t n) override { return n; }
    void   flush() override {}
private:
    std::string data_;
    size_t      pos_ = 0;
};

}  // namespace

TEST_F(UpdateTest, BeginWriteEndRoundtrip) {
    Update.abort();  // reset any state from a prior test
    std::string payload = "\x55\xAA\xDE\xADfirmware-bytes-here\x00\x42";
    ASSERT_TRUE(Update.begin(payload.size()));
    EXPECT_EQ(Update.size(), payload.size());

    EXPECT_EQ(Update.write((uint8_t*)payload.data(), payload.size()), payload.size());
    EXPECT_EQ(Update.progress(), payload.size());
    EXPECT_EQ(Update.remaining(), 0u);

    EXPECT_TRUE(Update.end());
    EXPECT_FALSE(Update.hasError());

    // On-disk bytes must match what we wrote exactly.
    EXPECT_EQ(read_file(), payload);
}

TEST_F(UpdateTest, EndFailsOnSizeMismatchWithoutForce) {
    Update.abort();
    ASSERT_TRUE(Update.begin(100));   // declare 100-byte firmware...
    uint8_t partial[40] = {0};
    EXPECT_EQ(Update.write(partial, sizeof(partial)), 40u);
    // ...but only write 40 → end() must fail unless forced.
    EXPECT_FALSE(Update.end(false));
    EXPECT_TRUE(Update.hasError());
    EXPECT_NE(std::string(Update.errorString()).find("size mismatch"), std::string::npos);
}

TEST_F(UpdateTest, EndSucceedsOnSizeMismatchWhenForced) {
    Update.abort();
    ASSERT_TRUE(Update.begin(100));
    uint8_t partial[40] = {0};
    EXPECT_EQ(Update.write(partial, sizeof(partial)), 40u);
    EXPECT_TRUE(Update.end(true));   // evenIfRemaining=true tolerates short
    EXPECT_FALSE(Update.hasError());
}

TEST_F(UpdateTest, Md5VerificationCatchesCorruption) {
    Update.abort();
    // "hello" → md5 = 5d41402abc4b2a76b9719d911017c592
    ASSERT_TRUE(Update.begin(5));
    Update.setMD5("5d41402abc4b2a76b9719d911017c592");
    uint8_t hello[] = {'h','e','l','l','o'};
    EXPECT_EQ(Update.write(hello, 5), 5u);
    EXPECT_TRUE(Update.end());
    EXPECT_EQ(std::string(Update.md5String().c_str()),
              "5d41402abc4b2a76b9719d911017c592");

    // Now corrupt: same length, different bytes → end() should fail.
    Update.abort();
    ASSERT_TRUE(Update.begin(5));
    Update.setMD5("5d41402abc4b2a76b9719d911017c592");
    uint8_t wrong[] = {'h','e','l','l','x'};
    EXPECT_EQ(Update.write(wrong, 5), 5u);
    EXPECT_FALSE(Update.end());
    EXPECT_TRUE(Update.hasError());
    EXPECT_NE(std::string(Update.errorString()).find("MD5 mismatch"), std::string::npos);
}

TEST_F(UpdateTest, WriteStreamReadsFullStream) {
    Update.abort();
    std::string fw = "stream-firmware-" + std::string(2000, 'X');
    ByteStream stream(fw);

    ASSERT_TRUE(Update.begin(fw.size()));
    size_t total = Update.writeStream(stream);
    EXPECT_EQ(total, fw.size());
    EXPECT_TRUE(Update.end());
    EXPECT_EQ(read_file(), fw);
}

TEST_F(UpdateTest, AbortDeletesPartialFile) {
    Update.abort();
    ASSERT_TRUE(Update.begin(50));
    uint8_t partial[20] = {0};
    EXPECT_EQ(Update.write(partial, sizeof(partial)), 20u);
    EXPECT_TRUE(fs::exists(ota_path));
    Update.abort();
    EXPECT_FALSE(fs::exists(ota_path));
    EXPECT_TRUE(Update.hasError());
}

TEST_F(UpdateTest, BeginRejectsReentry) {
    Update.abort();
    ASSERT_TRUE(Update.begin(100));
    // Re-entrant begin should fail with a clear error.
    EXPECT_FALSE(Update.begin(50));
    EXPECT_TRUE(Update.hasError());
    Update.abort();  // cleanup
}
