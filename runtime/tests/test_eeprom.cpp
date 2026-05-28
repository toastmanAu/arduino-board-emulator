#include <gtest/gtest.h>
#include "EEPROM.h"
#include <cstdlib>
#include <filesystem>
namespace fs = std::filesystem;

class EepromTest : public ::testing::Test {
protected:
    fs::path path_;
    void SetUp() override {
        path_ = fs::temp_directory_path() / "boardghost-eeprom-test.bin";
        if (fs::exists(path_)) fs::remove(path_);
        setenv("BOARDGHOST_EEPROM_PATH", path_.c_str(), 1);
    }
    void TearDown() override {
        if (fs::exists(path_)) fs::remove(path_);
    }
};

TEST_F(EepromTest, BeginInitsZeroes) {
    ASSERT_TRUE(EEPROM.begin(64));
    EXPECT_EQ(EEPROM.read(0), 0);
    EXPECT_EQ(EEPROM.read(63), 0);
    EEPROM.end();
}

TEST_F(EepromTest, WriteAndCommitPersists) {
    ASSERT_TRUE(EEPROM.begin(64));
    EEPROM.write(0, 42);
    EEPROM.write(1, 99);
    EEPROM.writeInt(4, 777);
    ASSERT_TRUE(EEPROM.commit());
    EEPROM.end();

    // Re-init and verify the values survived.
    ASSERT_TRUE(EEPROM.begin(64));
    EXPECT_EQ(EEPROM.read(0), 42);
    EXPECT_EQ(EEPROM.read(1), 99);
    EXPECT_EQ(EEPROM.readInt(4), 777);
    EEPROM.end();
}

TEST_F(EepromTest, OutOfRangeReadReturnsZero) {
    ASSERT_TRUE(EEPROM.begin(16));
    EXPECT_EQ(EEPROM.read(1000), 0);
    EXPECT_EQ(EEPROM.readInt(1000), 0);
    EEPROM.end();
}
