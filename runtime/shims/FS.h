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

// Hoist `File` to global scope to match ESP32 Arduino's <FS.h> behavior —
// sketches commonly write `File f = SPIFFS.open(...)` without qualifying.
using File = fs::File;

// File mode constants used by Arduino's SD / SPIFFS.
#define FILE_READ   "r"
#define FILE_WRITE  "w"
#define FILE_APPEND "a"

// -----------------------------------------------------------------------
// LovyanGFX DataWrapper specializations for fs::File and fs::FS.
//
// LovyanGFX's drawJpgFile(T& fs, ...) instantiates DataWrapperT<T>.
// We must specialise DataWrapperT for our shim fs::FS (and fs::File)
// so the template can be instantiated with concrete virtual-method bodies.
// These specializations mirror the ones in LovyanGFX's esp8266/common.hpp.
// -----------------------------------------------------------------------
#ifdef BOARDGHOST_SIM
#include "lgfx/v1/misc/DataWrapper.hpp"

namespace lgfx { inline namespace v1 {

  template <>
  struct DataWrapperT<fs::File> : public DataWrapper {
    DataWrapperT(fs::File* fp = nullptr) : DataWrapper{}, _fp { fp } {
      need_transaction = false;
    }
    int     read(uint8_t* buf, uint32_t len) override {
      if (!_fp) return 0;
      return (int)_fp->read(buf, len);
    }
    void    skip(int32_t offset) override {
      if (!_fp) return;
      _fp->seek((uint32_t)(_fp->position() + offset));
    }
    bool    seek(uint32_t offset) override {
      if (!_fp) return false;
      return _fp->seek(offset);
    }
    void    close(void) override { if (_fp) _fp->close(); }
    int32_t tell(void) override { return _fp ? (int32_t)_fp->position() : 0; }
  protected:
    fs::File* _fp;
  };

  template <>
  struct DataWrapperT<fs::FS> : public DataWrapperT<fs::File> {
    DataWrapperT(fs::FS* fs, fs::File* fp = nullptr)
      : DataWrapperT<fs::File>{ fp }, _fs { fs } {}
    bool open(const char* path) override {
      _file = _fs->open(path, "r");
      DataWrapperT<fs::File>::_fp = &_file;
      return (bool)_file;
    }
  protected:
    fs::FS*  _fs;
    fs::File _file;
  };

}}  // namespace lgfx::v1
#endif  // BOARDGHOST_SIM
