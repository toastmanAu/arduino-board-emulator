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
    // Directory open is path-of-existing-dir; sketches walk it with
    // openNextFile(). Detect that here so File knows to iterate.
    std::error_code ec;
    if (std_fs::is_directory(mapped.c_str(), ec)) {
        File f;
        f.setPath(String(path));
        f.set_as_directory(mapped.c_str());
        return f;
    }
    bool write = mode && (mode[0] == 'w' || mode[0] == 'a' || (mode[0] == 'r' && mode[1] == '+'));
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
    if (dir_iter_) {
        delete static_cast<std_fs::directory_iterator*>(dir_iter_);
        dir_iter_ = nullptr;
    }
    is_dir_ = false;
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

int File::available() {
    if (!fp_) return 0;
    long here = std::ftell(fp_);
    if (here < 0) return 0;
    std::fseek(fp_, 0, SEEK_END);
    long end = std::ftell(fp_);
    std::fseek(fp_, here, SEEK_SET);
    return end < 0 ? 0 : (int)(end - here);
}

size_t File::print(const char* s)   { if (!fp_ || !s) return 0; return std::fputs(s, fp_) >= 0 ? std::strlen(s) : 0; }
size_t File::print(int v)           { if (!fp_) return 0; return (size_t)std::fprintf(fp_, "%d",  v); }
size_t File::print(long v)          { if (!fp_) return 0; return (size_t)std::fprintf(fp_, "%ld", v); }
size_t File::print(unsigned long v) { if (!fp_) return 0; return (size_t)std::fprintf(fp_, "%lu", v); }
size_t File::print(double v, int d) { if (!fp_) return 0; return (size_t)std::fprintf(fp_, "%.*f", d, v); }
size_t File::println()              { if (!fp_) return 0; std::fputc('\n', fp_); return 1; }
size_t File::println(const char* s) { size_t n = print(s); return n + println(); }

String File::name() {
    if (path_.length() == 0) return path_;
    const char* c = path_.c_str();
    const char* slash = std::strrchr(c, '/');
    return slash ? String(slash + 1) : path_;
}

void File::set_as_directory(const std::string& abs_root) {
    is_dir_   = true;
    abs_root_ = abs_root;
    // Stash a directory-iterator-by-value on the heap so move semantics are
    // simple; the void* hides the std_fs type from the header.
    dir_iter_ = new std_fs::directory_iterator(abs_root_);
}

File File::openNextFile(const char* /*mode*/) {
    if (!is_dir_ || !dir_iter_) return File{};
    auto* it  = static_cast<std_fs::directory_iterator*>(dir_iter_);
    auto  end = std_fs::end(*it);
    if (*it == end) return File{};
    auto entry_path = (*it)->path();
    ++(*it);
    // Map host path back to sketch-visible mount-relative path: strip
    // abs_root_ prefix, prepend the parent dir's mount-relative path.
    std::string rel = entry_path.string();
    if (rel.size() > abs_root_.size() && rel.compare(0, abs_root_.size(), abs_root_) == 0) {
        rel = rel.substr(abs_root_.size());
    }
    std::string sketch_path = std::string(path_.c_str());
    if (!sketch_path.empty() && sketch_path.back() != '/') sketch_path += '/';
    sketch_path += rel.front() == '/' ? rel.substr(1) : rel;

    std::error_code ec;
    if (std_fs::is_directory(entry_path, ec)) {
        File f;
        f.setPath(String(sketch_path.c_str()));
        f.set_as_directory(entry_path.string());
        return f;
    }
    FILE* fp = std::fopen(entry_path.c_str(), "r");
    File f(fp, false);
    f.setPath(String(sketch_path.c_str()));
    return f;
}

}  // namespace fs
