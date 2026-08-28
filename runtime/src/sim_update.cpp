// Real backing for the OTA Update shim. Writes the streamed firmware to
// <project>/.boardghost/ota-firmware.bin (or BOARDGHOST_OTA_PATH if set) so
// the user can verify what their sketch's upload handler actually received.
//
// On end():
//   - flushes + closes the file
//   - if setMD5(expected) was called, hashes the on-disk bytes and compares
//   - if begin(size>0) was called and `evenIfRemaining=false`, requires
//     written == expected_size before declaring success
//
// abort() unlinks the partial file so a half-uploaded firmware can't be
// mistaken for a complete one.
//
// Mirrors the ESP32 OTA semantics close enough that a sketch's branch
// logic (Update.hasError() ? "FAIL" : "OK") returns the same string in the
// sim as it would on hardware, given the same upload bytes.

#include <cstdint>
#include "Update.h"
#include "Arduino.h"      // Stream base class

// Singleton instance — sketches use `Update.foo()` directly via this global.
UpdateClass Update;

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

fs::path ota_path() {
    if (const char* env = std::getenv("BOARDGHOST_OTA_PATH"); env && *env) {
        return fs::path(env);
    }
    // Default: <project>/.boardghost/ota-firmware.bin, derived the same way
    // sim_eeprom + sim_fs anchor their state.
    auto cwd = fs::current_path();
    return cwd.parent_path().parent_path() / "ota-firmware.bin";
}

std::string md5_hex(const std::string& path) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return {};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) { std::fclose(fp); return {}; }
    if (EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx); std::fclose(fp); return {};
    }
    unsigned char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (EVP_DigestUpdate(ctx, buf, n) != 1) {
            EVP_MD_CTX_free(ctx); std::fclose(fp); return {};
        }
    }
    std::fclose(fp);
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;
    EVP_DigestFinal_ex(ctx, digest, &len);
    EVP_MD_CTX_free(ctx);
    char hex[2 * EVP_MAX_MD_SIZE + 1] = {0};
    for (unsigned i = 0; i < len; ++i) {
        std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    return std::string(hex);
}

}  // namespace

bool UpdateClass::begin(size_t size, int command,
                        int /*ledPin*/, uint8_t /*ledOn*/,
                        const char* /*label*/) {
    // Refuse re-entry if a previous upload is still open — matches ESP32
    // which only allows one OTA at a time. Tests for this branch should
    // either end() or abort() first.
    if (fd_ >= 0) {
        error_     = true;
        error_msg_ = "previous update still open";
        return false;
    }
    error_         = false;
    error_msg_.clear();
    finished_      = false;
    written_       = 0;
    expected_size_ = size;
    command_       = command;
    actual_md5_.clear();
    expected_md5_.clear();   // stale value from a prior session would otherwise leak into the new verify

    path_ = ota_path().string();
    std::error_code ec;
    fs::create_directories(fs::path(path_).parent_path(), ec);
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0) {
        error_     = true;
        error_msg_ = "could not open OTA file: " + path_;
        return false;
    }
    std::fprintf(stderr,
        "[boardghost] Update.begin(size=%zu, command=%d) → %s\n",
        size, command, path_.c_str());
    return true;
}

size_t UpdateClass::write(uint8_t* data, size_t len) {
    if (fd_ < 0 || !data || len == 0) return 0;
    ssize_t w = ::write(fd_, data, len);
    if (w < 0) {
        error_     = true;
        error_msg_ = "write failed";
        return 0;
    }
    written_ += (size_t)w;
    return (size_t)w;
}

size_t UpdateClass::writeStream(Stream& data) {
    // Read in 4KB chunks until the stream stops producing bytes. Honour
    // expected_size_ as an upper bound when it was set, so a misbehaving
    // peer can't write past the declared firmware size.
    if (fd_ < 0) return 0;
    uint8_t buf[4096];
    size_t  total = 0;
    while (true) {
        // Stop early if we've already written the full expected size.
        size_t budget = sizeof(buf);
        if (expected_size_ > 0) {
            if (written_ >= expected_size_) break;
            budget = std::min(budget, expected_size_ - written_);
        }
        int got = data.read(buf, budget);
        if (got <= 0) break;
        size_t w = write(buf, (size_t)got);
        if (w != (size_t)got) {
            error_ = true;
            error_msg_ = "short write";
            break;
        }
        total += w;
    }
    return total;
}

bool UpdateClass::end(bool evenIfRemaining) {
    if (fd_ < 0) {
        error_ = true;
        error_msg_ = "end without begin";
        return false;
    }
    ::close(fd_);
    fd_ = -1;
    finished_ = true;

    if (expected_size_ > 0 && written_ != expected_size_ && !evenIfRemaining) {
        error_ = true;
        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "size mismatch: wrote %zu, expected %zu",
            written_, expected_size_);
        error_msg_ = msg;
        return false;
    }

    actual_md5_ = md5_hex(path_);
    if (!expected_md5_.empty() && actual_md5_ != expected_md5_) {
        error_ = true;
        error_msg_ = "MD5 mismatch (expected " + expected_md5_ +
                     ", actual " + actual_md5_ + ")";
        return false;
    }
    std::fprintf(stderr,
        "[boardghost] Update.end: %zu bytes written to %s, md5=%s\n",
        written_, path_.c_str(), actual_md5_.c_str());
    return !error_;
}

void UpdateClass::abort() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (!path_.empty()) {
        std::error_code ec;
        fs::remove(path_, ec);
    }
    error_     = true;
    error_msg_ = "aborted";
    finished_  = true;
}

const char* UpdateClass::errorString() {
    if (error_msg_.empty()) return error_ ? "unknown" : "";
    return error_msg_.c_str();
}

void UpdateClass::setMD5(const char* expected) {
    expected_md5_ = expected ? expected : "";
    // Normalise to lowercase so comparisons aren't case-sensitive — most
    // sketches generate the hex via stdlib which is lowercase, but some
    // human-edited test fixtures use uppercase.
    for (auto& c : expected_md5_) c = (char)std::tolower((unsigned char)c);
}

String UpdateClass::md5String() {
    return String(actual_md5_.c_str());
}
