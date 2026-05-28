#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WString.h"
#include <cstdio>

namespace fs {

class File {
public:
    File() = default;
    File(FILE* fp, bool writable) : fp_(fp), writable_(writable) {}
    ~File() { close(); }

    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& o) noexcept : fp_(o.fp_), writable_(o.writable_) { o.fp_ = nullptr; }
    File& operator=(File&& o) noexcept {
        if (this != &o) { close(); fp_ = o.fp_; writable_ = o.writable_; o.fp_ = nullptr; }
        return *this;
    }

    operator bool() const { return fp_ != nullptr; }

    size_t  read(uint8_t* buf, size_t n);
    int     read();
    size_t  write(const uint8_t* buf, size_t n);
    size_t  write(uint8_t b);
    size_t  size();
    void    close();
    void    flush();
    bool    seek(uint32_t pos);
    uint32_t position();

    String  name() { return path_; }
    void    setPath(const String& p) { path_ = p; }

private:
    FILE*  fp_       = nullptr;
    bool   writable_ = false;
    String path_;
};

class FS {
public:
    FS(const String& mount_root) : root_(mount_root) {}

    bool   begin(bool format_on_fail = false);
    void   end() {}
    File   open(const char* path, const char* mode = "r");
    File   open(const String& path, const char* mode = "r") { return open(path.c_str(), mode); }
    bool   exists(const char* path);
    bool   exists(const String& path) { return exists(path.c_str()); }
    bool   remove(const char* path);
    bool   mkdir(const char* path);

    size_t usedBytes()  { return 0; }
    size_t totalBytes() { return 1024 * 1024; }

private:
    String map_(const char* p);
    String root_;
};

}  // namespace fs

// File mode constants used by Arduino's SD / SPIFFS.
#define FILE_READ   "r"
#define FILE_WRITE  "w"
#define FILE_APPEND "a"
