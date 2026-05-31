#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include "WString.h"
#include <cstdio>

// Forward-decl SPIClass — fs::FS::begin(cs, spi, freq) takes one but we
// don't actually use it in the sim, so the header doesn't depend on SPI.h.
class SPIClass;

// SD card type — ESP32 SD library enum, declared at global scope to match
// real hardware (sketches use `CARD_NONE` etc. unqualified). Defined before
// namespace fs so fs::FS::cardType() can reference it.
typedef enum {
    CARD_NONE    = 0,
    CARD_MMC     = 1,
    CARD_SD      = 2,
    CARD_SDHC    = 3,
    CARD_UNKNOWN = 4,
} sdcard_type_t;

namespace fs {

class File {
public:
    File() = default;
    File(FILE* fp, bool writable) : fp_(fp), writable_(writable) {}
    ~File() { close(); }

    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& o) noexcept
        : fp_(o.fp_), writable_(o.writable_), path_(std::move(o.path_)),
          dir_iter_(o.dir_iter_), is_dir_(o.is_dir_) {
        o.fp_ = nullptr; o.dir_iter_ = nullptr;
    }
    File& operator=(File&& o) noexcept {
        if (this != &o) {
            close(); fp_ = o.fp_; writable_ = o.writable_;
            path_ = std::move(o.path_); dir_iter_ = o.dir_iter_; is_dir_ = o.is_dir_;
            o.fp_ = nullptr; o.dir_iter_ = nullptr;
        }
        return *this;
    }

    operator bool() const { return fp_ != nullptr || is_dir_; }

    size_t  read(uint8_t* buf, size_t n);
    int     read();
    int     available();
    size_t  write(const uint8_t* buf, size_t n);
    size_t  write(uint8_t b);
    size_t  size();
    void    close();
    void    flush();
    bool    seek(uint32_t pos);
    uint32_t position();

    // Print-API helpers — Arduino's File inherits from Print on real hardware.
    // We replicate just enough surface for code that streams strings/numbers.
    size_t  print(const char* s);
    size_t  print(const String& s) { return print(s.c_str()); }
    size_t  print(int v);
    size_t  print(long v);
    size_t  print(unsigned long v);
    size_t  print(double v, int decimals = 2);
    size_t  println();
    size_t  println(const char* s);
    size_t  println(const String& s) { return println(s.c_str()); }

    // Directory APIs — `openNextFile()` walks the FS::open(path, "r") on a
    // directory; in the sim we open the directory iterator under the hood.
    bool    isDirectory() const { return is_dir_; }
    File    openNextFile(const char* mode = "r");

    String  name();   // basename of path_
    String  path()    { return path_; }
    void    setPath(const String& p) { path_ = p; }

    // Hook for FS::open() to mark a directory-flavoured File. Stores the
    // host-side absolute root path so openNextFile can iterate it.
    void    set_as_directory(const std::string& abs_root);

private:
    FILE*  fp_       = nullptr;
    bool   writable_ = false;
    String path_;          // sketch-visible path (mount-relative, leading "/")
    void*  dir_iter_ = nullptr;
    bool   is_dir_   = false;
    std::string abs_root_; // host-side absolute path (only set when is_dir_)
};

class FS {
public:
    FS(const String& mount_root) : root_(mount_root) {}

    bool   begin(bool format_on_fail = false);
    // ESP32 SD overload: begin(cs, spi, frequency). We ignore the SPI side
    // — sim FS is host-backed — but accept the call so SD-using sketches
    // compile and mount.
    bool   begin(int /*cs*/, SPIClass& /*spi*/, uint32_t /*frequency*/ = 4000000) {
        return begin(false);
    }
    void   end() {}
    File   open(const char* path, const char* mode = "r");
    File   open(const String& path, const char* mode = "r") { return open(path.c_str(), mode); }
    bool   exists(const char* path);
    bool   exists(const String& path) { return exists(path.c_str()); }
    bool   remove(const char* path);
    bool   mkdir(const char* path);

    size_t usedBytes()  { return 0; }
    size_t totalBytes() { return 1024 * 1024; }

    // SD-specific introspection. The sim always reports a 512MB SDHC card so
    // `cardType() != CARD_NONE` gates open and capacity checks pass. Returning
    // a non-NONE value also lets the sketch reach the "mounted" code path.
    ::sdcard_type_t cardType()  { return root_ == "sd" ? ::CARD_SDHC : ::CARD_NONE; }
    uint64_t        cardSize()  { return root_ == "sd" ? (512ULL * 1024 * 1024) : 0; }

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
