#include <cstdint>
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

// readString / writeString — ESP32 Arduino's null-terminated convention:
// bytes are written verbatim until a '\0' or the end of EEPROM. readString
// stops at the first '\0'.
String EEPROMClass::readString(int addr) {
    if (!data_ || addr < 0 || (size_t)addr >= size_) return String();
    char buf[256];
    size_t n = 0;
    while ((size_t)(addr + n) < size_ && n < sizeof(buf) - 1) {
        uint8_t c = data_[addr + n];
        if (c == 0) break;
        buf[n++] = static_cast<char>(c);
    }
    buf[n] = '\0';
    return String(buf);
}

size_t EEPROMClass::writeString(int addr, const String& val) {
    return writeString(addr, val.c_str());
}

size_t EEPROMClass::writeString(int addr, const char* val) {
    if (!data_ || addr < 0 || (size_t)addr >= size_ || !val) return 0;
    size_t i = 0;
    while (val[i] && (size_t)(addr + i) < size_) {
        data_[addr + i] = static_cast<uint8_t>(val[i]);
        ++i;
    }
    if ((size_t)(addr + i) < size_) data_[addr + i] = 0;
    return i;
}
