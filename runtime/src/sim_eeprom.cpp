#include "EEPROM.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

EEPROMClass EEPROM;

namespace {
fs::path eeprom_path() {
    // Anchor under the project's .boardghost dir if available, else /tmp.
    const char* override_path = std::getenv("BOARDGHOST_EEPROM_PATH");
    if (override_path && *override_path) return fs::path(override_path);
    // The CWD when the sketch binary runs is its build dir under
    // <project>/.boardghost/<board>/build, so step up two levels.
    auto cwd = fs::current_path();
    auto candidate = cwd.parent_path().parent_path() / "eeprom.bin";
    return candidate;
}
}  // namespace

bool EEPROMClass::begin(size_t size) {
    if (data_) std::free(data_);
    size_ = size;
    data_ = static_cast<uint8_t*>(std::calloc(size_, 1));
    if (!data_) return false;

    // Load from disk if present.
    auto path = eeprom_path();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f) {
        std::fread(data_, 1, size_, f);
        std::fclose(f);
    }
    return true;
}

void EEPROMClass::end() {
    commit();
    if (data_) { std::free(data_); data_ = nullptr; size_ = 0; }
}

uint8_t EEPROMClass::read(int addr) {
    if (!data_ || addr < 0 || (size_t)addr >= size_) return 0;
    return data_[addr];
}

void EEPROMClass::write(int addr, uint8_t val) {
    if (!data_ || addr < 0 || (size_t)addr >= size_) return;
    data_[addr] = val;
}

bool EEPROMClass::commit() {
    if (!data_) return false;
    auto path = eeprom_path();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t written = std::fwrite(data_, 1, size_, f);
    std::fclose(f);
    return written == size_;
}

int EEPROMClass::readInt(int addr) {
    if (!data_ || addr < 0 || (size_t)(addr + sizeof(int)) > size_) return 0;
    int v;
    std::memcpy(&v, data_ + addr, sizeof(int));
    return v;
}

void EEPROMClass::writeInt(int addr, int value) {
    if (!data_ || addr < 0 || (size_t)(addr + sizeof(int)) > size_) return;
    std::memcpy(data_ + addr, &value, sizeof(int));
}
