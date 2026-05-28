#include <gtest/gtest.h>
#include "SPIFFS.h"
#include "LittleFS.h"
#include "FS.h"
#include <filesystem>
#include <cstdlib>
namespace std_fs = std::filesystem;

class FsTest : public ::testing::Test {
protected:
    std_fs::path assets_;
    void SetUp() override {
        assets_ = std_fs::temp_directory_path() / "bg-fs-test";
        std_fs::remove_all(assets_);
        std_fs::create_directories(assets_);
        setenv("BOARDGHOST_ASSETS_DIR", assets_.c_str(), 1);
    }
    void TearDown() override {
        std_fs::remove_all(assets_);
    }
};

TEST_F(FsTest, BeginCreatesMountRoot) {
    ASSERT_TRUE(SPIFFS.begin());
    EXPECT_TRUE(std_fs::exists(assets_ / "spiffs"));
}

TEST_F(FsTest, WriteThenReadFile) {
    ASSERT_TRUE(SPIFFS.begin());
    {
        auto f = SPIFFS.open("/hello.txt", FILE_WRITE);
        ASSERT_TRUE(f);
        const char* msg = "hello";
        f.write(reinterpret_cast<const uint8_t*>(msg), 5);
    }

    auto f = SPIFFS.open("/hello.txt", FILE_READ);
    ASSERT_TRUE(f);
    uint8_t buf[16] = {};
    size_t n = f.read(buf, sizeof(buf));
    EXPECT_EQ(n, 5u);
    EXPECT_EQ(std::string((char*)buf, 5), "hello");
}

TEST_F(FsTest, ExistsReturnsFalseForMissing) {
    ASSERT_TRUE(SPIFFS.begin());
    EXPECT_FALSE(SPIFFS.exists("/no-such-file"));
}

TEST_F(FsTest, SpiffsAndLittleFsAreIsolated) {
    ASSERT_TRUE(SPIFFS.begin());
    ASSERT_TRUE(LittleFS.begin());
    {
        auto f = SPIFFS.open("/x.bin", FILE_WRITE);
        ASSERT_TRUE(f);
        uint8_t b = 0xAA;
        f.write(&b, 1);
    }
    // LittleFS shouldn't see SPIFFS's file.
    EXPECT_FALSE(LittleFS.exists("/x.bin"));
    EXPECT_TRUE (SPIFFS.exists("/x.bin"));
}
