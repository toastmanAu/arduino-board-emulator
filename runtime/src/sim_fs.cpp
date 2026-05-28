#include "FS.h"
#include "SPIFFS.h"
#include "LittleFS.h"
#include "SD.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cstdio>

namespace std_fs = std::filesystem;

// All three filesystems map to per-mount subdirs under <project>/sim-assets/
// so sketches that use both SPIFFS and LittleFS don't collide.
fs::FS SPIFFS("spiffs");
fs::FS LittleFS("littlefs");
fs::FS SD("sd");
SDClass SDLib;

namespace {
std_fs::path assets_root() {
    if (const char* p = std::getenv("BOARDGHOST_ASSETS_DIR"); p && *p) return std_fs::path(p);
    // When the sketch binary runs from <project>/.boardghost/<board>/build,
    // step up two levels to reach <project>/sim-assets.
    auto cwd = std_fs::current_path();
    return cwd.parent_path().parent_path() / "sim-assets";
}
}  // namespace

namespace fs {

String FS::map_(const char* p) {
    if (!p) return String("");
    auto root = assets_root() / root_.c_str();
    auto path = root / (p[0] == '/' ? p + 1 : p);
    return String(path.c_str());
}

bool FS::begin(bool /*format_on_fail*/) {
    auto root = assets_root() / root_.c_str();
    std::error_code ec;
    std_fs::create_directories(root, ec);
    return !ec;
}

File FS::open(const char* path, const char* mode) {
    auto mapped = map_(path);
    bool write = mode && (mode[0] == 'w' || mode[0] == 'a' || mode[0] == 'r' && mode[1] == '+');
    FILE* fp = std::fopen(mapped.c_str(), mode);
    File f(fp, write);
    f.setPath(String(path));
    return f;
}

bool FS::exists(const char* path) {
    auto mapped = map_(path);
    return std_fs::exists(mapped.c_str());
}

bool FS::remove(const char* path) {
    auto mapped = map_(path);
    std::error_code ec;
    return std_fs::remove(mapped.c_str(), ec);
}

bool FS::mkdir(const char* path) {
    auto mapped = map_(path);
    std::error_code ec;
    return std_fs::create_directories(mapped.c_str(), ec);
}

// --- File ---

size_t File::read(uint8_t* buf, size_t n) {
    if (!fp_) return 0;
    return std::fread(buf, 1, n, fp_);
}

int File::read() {
    if (!fp_) return -1;
    int c = std::fgetc(fp_);
    return c == EOF ? -1 : c;
}

size_t File::write(const uint8_t* buf, size_t n) {
    if (!fp_) return 0;
    return std::fwrite(buf, 1, n, fp_);
}

size_t File::write(uint8_t b) {
    if (!fp_) return 0;
    std::fputc(b, fp_);
    return 1;
}

size_t File::size() {
    if (!fp_) return 0;
    long here = std::ftell(fp_);
    std::fseek(fp_, 0, SEEK_END);
    long s = std::ftell(fp_);
    std::fseek(fp_, here, SEEK_SET);
    return s < 0 ? 0 : (size_t)s;
}

void File::close() {
    if (fp_) { std::fclose(fp_); fp_ = nullptr; }
}

void File::flush() { if (fp_) std::fflush(fp_); }

bool File::seek(uint32_t pos) {
    if (!fp_) return false;
    return std::fseek(fp_, (long)pos, SEEK_SET) == 0;
}

uint32_t File::position() {
    if (!fp_) return 0;
    long p = std::ftell(fp_);
    return p < 0 ? 0 : (uint32_t)p;
}

}  // namespace fs
