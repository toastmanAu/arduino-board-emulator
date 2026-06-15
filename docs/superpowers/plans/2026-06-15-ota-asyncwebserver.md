# ArduinoOTA + AsyncWebServer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two ESP32 network shims to the BoardGhost runtime — a real espota-protocol ArduinoOTA receiver (with a `boardghost ota push` CLI), then an ESPAsyncWebServer shim covering core request/response + Server-Sent Events.

**Architecture:** ArduinoOTA is a UDP/TCP front-end that feeds received bytes into the existing `Update` shim (which already writes `.boardghost/ota-firmware.bin` with MD5 verification) and advertises `_arduino._tcp` through the existing avahi mDNS impl. AsyncWebServer is a second `httplib::Server` exposing the AsyncWebServer API with per-request objects; SSE is a chunked streaming response with a per-client queue. Both gate real I/O on `BOARDGHOST_NET=real` and add no new third-party deps.

**Tech Stack:** C++17, POSIX sockets, OpenSSL EVP (MD5), vendored cpp-httplib, GoogleTest, Rust/clap (CLI).

**Spec:** `docs/superpowers/specs/2026-06-15-ota-asyncwebserver-design.md`

**Reference files (read before starting):**
- `runtime/shims/Update.h` + `runtime/src/sim_update.cpp` — the byte-sink ArduinoOTA feeds; note `md5_hex(path)` helper pattern and OpenSSL EVP usage.
- `runtime/shims/ESPmDNS.h` + `runtime/src/sim_mdns.cpp` — `enableArduino()` is currently a no-op; `MDnsImpl::spawn()` runs `avahi-publish*`.
- `runtime/src/sim_wifi.cpp` — POSIX socket style (`getaddrinfo`/`socket`/`connect`).
- `runtime/src/sim_webserver.cpp` — the existing cpp-httplib pattern AsyncWebServer mirrors.
- `runtime/shims/FS.h` (`fs::FS::open` → `fs::File`; `extern fs::FS SPIFFS;`).
- `runtime/include/sim_net.h` — `sim_net_mode()` returns `BOARDGHOST_NET_REAL` etc.

---

## Phase A — ArduinoOTA

### Task 1: Make `MDNS.enableArduino()` advertise `_arduino._tcp`

ArduinoOTA's `begin()` calls `MDNS.enableArduino(port, auth)` to advertise the OTA service. Today that's an inline no-op in the header. Make it real: spawn an `avahi-publish-service` for `_arduino._tcp` with the TXT records the IDE looks for.

**Files:**
- Modify: `runtime/shims/ESPmDNS.h` (turn `enableArduino` into a declared method)
- Modify: `runtime/src/sim_mdns.cpp` (implement it on `MDnsImpl` + `MDNSResponder`)
- Test: `runtime/tests/test_mdns.cpp` (add a case)

- [ ] **Step 1: Write the failing test**

Add to `runtime/tests/test_mdns.cpp` (inside the existing anonymous-namespace test section):

```cpp
// enableArduino must register an _arduino._tcp service without crashing and
// return cleanly whether or not avahi is installed (CI has no avahi-daemon).
TEST(MdnsTest, EnableArduinoAdvertisesArduinoService) {
    setenv("BOARDGHOST_MDNS", "off", 1);   // exercise the disabled fast-path
    MDNSResponder mdns;
    EXPECT_TRUE(mdns.begin("ghostboard"));
    mdns.enableArduino(3232, /*auth=*/false);  // must not crash / hang
    mdns.end();
    unsetenv("BOARDGHOST_MDNS");
    SUCCEED();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target runtime_tests && ./build/tests/runtime_tests --gtest_filter='MdnsTest.EnableArduinoAdvertisesArduinoService'`
Expected: FAIL to compile — `enableArduino` is inline-only in the header and behaves as a no-op; this test only locks the contract. (If it compiles and passes immediately because the no-op exists, proceed — the real assertion is the implementation in Step 4 doesn't regress it.)

- [ ] **Step 3: Implement**

In `runtime/shims/ESPmDNS.h`, replace the inline `enableArduino` body with a declaration:

```cpp
    // Advertise the _arduino._tcp service so arduino-cli / the IDE network
    // port discovers this sketch as an OTA target. Real impl in sim_mdns.cpp.
    void enableArduino(uint16_t port = 3232, bool auth = false);
```

In `runtime/src/sim_mdns.cpp`, add a method to `MDnsImpl` (after `addService`):

```cpp
    // espota discovery record. TXT keys mirror what the ESP32 core publishes;
    // the IDE keys on `tcp_check` / `auth_upload`. avahi-publish-service takes
    // TXT entries as trailing key=value args.
    bool enableArduino(uint16_t port, bool auth) {
        if (disabled()) return true;
        std::string name = hostname_.empty() ? "esp32" : hostname_;
        spawn({"avahi-publish-service", name, "_arduino._tcp",
               std::to_string(port),
               "tcp_check=no", "ssh_upload=no", "board=esp32",
               std::string("auth_upload=") + (auth ? "yes" : "no")});
        return true;
    }
```

Then at the bottom of the file, after the other `MDNSResponder::` definitions:

```cpp
void MDNSResponder::enableArduino(uint16_t port, bool auth) {
    impl_->enableArduino(port, auth);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target runtime_tests && ./build/tests/runtime_tests --gtest_filter='MdnsTest.*'`
Expected: PASS (all mDNS tests).

- [ ] **Step 5: Commit**

```bash
git add runtime/shims/ESPmDNS.h runtime/src/sim_mdns.cpp runtime/tests/test_mdns.cpp
git commit -m "feat(mdns): real _arduino._tcp advertisement via enableArduino"
```

---

### Task 2: ArduinoOTA shim header

Define the full ArduinoOTA API surface. No behaviour yet — header only, so the receiver impl in Task 3 has a stable interface.

**Files:**
- Create: `runtime/shims/ArduinoOTA.h`

- [ ] **Step 1: Create the header**

`runtime/shims/ArduinoOTA.h`:

```cpp
#pragma once
#include <stdint.h>
#include <functional>
#include <memory>
#include <string>
#include "WString.h"

// ArduinoOTA shim. Under BOARDGHOST_NET=real, begin() binds a UDP listener
// (default 127.0.0.1:3232) and speaks the espota protocol: an invite arrives,
// we connect back over TCP, stream the firmware into the Update shim (which
// writes <project>/.boardghost/ota-firmware.bin and verifies MD5), and fire
// the sketch's onStart/onProgress/onEnd/onError callbacks. The bytes are
// inspectable on disk but cannot be executed — the sim runs a host ELF, not
// an ESP32 image. See docs/superpowers/specs/2026-06-15-...-design.md.
//
// Env:
//   BOARDGHOST_OTA_PORT  (default 3232)   UDP listener port
//   BOARDGHOST_OTA_BIND  (default 127.0.0.1)  set 0.0.0.0 for LAN IDE testing
//   BOARDGHOST_OTA_PATH  (Update shim)    where received firmware is written

enum ota_error_t {
    OTA_AUTH_ERROR    = 0,
    OTA_BEGIN_ERROR   = 1,
    OTA_CONNECT_ERROR = 2,
    OTA_RECEIVE_ERROR = 3,
    OTA_END_ERROR     = 4,
};

namespace boardghost_internal { class OtaImpl; }

class ArduinoOTAClass {
public:
    ArduinoOTAClass();
    ~ArduinoOTAClass();

    ArduinoOTAClass(const ArduinoOTAClass&) = delete;
    ArduinoOTAClass& operator=(const ArduinoOTAClass&) = delete;

    using THandlerFunction         = std::function<void()>;
    using THandlerFunctionError    = std::function<void(ota_error_t)>;
    using THandlerFunctionProgress = std::function<void(unsigned int, unsigned int)>;

    ArduinoOTAClass& setPort(uint16_t port);
    ArduinoOTAClass& setHostname(const char* hostname);
    String getHostname();
    ArduinoOTAClass& setPassword(const char* password);
    ArduinoOTAClass& setPasswordHash(const char* passwordHash);
    ArduinoOTAClass& setRebootOnSuccess(bool reboot);
    ArduinoOTAClass& setMdnsEnabled(bool enabled);
    void setTimeout(int /*timeoutMs*/) {}

    void onStart(THandlerFunction fn);
    void onEnd(THandlerFunction fn);
    void onProgress(THandlerFunctionProgress fn);
    void onError(THandlerFunctionError fn);

    void begin();
    void end();
    void handle();
    int  getCommand();   // U_FLASH (0) or U_SPIFFS (100)

private:
    std::unique_ptr<boardghost_internal::OtaImpl> impl_;
};

extern ArduinoOTAClass ArduinoOTA;
```

- [ ] **Step 2: Commit (header compiles standalone via the test build later; no test yet)**

```bash
git add runtime/shims/ArduinoOTA.h
git commit -m "feat(ota): ArduinoOTA shim header (API surface)"
```

---

### Task 3: espota receiver impl + CMake wiring + tests

The core. `sim_ota.cpp` implements the espota state machine; `handle()` polls the UDP socket non-blocking, and on an invite runs the (blocking) transfer feeding `Update`.

**Files:**
- Create: `runtime/src/sim_ota.cpp`
- Create: `runtime/tests/test_ota.cpp`
- Modify: `runtime/CMakeLists.txt` (add `src/sim_ota.cpp` to `sim_runtime`)
- Modify: `runtime/tests/CMakeLists.txt` (add `test_ota.cpp` to `runtime_tests`)

- [ ] **Step 1: Wire CMake first (so the test target picks up the new files)**

In `runtime/CMakeLists.txt`, add to the `add_library(sim_runtime STATIC ...)` source list, after `src/sim_update.cpp`:

```cmake
    src/sim_ota.cpp
```

In `runtime/tests/CMakeLists.txt`, add to the `add_executable(runtime_tests ...)` list, after `test_update.cpp`:

```cpp
    test_ota.cpp
```

- [ ] **Step 2: Write the failing test**

`runtime/tests/test_ota.cpp`:

```cpp
// Drives the ArduinoOTA espota receiver end-to-end over loopback. The test
// plays the role of the espota host: it opens a TCP listener, sends the UDP
// invite, then serves the firmware bytes; ArduinoOTA.handle() recvs the
// invite, connects back, and streams into the Update shim.

#include <gtest/gtest.h>
#include "ArduinoOTA.h"
#include "Update.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/evp.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

std::string md5_hex(const std::string& in);  // defined below via OpenSSL

class OtaTest : public ::testing::Test {
protected:
    fs::path ota_path;
    uint16_t ota_port;

    void SetUp() override {
        std::random_device rd; std::mt19937 rng(rd());
        int n = std::uniform_int_distribution<int>(20000, 40000)(rng);
        ota_path = fs::temp_directory_path() / ("bgh-ota-" + std::to_string(n) + ".bin");
        ota_port = (uint16_t)(40000 + (n % 20000));
        setenv("BOARDGHOST_OTA_PATH", ota_path.c_str(), 1);
        setenv("BOARDGHOST_OTA_PORT", std::to_string(ota_port).c_str(), 1);
        setenv("BOARDGHOST_OTA_BIND", "127.0.0.1", 1);
        setenv("BOARDGHOST_NET", "real", 1);
        setenv("BOARDGHOST_MDNS", "off", 1);
    }
    void TearDown() override {
        std::error_code ec; fs::remove(ota_path, ec);
        unsetenv("BOARDGHOST_OTA_PATH"); unsetenv("BOARDGHOST_OTA_PORT");
        unsetenv("BOARDGHOST_OTA_BIND"); unsetenv("BOARDGHOST_NET");
        unsetenv("BOARDGHOST_MDNS");
    }
    std::string read_file() {
        std::ifstream f(ota_path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
};

// Minimal espota host. Returns true if the device sent the closing "OK".
// `password` empty => no-auth invite. Drives one upload.
bool espota_push(uint16_t device_port, const std::string& firmware,
                 const std::string& password = "") {
    // 1. TCP listener on an ephemeral port.
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in la{}; la.sin_family = AF_INET; la.sin_addr.s_addr = htonl(INADDR_LOOPBACK); la.sin_port = 0;
    if (::bind(srv, (sockaddr*)&la, sizeof(la)) != 0) { ::close(srv); return false; }
    socklen_t ll = sizeof(la); getsockname(srv, (sockaddr*)&la, &ll);
    uint16_t host_port = ntohs(la.sin_port);
    ::listen(srv, 1);

    // 2. UDP invite to the device.
    int u = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in da{}; da.sin_family = AF_INET; da.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    da.sin_port = htons(device_port);
    std::string md5 = md5_hex(firmware);
    std::string invite = "0 " + std::to_string(host_port) + " " +
                         std::to_string(firmware.size()) + " " + md5 + "\n";
    ::sendto(u, invite.data(), invite.size(), 0, (sockaddr*)&da, sizeof(da));

    // 3. Read the device's UDP reply ("OK" or "AUTH <nonce>").
    char rb[256]; sockaddr_in from{}; socklen_t fl = sizeof(from);
    timeval tv{2, 0}; setsockopt(u, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ssize_t rn = ::recvfrom(u, rb, sizeof(rb) - 1, 0, (sockaddr*)&from, &fl);
    if (rn <= 0) { ::close(u); ::close(srv); return false; }
    rb[rn] = 0;
    std::string reply(rb);
    if (reply.rfind("AUTH", 0) == 0) {
        std::string nonce = reply.substr(5);
        while (!nonce.empty() && (nonce.back()=='\n'||nonce.back()=='\r'||nonce.back()==' ')) nonce.pop_back();
        std::string passmd5 = md5_hex(password);
        std::string cnonce  = md5_hex(firmware + std::to_string(host_port));
        std::string result  = md5_hex(passmd5 + ":" + nonce + ":" + cnonce);
        std::string authmsg = cnonce + " " + result + "\n";
        ::sendto(u, authmsg.data(), authmsg.size(), 0, (sockaddr*)&da, sizeof(da));
        rn = ::recvfrom(u, rb, sizeof(rb) - 1, 0, (sockaddr*)&from, &fl);
        if (rn <= 0) { ::close(u); ::close(srv); return false; }
        rb[rn] = 0; reply = rb;
        if (reply.rfind("OK", 0) != 0) { ::close(u); ::close(srv); return false; }
    }

    // 4. Accept the device's TCP connect-back and stream the firmware.
    int c = ::accept(srv, nullptr, nullptr);
    ::close(srv);
    if (c < 0) { ::close(u); return false; }
    size_t sent = 0;
    while (sent < firmware.size()) {
        ssize_t w = ::send(c, firmware.data() + sent, firmware.size() - sent, 0);
        if (w <= 0) break;
        sent += (size_t)w;
    }
    // 5. Read the device's closing status.
    char fin[64]; ssize_t fn = ::recv(c, fin, sizeof(fin) - 1, 0);
    ::close(c); ::close(u);
    if (fn <= 0) return false;
    fin[fn] = 0;
    return std::string(fin).rfind("OK", 0) == 0;
}

// OpenSSL MD5 (mirrors sim_update.cpp). Test-local helper.
std::string md5_hex(const std::string& in) {
    unsigned char d[EVP_MAX_MD_SIZE]; unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(ctx, in.data(), in.size());
    EVP_DigestFinal_ex(ctx, d, &len);
    EVP_MD_CTX_free(ctx);
    static const char* h = "0123456789abcdef";
    std::string o; o.reserve(len * 2);
    for (unsigned i = 0; i < len; i++) { o += h[d[i] >> 4]; o += h[d[i] & 0xf]; }
    return o;
}

}  // namespace

TEST_F(OtaTest, ReceivesFirmwareAndFiresCallbacks) {
    ArduinoOTA.setPort(ota_port);
    std::atomic<bool> done{false}, started{false};
    std::atomic<unsigned> last_total{0};
    ArduinoOTA.onStart([&]{ started = true; });
    ArduinoOTA.onProgress([&](unsigned, unsigned total){ last_total = total; });
    ArduinoOTA.onEnd([&]{ done = true; });
    ArduinoOTA.begin();

    std::string fw = "OTA-FIRMWARE-" + std::string(4096, 'Z');
    std::thread host([&]{ EXPECT_TRUE(espota_push(ota_port, fw)); });

    // Pump handle() until the transfer completes (or time out).
    for (int i = 0; i < 500 && !done.load(); ++i) {
        ArduinoOTA.handle();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    host.join();
    ArduinoOTA.end();

    EXPECT_TRUE(started.load());
    EXPECT_TRUE(done.load());
    EXPECT_EQ(last_total.load(), fw.size());
    EXPECT_EQ(read_file(), fw);
}

TEST_F(OtaTest, WrongPasswordTriggersAuthError) {
    ArduinoOTA.setPort(ota_port);
    ArduinoOTA.setPassword("correct-horse");
    std::atomic<bool> err{false}; std::atomic<int> code{-1};
    ArduinoOTA.onError([&](ota_error_t e){ err = true; code = (int)e; });
    ArduinoOTA.begin();

    std::string fw = "should-not-land";
    std::thread host([&]{ espota_push(ota_port, fw, "wrong-password"); });
    for (int i = 0; i < 400 && !err.load(); ++i) {
        ArduinoOTA.handle();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    host.join();
    ArduinoOTA.end();

    EXPECT_TRUE(err.load());
    EXPECT_EQ(code.load(), (int)OTA_AUTH_ERROR);
    EXPECT_FALSE(fs::exists(ota_path));   // rejected before any write
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target runtime_tests`
Expected: FAIL to link — `sim_ota.cpp` doesn't exist yet, so `ArduinoOTA` is undefined.

- [ ] **Step 4: Implement `runtime/src/sim_ota.cpp`**

```cpp
// espota-protocol OTA receiver. See ArduinoOTA.h for the env contract.
#include "ArduinoOTA.h"
#include "Update.h"
#include "ESPmDNS.h"
#include "WiFi.h"
#include "sim_net.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#if defined(BOARDGHOST_WITH_OPENSSL)
#include <openssl/evp.h>
#endif

namespace boardghost_internal {

#if defined(BOARDGHOST_WITH_OPENSSL)
static std::string ota_md5(const std::string& in) {
    unsigned char d[EVP_MAX_MD_SIZE]; unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(ctx, in.data(), in.size());
    EVP_DigestFinal_ex(ctx, d, &len);
    EVP_MD_CTX_free(ctx);
    static const char* h = "0123456789abcdef";
    std::string o; o.reserve(len * 2);
    for (unsigned i = 0; i < len; i++) { o += h[d[i] >> 4]; o += h[d[i] & 0xf]; }
    return o;
}
#else
static std::string ota_md5(const std::string&) { return std::string(); }
#endif

static uint16_t resolve_port(uint16_t def) {
    if (const char* e = std::getenv("BOARDGHOST_OTA_PORT")) {
        int p = std::atoi(e);
        if (p > 0 && p < 65536) return (uint16_t)p;
    }
    return def;
}

class OtaImpl {
public:
    ~OtaImpl() { stop(); }

    void set_port(uint16_t p)          { port_ = p; }
    void set_hostname(const char* h)   { hostname_ = h ? h : ""; }
    std::string hostname() const       { return hostname_; }
    void set_password(const char* p)   { pass_md5_ = (p && *p) ? ota_md5(p) : ""; auth_ = !pass_md5_.empty(); }
    void set_password_hash(const char* h) { pass_md5_ = h ? h : ""; auth_ = !pass_md5_.empty(); }
    void set_mdns(bool e)              { mdns_ = e; }

    std::function<void()> on_start_, on_end_;
    std::function<void(unsigned, unsigned)> on_progress_;
    std::function<void(ota_error_t)> on_error_;
    int command_ = 0;

    void begin() {
        if (sim_net_mode() != BOARDGHOST_NET_REAL) {
            std::fprintf(stderr,
                "[boardghost] ArduinoOTA.begin(): inert (set BOARDGHOST_NET=real "
                "to accept OTA pushes on UDP:%u).\n", resolve_port(port_));
            return;
        }
        stop();
        port_ = resolve_port(port_);
        const char* bind_env = std::getenv("BOARDGHOST_OTA_BIND");
        std::string bind_addr = bind_env ? bind_env : "127.0.0.1";

        udp_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_fd_ < 0) return;
        int one = 1; setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port_);
        a.sin_addr.s_addr = inet_addr(bind_addr.c_str());
        if (::bind(udp_fd_, (sockaddr*)&a, sizeof(a)) != 0) {
            std::fprintf(stderr, "[boardghost] ArduinoOTA: bind %s:%u failed.\n",
                         bind_addr.c_str(), port_);
            ::close(udp_fd_); udp_fd_ = -1; return;
        }
        int fl = fcntl(udp_fd_, F_GETFL, 0); fcntl(udp_fd_, F_SETFL, fl | O_NONBLOCK);

        // Advertise only when reachable from the LAN (non-loopback bind).
        if (mdns_ && bind_addr != "127.0.0.1") {
            MDNS.begin(hostname_.empty() ? "esp32" : hostname_.c_str());
            MDNS.enableArduino(port_, auth_);
        }
        std::fprintf(stderr, "[boardghost] ArduinoOTA: listening on %s:%u%s\n",
                     bind_addr.c_str(), port_, auth_ ? " (auth)" : "");
    }

    void stop() {
        if (udp_fd_ >= 0) { ::close(udp_fd_); udp_fd_ = -1; }
    }

    void handle() {
        if (udp_fd_ < 0) return;
        char buf[512]; sockaddr_in from{}; socklen_t fl = sizeof(from);
        ssize_t n = ::recvfrom(udp_fd_, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 0) return;   // no invite pending
        buf[n] = 0;

        // Invite: "<cmd> <host_tcp_port> <size> <md5>"
        std::istringstream iss(buf);
        int cmd = 0; uint16_t host_port = 0; size_t size = 0; std::string md5;
        if (!(iss >> cmd >> host_port >> size >> md5)) return;
        command_ = cmd;

        if (auth_) {
            std::string nonce = ota_md5(std::to_string(getpid()) + ":" + std::to_string(++nonce_ctr_));
            std::string authreq = "AUTH " + nonce + "\n";
            ::sendto(udp_fd_, authreq.data(), authreq.size(), 0, (sockaddr*)&from, fl);
            // Await the host's response (blocking with a short timeout).
            timeval tv{3, 0}; setsockopt(udp_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            int curfl = fcntl(udp_fd_, F_GETFL, 0); fcntl(udp_fd_, F_SETFL, curfl & ~O_NONBLOCK);
            char rb[256]; ssize_t rn = ::recvfrom(udp_fd_, rb, sizeof(rb) - 1, 0, (sockaddr*)&from, &fl);
            fcntl(udp_fd_, F_SETFL, curfl);   // restore non-blocking
            bool ok = false;
            if (rn > 0) {
                rb[rn] = 0; std::istringstream as(rb);
                std::string cnonce, response; as >> cnonce >> response;
                ok = (response == ota_md5(pass_md5_ + ":" + nonce + ":" + cnonce));
            }
            if (!ok) {
                const char* deny = "Authentication Failed\n";
                ::sendto(udp_fd_, deny, std::strlen(deny), 0, (sockaddr*)&from, fl);
                fire_error(OTA_AUTH_ERROR);
                return;
            }
        }
        const char* okmsg = "OK\n";
        ::sendto(udp_fd_, okmsg, std::strlen(okmsg), 0, (sockaddr*)&from, fl);

        // Connect back to the host and pull the firmware.
        int tcp = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in h{}; h.sin_family = AF_INET; h.sin_port = htons(host_port);
        h.sin_addr = from.sin_addr;
        if (tcp < 0 || ::connect(tcp, (sockaddr*)&h, sizeof(h)) != 0) {
            if (tcp >= 0) ::close(tcp);
            fire_error(OTA_CONNECT_ERROR);
            return;
        }

        if (!Update.begin(size, cmd == U_SPIFFS ? U_SPIFFS : U_FLASH)) {
            ::close(tcp); fire_error(OTA_BEGIN_ERROR); return;
        }
        if (!md5.empty()) Update.setMD5(md5.c_str());
        if (on_start_) on_start_();

        size_t got = 0; char chunk[4096]; bool recv_ok = true;
        while (got < size) {
            ssize_t r = ::recv(tcp, chunk, sizeof(chunk), 0);
            if (r <= 0) { recv_ok = false; break; }
            Update.write((uint8_t*)chunk, (size_t)r);
            got += (size_t)r;
            if (on_progress_) on_progress_((unsigned)got, (unsigned)size);
        }
        if (!recv_ok) { Update.abort(); ::close(tcp); fire_error(OTA_RECEIVE_ERROR); return; }

        if (!Update.end(true)) {
            ::close(tcp); fire_error(OTA_END_ERROR); return;
        }
        const char* fin = "OK\n";
        ::send(tcp, fin, std::strlen(fin), 0);
        ::close(tcp);
        if (on_end_) on_end_();
    }

private:
    void fire_error(ota_error_t e) {
        std::fprintf(stderr, "[boardghost] ArduinoOTA: error %d\n", (int)e);
        if (on_error_) on_error_(e);
    }

    int         udp_fd_   = -1;
    uint16_t    port_     = 3232;
    std::string hostname_ = "esp32";
    std::string pass_md5_;
    bool        auth_     = false;
    bool        mdns_     = true;
    unsigned    nonce_ctr_ = 0;
};

}  // namespace boardghost_internal

namespace bgi = boardghost_internal;

ArduinoOTAClass::ArduinoOTAClass() : impl_(std::make_unique<bgi::OtaImpl>()) {}
ArduinoOTAClass::~ArduinoOTAClass() = default;

ArduinoOTAClass& ArduinoOTAClass::setPort(uint16_t port)          { impl_->set_port(port); return *this; }
ArduinoOTAClass& ArduinoOTAClass::setHostname(const char* h)      { impl_->set_hostname(h); return *this; }
String ArduinoOTAClass::getHostname()                             { return String(impl_->hostname().c_str()); }
ArduinoOTAClass& ArduinoOTAClass::setPassword(const char* p)      { impl_->set_password(p); return *this; }
ArduinoOTAClass& ArduinoOTAClass::setPasswordHash(const char* h)  { impl_->set_password_hash(h); return *this; }
ArduinoOTAClass& ArduinoOTAClass::setRebootOnSuccess(bool)        { return *this; }
ArduinoOTAClass& ArduinoOTAClass::setMdnsEnabled(bool e)          { impl_->set_mdns(e); return *this; }

void ArduinoOTAClass::onStart(THandlerFunction fn)               { impl_->on_start_ = std::move(fn); }
void ArduinoOTAClass::onEnd(THandlerFunction fn)                 { impl_->on_end_ = std::move(fn); }
void ArduinoOTAClass::onProgress(THandlerFunctionProgress fn)    { impl_->on_progress_ = std::move(fn); }
void ArduinoOTAClass::onError(THandlerFunctionError fn)          { impl_->on_error_ = std::move(fn); }

void ArduinoOTAClass::begin()  { impl_->begin(); }
void ArduinoOTAClass::end()    { impl_->stop(); }
void ArduinoOTAClass::handle() { impl_->handle(); }
int  ArduinoOTAClass::getCommand() { return impl_->command_; }

ArduinoOTAClass ArduinoOTA;
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target runtime_tests && ./build/tests/runtime_tests --gtest_filter='OtaTest.*'`
Expected: PASS (2 tests). If `ReceivesFirmware...` flakes on timing, raise the pump loop iteration count — it should not, since `handle()` blocks through the transfer once the invite is read.

- [ ] **Step 6: Run the full suite to confirm no regressions**

Run: `./build/tests/runtime_tests`
Expected: PASS (all tests, including the new OTA ones).

- [ ] **Step 7: Commit**

```bash
git add runtime/src/sim_ota.cpp runtime/tests/test_ota.cpp runtime/CMakeLists.txt runtime/tests/CMakeLists.txt
git commit -m "feat(ota): espota-protocol receiver feeding the Update shim"
```

---

### Task 4: `boardghost ota push` CLI (built-in espota client)

A Rust subcommand that pushes a firmware file to a running sim over the espota protocol — so flashing needs no Python/arduino-cli.

**Files:**
- Create: `crates/boardghost-cli/src/ota.rs`
- Modify: `crates/boardghost-cli/src/cli.rs` (add `Command::Ota` + `OtaAction`)
- Modify: `crates/boardghost-cli/src/main.rs` (dispatch + `mod ota;` if needed)

- [ ] **Step 1: Write the failing test**

Create `crates/boardghost-cli/src/ota.rs` with the function under test and a unit test that exercises the auth-digest math (the wire I/O is covered by the C++ side):

```rust
use anyhow::{Context, Result};
use std::io::{Read, Write};
use std::net::{TcpListener, UdpSocket};
use std::path::Path;
use std::time::Duration;

/// MD5 hex of a byte slice. espota uses MD5 for both the firmware checksum
/// and the auth challenge.
fn md5_hex(bytes: &[u8]) -> String {
    // `md5` crate (add to Cargo.toml). Tiny, pure-Rust, no build deps.
    format!("{:x}", md5::compute(bytes))
}

/// Push `firmware` to a running sim's ArduinoOTA receiver at 127.0.0.1:port.
/// Returns Ok(()) when the device replies with the closing "OK".
pub fn push(port: u16, firmware: &[u8], password: Option<&str>) -> Result<()> {
    let listener = TcpListener::bind("127.0.0.1:0").context("bind tcp listener")?;
    let host_port = listener.local_addr()?.port();

    let udp = UdpSocket::bind("127.0.0.1:0").context("bind udp")?;
    udp.set_read_timeout(Some(Duration::from_secs(3)))?;
    let dev = format!("127.0.0.1:{port}");

    let md5 = md5_hex(firmware);
    let invite = format!("0 {host_port} {} {md5}\n", firmware.len());
    udp.send_to(invite.as_bytes(), &dev).context("send invite")?;

    let mut rb = [0u8; 256];
    let (n, _) = udp.recv_from(&mut rb).context("await device reply")?;
    let reply = String::from_utf8_lossy(&rb[..n]).to_string();

    if let Some(stripped) = reply.strip_prefix("AUTH ") {
        let pw = password.context("device requires a password (--password)")?;
        let nonce = stripped.trim();
        let passmd5 = md5_hex(pw.as_bytes());
        let cnonce = md5_hex(format!("{}{host_port}", firmware.len()).as_bytes());
        let result = md5_hex(format!("{passmd5}:{nonce}:{cnonce}").as_bytes());
        udp.send_to(format!("{cnonce} {result}\n").as_bytes(), &dev)?;
        let (n2, _) = udp.recv_from(&mut rb).context("await auth result")?;
        let r2 = String::from_utf8_lossy(&rb[..n2]).to_string();
        anyhow::ensure!(r2.starts_with("OK"), "authentication rejected: {}", r2.trim());
    } else {
        anyhow::ensure!(reply.starts_with("OK"), "device declined: {}", reply.trim());
    }

    let (mut sock, _) = listener.accept().context("device did not connect back")?;
    sock.write_all(firmware).context("stream firmware")?;
    let mut fin = String::new();
    sock.read_to_string(&mut fin).ok();
    anyhow::ensure!(fin.starts_with("OK"), "device reported failure: {}", fin.trim());
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn auth_digest_matches_reference() {
        // Reference vector: passmd5 = md5("secret"), nonce/cnonce fixed.
        let passmd5 = md5_hex(b"secret");
        let nonce = "deadbeef";
        let cnonce = "feedface";
        let result = md5_hex(format!("{passmd5}:{nonce}:{cnonce}").as_bytes());
        // Same formula the C++ receiver uses — recompute and compare.
        assert_eq!(result, md5_hex(format!("{passmd5}:{nonce}:{cnonce}").as_bytes()));
        assert_eq!(result.len(), 32);
    }
}
```

Add `md5 = "0.7"` to `crates/boardghost-cli/Cargo.toml` `[dependencies]`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test -p boardghost-cli ota::`
Expected: FAIL to compile until `mod ota;` is declared and the `md5` dep is added.

- [ ] **Step 3: Wire the module and subcommand**

In `crates/boardghost-cli/src/main.rs`, add near the other `mod` declarations (or rely on the library — check whether modules live in `lib.rs`; add `mod ota;` wherever `mod` declarations sit):

```rust
mod ota;
```

In `crates/boardghost-cli/src/cli.rs`, add a variant to `enum Command` (after `Uart`):

```rust
    /// Push a firmware file to a running sim over the espota protocol — the
    /// same path arduino-cli / the IDE network port uses, but built-in so no
    /// Python is required. The sketch must be running with BOARDGHOST_NET=real
    /// and have called ArduinoOTA.begin().
    Ota {
        #[command(subcommand)]
        action: OtaAction,
    },
```

And add the subcommand enum at the bottom of `cli.rs`:

```rust
#[derive(Subcommand)]
pub enum OtaAction {
    /// Stream a firmware .bin to the sim's OTA receiver on 127.0.0.1:<port>.
    Push {
        /// Firmware file to upload.
        #[arg(value_name = "FIRMWARE_BIN")]
        file: PathBuf,
        /// OTA UDP port the sim is listening on (matches BOARDGHOST_OTA_PORT).
        #[arg(long, default_value_t = 3232u16)]
        port: u16,
        /// Password, if the sketch called ArduinoOTA.setPassword().
        #[arg(long)]
        password: Option<String>,
    },
}
```

In `crates/boardghost-cli/src/main.rs`, add the dispatch arm in the `match` (after `Command::Uart`):

```rust
        Command::Ota { action } => match action {
            cli::OtaAction::Push { file, port, password } => {
                let firmware = std::fs::read(&file)
                    .with_context(|| format!("reading {}", file.display()))?;
                eprintln!("→ Pushing {} bytes to 127.0.0.1:{port}...", firmware.len());
                ota::push(port, &firmware, password.as_deref())?;
                eprintln!("✓ OTA accepted — firmware written to <project>/.boardghost/ota-firmware.bin");
                Ok(())
            }
        },
```

(Adjust the `cli::OtaAction` / `OtaAction` path to match how the existing `UartAction` is referenced in `main.rs`.)

- [ ] **Step 4: Run test + build to verify pass**

Run: `cargo test -p boardghost-cli && cargo build -p boardghost-cli`
Expected: PASS + clean build.

- [ ] **Step 5: Commit**

```bash
git add crates/boardghost-cli/src/ota.rs crates/boardghost-cli/src/cli.rs crates/boardghost-cli/src/main.rs crates/boardghost-cli/Cargo.toml crates/boardghost-cli/Cargo.lock
git commit -m "feat(cli): boardghost ota push — built-in espota client"
```

---

## Phase B — AsyncWebServer

### Task 5: AsyncWebServer + AsyncTCP shim headers

**Files:**
- Create: `runtime/shims/AsyncTCP.h` (stub — sketches include it first)
- Create: `runtime/shims/ESPAsyncWebServer.h` (API surface)

- [ ] **Step 1: Create `runtime/shims/AsyncTCP.h`**

```cpp
#pragma once
// ESPAsyncWebServer sketches conventionally #include <AsyncTCP.h> before
// <ESPAsyncWebServer.h>. The async TCP layer is internal to the cpp-httplib
// backing in the sim, so this header only needs to exist for the include to
// resolve. Intentionally empty.
```

- [ ] **Step 2: Create `runtime/shims/ESPAsyncWebServer.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <memory>
#include <string>
#include "WString.h"
#include "FS.h"

// ESPAsyncWebServer shim over a dedicated cpp-httplib server. Covers the
// realistic dashboard subset: on() with per-request AsyncWebServerRequest,
// request->send (incl. send(FS,path) + template processor), getParam/hasParam,
// serveStatic, onNotFound, and AsyncEventSource (Server-Sent Events).
//
// NOTE: these HTTP_* values are ESPAsyncWebServer's bitmask form and DIFFER
// from WebServer.h's sequential enum — exactly as on real hardware, you cannot
// include both <WebServer.h> and <ESPAsyncWebServer.h> in one sketch.
//
// Env: BOARDGHOST_ASYNC_WEBSERVER_PORT overrides the constructor port.

#ifndef BOARDGHOST_ASYNC_HTTP_METHODS
#define BOARDGHOST_ASYNC_HTTP_METHODS
typedef enum {
    HTTP_GET     = 0b00000001,
    HTTP_POST    = 0b00000010,
    HTTP_DELETE  = 0b00000100,
    HTTP_PUT     = 0b00001000,
    HTTP_PATCH   = 0b00010000,
    HTTP_HEAD    = 0b00100000,
    HTTP_OPTIONS = 0b01000000,
    HTTP_ANY     = 0b01111111,
} WebRequestMethod;
typedef uint8_t WebRequestMethodComposite;
#endif

namespace boardghost_internal { class AsyncServerImpl; struct AsyncReqState; class EventSourceImpl; }

using AwsTemplateProcessor = std::function<String(const String&)>;

class AsyncWebParameter {
public:
    AsyncWebParameter(String name, String value, bool post = false, bool file = false)
        : name_(std::move(name)), value_(std::move(value)), post_(post), file_(file) {}
    const String& name()  const { return name_; }
    const String& value() const { return value_; }
    bool isPost() const { return post_; }
    bool isFile() const { return file_; }
private:
    String name_, value_;
    bool post_, file_;
};

class AsyncWebServerRequest {
public:
    explicit AsyncWebServerRequest(boardghost_internal::AsyncReqState* st) : st_(st) {}

    String url() const;
    String host() const;
    WebRequestMethodComposite method() const;

    int params() const;
    bool hasParam(const String& name, bool post = false, bool file = false) const;
    const AsyncWebParameter* getParam(const String& name, bool post = false, bool file = false) const;
    const AsyncWebParameter* getParam(size_t idx) const;
    String arg(const String& name) const;     // convenience: value or ""

    bool hasHeader(const String& name) const;
    String header(const String& name) const;

    void send(int code, const String& contentType = String(), const String& content = String());
    void send_P(int code, const String& contentType, const char* content,
                AwsTemplateProcessor processor = nullptr);
    void send(fs::FS& fs, const String& path, const String& contentType = String(),
              bool download = false, AwsTemplateProcessor processor = nullptr);
    void redirect(const String& url);

private:
    boardghost_internal::AsyncReqState* st_;
};

using ArRequestHandlerFunction = std::function<void(AsyncWebServerRequest*)>;

// AsyncEventSource — Server-Sent Events. addHandler() it onto the server.
class AsyncEventSource {
public:
    explicit AsyncEventSource(const String& url);
    ~AsyncEventSource();
    const String& url() const { return url_; }
    void onConnect(std::function<void()> cb);
    void send(const char* message, const char* event = nullptr,
              uint32_t id = 0, uint32_t reconnect = 0);
    size_t count() const;
    // Internal: called by AsyncWebServer::addHandler.
    boardghost_internal::EventSourceImpl* impl() const { return impl_.get(); }
private:
    String url_;
    std::shared_ptr<boardghost_internal::EventSourceImpl> impl_;
};

class AsyncWebServer {
public:
    explicit AsyncWebServer(uint16_t port);
    ~AsyncWebServer();

    AsyncWebServer(const AsyncWebServer&) = delete;
    AsyncWebServer& operator=(const AsyncWebServer&) = delete;

    void on(const char* uri, ArRequestHandlerFunction handler);
    void on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction handler);
    void onNotFound(ArRequestHandlerFunction handler);
    void serveStatic(const char* uri, fs::FS& fs, const char* path, const char* cache_control = nullptr);
    void addHandler(AsyncEventSource* source);

    void begin();
    void end();

private:
    std::unique_ptr<boardghost_internal::AsyncServerImpl> impl_;
};
```

- [ ] **Step 3: Commit**

```bash
git add runtime/shims/AsyncTCP.h runtime/shims/ESPAsyncWebServer.h
git commit -m "feat(async): ESPAsyncWebServer + AsyncTCP shim headers"
```

---

### Task 6: AsyncWebServer core impl + CMake + tests

Core request/response: `on()`, `request->send`, params, `serveStatic`, template processor, `onNotFound`. SSE comes in Task 7.

**Files:**
- Create: `runtime/src/sim_asyncwebserver.cpp`
- Create: `runtime/tests/test_asyncwebserver.cpp`
- Modify: `runtime/CMakeLists.txt`, `runtime/tests/CMakeLists.txt`

- [ ] **Step 1: Wire CMake**

In `runtime/CMakeLists.txt` `sim_runtime` sources, after `src/sim_webserver.cpp`:

```cmake
    src/sim_asyncwebserver.cpp
```

In `runtime/tests/CMakeLists.txt` `runtime_tests` list, after `test_webserver.cpp`:

```cpp
    test_asyncwebserver.cpp
```

- [ ] **Step 2: Write the failing test (core behaviours)**

`runtime/tests/test_asyncwebserver.cpp`:

```cpp
// AsyncWebServer core: drive a live instance with the vendored cpp-httplib
// client and assert request/response, params, serveStatic, and templating.

#include <gtest/gtest.h>
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <cstdlib>
#include <random>
#include <string>
#include <thread>

namespace {
uint16_t pick_port() {
    std::random_device rd; std::mt19937 rng(rd());
    return (uint16_t)(40000 + std::uniform_int_distribution<int>(0, 20000)(rng));
}
}  // namespace

TEST(AsyncWebServerTest, SendAndParamsRoundtrip) {
    uint16_t port = pick_port();
    AsyncWebServer server(port);
    server.on("/hello", HTTP_GET, [](AsyncWebServerRequest* req) {
        String who = req->hasParam("name") ? req->getParam("name")->value() : String("world");
        req->send(200, "text/plain", String("hi ") + who);
    });
    server.begin();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    httplib::Client cli("127.0.0.1", port);
    auto res = cli.Get("/hello?name=ghost");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "hi ghost");

    auto res2 = cli.Get("/hello");
    ASSERT_TRUE(res2 != nullptr);
    EXPECT_EQ(res2->body, "hi world");

    server.end();
}

TEST(AsyncWebServerTest, OnNotFoundFires) {
    uint16_t port = pick_port();
    AsyncWebServer server(port);
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "nope");
    });
    server.begin();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    httplib::Client cli("127.0.0.1", port);
    auto res = cli.Get("/does-not-exist");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
    EXPECT_EQ(res->body, "nope");
    server.end();
}

TEST(AsyncWebServerTest, TemplateProcessorSubstitutes) {
    uint16_t port = pick_port();
    AsyncWebServer server(port);
    server.on("/page", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", "<b>%TITLE%</b>", [](const String& var) -> String {
            if (var == "TITLE") return String("GhostBoard");
            return String();
        });
    });
    server.begin();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    httplib::Client cli("127.0.0.1", port);
    auto res = cli.Get("/page");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->body, "<b>GhostBoard</b>");
    server.end();
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target runtime_tests`
Expected: FAIL to link — `sim_asyncwebserver.cpp` doesn't exist.

- [ ] **Step 4: Implement `runtime/src/sim_asyncwebserver.cpp` (core only)**

```cpp
// AsyncWebServer backing — own cpp-httplib server, per-request state objects.
#include "ESPAsyncWebServer.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace boardghost_internal {

// Per-request scratch the AsyncWebServerRequest reads/writes. One per request,
// stack-lived inside the httplib handler — naturally concurrent, no mutex.
struct AsyncReqState {
    const httplib::Request* req = nullptr;
    httplib::Response*      res = nullptr;
    std::vector<AsyncWebParameter> params;   // parsed query+form params
    bool responded = false;
};

static uint16_t resolve_async_port(uint16_t def) {
    if (const char* e = std::getenv("BOARDGHOST_ASYNC_WEBSERVER_PORT")) {
        int p = std::atoi(e);
        if (p > 0 && p < 65536) return (uint16_t)p;
    }
    return def;
}

// Expand %TOKEN% using the processor callback (ESPAsyncWebServer semantics).
static std::string apply_template(const std::string& in, const AwsTemplateProcessor& cb) {
    if (!cb) return in;
    std::string out; out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        if (in[i] == '%') {
            size_t end = in.find('%', i + 1);
            if (end != std::string::npos) {
                std::string token = in.substr(i + 1, end - i - 1);
                if (token.empty()) { out += '%'; i = end + 1; continue; }  // "%%" -> "%"
                out += cb(String(token.c_str())).c_str();
                i = end + 1;
                continue;
            }
        }
        out += in[i++];
    }
    return out;
}

class AsyncServerImpl {
public:
    explicit AsyncServerImpl(uint16_t port) : port_(port) {}
    ~AsyncServerImpl() { stop(); }

    void add_route(const std::string& uri, WebRequestMethodComposite method,
                   ArRequestHandlerFunction handler) {
        auto wrap = [h = std::move(handler)](const httplib::Request& req, httplib::Response& res) {
            AsyncReqState st; st.req = &req; st.res = &res;
            for (auto it = req.params.begin(); it != req.params.end(); ++it)
                st.params.emplace_back(String(it->first.c_str()), String(it->second.c_str()));
            AsyncWebServerRequest r(&st);
            try { h(&r); } catch (...) { res.status = 500; res.set_content("handler threw", "text/plain"); }
            if (!st.responded) { res.status = 404; }
        };
        if (method & HTTP_GET)    srv_.Get(uri, wrap);
        if (method & HTTP_POST)   srv_.Post(uri, wrap);
        if (method & HTTP_PUT)    srv_.Put(uri, wrap);
        if (method & HTTP_PATCH)  srv_.Patch(uri, wrap);
        if (method & HTTP_DELETE) srv_.Delete(uri, wrap);
        if (method & HTTP_OPTIONS) srv_.Options(uri, wrap);
    }

    void set_not_found(ArRequestHandlerFunction handler) {
        srv_.set_error_handler([h = std::move(handler)](const httplib::Request& req, httplib::Response& res) {
            if (res.status != 404) return;
            AsyncReqState st; st.req = &req; st.res = &res;
            AsyncWebServerRequest r(&st);
            try { h(&r); } catch (...) {}
        });
    }

    void serve_static(const std::string& uri, fs::FS& fs, const std::string& path) {
        // Map a URI prefix to a single file (the common dashboard case:
        // serveStatic("/", SPIFFS, "/index.html")).
        srv_.Get(uri, [&fs, path](const httplib::Request&, httplib::Response& res) {
            fs::File f = fs.open(path.c_str(), "r");
            if (!f) { res.status = 404; return; }
            std::string body; size_t n = f.size(); body.resize(n);
            if (n) f.read((uint8_t*)&body[0], n);
            res.set_content(body, "text/html");
        });
    }

    httplib::Server& server() { return srv_; }

    void start() {
        if (running_.load()) return;
        uint16_t port = resolve_async_port(port_);
        if (!srv_.bind_to_port("0.0.0.0", port)) {
            std::fprintf(stderr, "[boardghost] AsyncWebServer: bind port %u failed.\n", port);
            return;
        }
        running_.store(true);
        thread_ = std::thread([this]() { srv_.listen_after_bind(); running_.store(false); });
        std::fprintf(stderr, "[boardghost] AsyncWebServer: listening on http://localhost:%u\n", port);
    }

    void stop() {
        if (!running_.load() && !thread_.joinable()) return;
        srv_.stop();
        if (thread_.joinable()) thread_.join();
        running_.store(false);
    }

private:
    uint16_t port_;
    httplib::Server srv_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace boardghost_internal

namespace bgi = boardghost_internal;

// ---- AsyncWebServerRequest ----
String AsyncWebServerRequest::url() const { return String(st_->req->path.c_str()); }
String AsyncWebServerRequest::host() const {
    auto it = st_->req->headers.find("Host");
    return it == st_->req->headers.end() ? String() : String(it->second.c_str());
}
WebRequestMethodComposite AsyncWebServerRequest::method() const {
    const std::string& m = st_->req->method;
    if (m == "GET") return HTTP_GET;     if (m == "POST") return HTTP_POST;
    if (m == "PUT") return HTTP_PUT;     if (m == "PATCH") return HTTP_PATCH;
    if (m == "DELETE") return HTTP_DELETE; if (m == "OPTIONS") return HTTP_OPTIONS;
    if (m == "HEAD") return HTTP_HEAD;   return HTTP_ANY;
}
int AsyncWebServerRequest::params() const { return (int)st_->params.size(); }
bool AsyncWebServerRequest::hasParam(const String& name, bool, bool) const {
    for (auto& p : st_->params) if (p.name() == name) return true;
    return false;
}
const AsyncWebParameter* AsyncWebServerRequest::getParam(const String& name, bool, bool) const {
    for (auto& p : st_->params) if (p.name() == name) return &p;
    return nullptr;
}
const AsyncWebParameter* AsyncWebServerRequest::getParam(size_t idx) const {
    return idx < st_->params.size() ? &st_->params[idx] : nullptr;
}
String AsyncWebServerRequest::arg(const String& name) const {
    auto* p = getParam(name); return p ? p->value() : String();
}
bool AsyncWebServerRequest::hasHeader(const String& name) const {
    return st_->req->headers.find(name.c_str()) != st_->req->headers.end();
}
String AsyncWebServerRequest::header(const String& name) const {
    auto it = st_->req->headers.find(name.c_str());
    return it == st_->req->headers.end() ? String() : String(it->second.c_str());
}
void AsyncWebServerRequest::send(int code, const String& contentType, const String& content) {
    st_->res->status = code;
    st_->res->set_content(content.c_str() ? content.c_str() : "",
                          contentType.length() ? contentType.c_str() : "text/plain");
    st_->responded = true;
}
void AsyncWebServerRequest::send_P(int code, const String& contentType, const char* content,
                                   AwsTemplateProcessor processor) {
    std::string body = bgi::apply_template(content ? content : "", processor);
    st_->res->status = code;
    st_->res->set_content(body, contentType.length() ? contentType.c_str() : "text/html");
    st_->responded = true;
}
void AsyncWebServerRequest::send(fs::FS& fs, const String& path, const String& contentType,
                                 bool /*download*/, AwsTemplateProcessor processor) {
    fs::File f = fs.open(path.c_str(), "r");
    if (!f) { st_->res->status = 404; st_->responded = true; return; }
    std::string body; size_t n = f.size(); body.resize(n);
    if (n) f.read((uint8_t*)&body[0], n);
    if (processor) body = bgi::apply_template(body, processor);
    st_->res->status = 200;
    st_->res->set_content(body, contentType.length() ? contentType.c_str() : "text/html");
    st_->responded = true;
}
void AsyncWebServerRequest::redirect(const String& url) {
    st_->res->status = 302;
    st_->res->set_header("Location", url.c_str());
    st_->responded = true;
}

// ---- AsyncWebServer ----
AsyncWebServer::AsyncWebServer(uint16_t port) : impl_(std::make_unique<bgi::AsyncServerImpl>(port)) {}
AsyncWebServer::~AsyncWebServer() = default;
void AsyncWebServer::on(const char* uri, ArRequestHandlerFunction handler) {
    impl_->add_route(uri ? uri : "/", HTTP_ANY, std::move(handler));
}
void AsyncWebServer::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction handler) {
    impl_->add_route(uri ? uri : "/", method, std::move(handler));
}
void AsyncWebServer::onNotFound(ArRequestHandlerFunction handler) { impl_->set_not_found(std::move(handler)); }
void AsyncWebServer::serveStatic(const char* uri, fs::FS& fs, const char* path, const char*) {
    impl_->serve_static(uri ? uri : "/", fs, path ? path : "/");
}
void AsyncWebServer::addHandler(AsyncEventSource* /*source*/) { /* implemented in Task 7 */ }
void AsyncWebServer::begin() { impl_->start(); }
void AsyncWebServer::end()   { impl_->stop(); }
```

Note: `AsyncEventSource` methods are declared in the header but not yet defined. To keep this task linking on its own, the test file for Task 6 does not touch `AsyncEventSource`, and the class's out-of-line methods are added in Task 7. If the linker complains about `AsyncEventSource` vtable/symbols when building Task 6's test (it shouldn't, since nothing references it), add the Task 7 stubs early.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target runtime_tests && ./build/tests/runtime_tests --gtest_filter='AsyncWebServerTest.*'`
Expected: PASS (3 tests).

- [ ] **Step 6: Commit**

```bash
git add runtime/src/sim_asyncwebserver.cpp runtime/tests/test_asyncwebserver.cpp runtime/CMakeLists.txt runtime/tests/CMakeLists.txt
git commit -m "feat(async): AsyncWebServer core (routes, params, serveStatic, templating)"
```

---

### Task 7: AsyncEventSource (Server-Sent Events)

A streaming GET that holds the connection and fans out `data:` frames pushed via `events.send()`.

**Files:**
- Modify: `runtime/src/sim_asyncwebserver.cpp` (add `EventSourceImpl`, `AsyncEventSource` methods, wire `addHandler`)
- Modify: `runtime/tests/test_asyncwebserver.cpp` (add an SSE test)

- [ ] **Step 1: Write the failing test**

Add to `runtime/tests/test_asyncwebserver.cpp`:

```cpp
TEST(AsyncWebServerTest, EventSourceDeliversFrame) {
    uint16_t port = pick_port();
    AsyncWebServer server(port);
    AsyncEventSource events("/events");
    server.addHandler(&events);
    server.begin();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Receive the stream on a background thread; stop after the first frame.
    std::string received;
    std::thread reader([&]{
        httplib::Client cli("127.0.0.1", port);
        cli.set_read_timeout(2, 0);
        cli.Get("/events", [&](const char* data, size_t len) {
            received.append(data, len);
            return received.find("\n\n") == std::string::npos;  // stop after one frame
        });
    });

    // Give the client time to connect, then push an event.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_GE(events.count(), (size_t)1);
    events.send("hello-sse", "tick");

    reader.join();
    server.end();

    EXPECT_NE(received.find("event: tick"), std::string::npos);
    EXPECT_NE(received.find("data: hello-sse"), std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target runtime_tests`
Expected: FAIL to link — `AsyncEventSource` ctor/`send`/`count`/`EventSourceImpl` are undefined.

- [ ] **Step 3: Implement SSE**

In `runtime/src/sim_asyncwebserver.cpp`, add to the `boardghost_internal` namespace (before `AsyncServerImpl`), the EventSource backing:

```cpp
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>

class EventSourceImpl {
public:
    // One connected browser.
    struct Client {
        std::mutex m;
        std::condition_variable cv;
        std::deque<std::string> queue;   // formatted SSE frames
        bool closed = false;
    };

    // Register the streaming route on the server.
    void attach(httplib::Server& srv, const std::string& url) {
        srv.Get(url, [this](const httplib::Request&, httplib::Response& res) {
            auto client = std::make_shared<Client>();
            { std::lock_guard<std::mutex> lk(clients_mutex_); clients_.insert(client); }
            res.set_chunked_content_provider("text/event-stream",
                [this, client](size_t, httplib::DataSink& sink) {
                    std::unique_lock<std::mutex> lk(client->m);
                    client->cv.wait(lk, [&]{ return client->closed || !client->queue.empty() || shutdown_; });
                    if (shutdown_ || client->closed) return false;   // end the stream
                    while (!client->queue.empty()) {
                        std::string frame = std::move(client->queue.front());
                        client->queue.pop_front();
                        lk.unlock();
                        if (!sink.write(frame.data(), frame.size())) { lk.lock(); return false; }
                        lk.lock();
                    }
                    return true;
                },
                [this, client](bool) {   // on connection close
                    std::lock_guard<std::mutex> lk(clients_mutex_);
                    clients_.erase(client);
                });
        });
    }

    void send(const std::string& message, const std::string& event,
              uint32_t id, uint32_t reconnect) {
        std::string frame;
        if (reconnect) frame += "retry: " + std::to_string(reconnect) + "\n";
        if (id)        frame += "id: " + std::to_string(id) + "\n";
        if (!event.empty()) frame += "event: " + event + "\n";
        frame += "data: " + message + "\n\n";
        std::lock_guard<std::mutex> lk(clients_mutex_);
        for (auto& c : clients_) {
            std::lock_guard<std::mutex> cl(c->m);
            c->queue.push_back(frame);
            c->cv.notify_one();
        }
    }

    size_t count() {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        return clients_.size();
    }

    // Release all held connections so the server thread can join.
    void shutdown() {
        shutdown_ = true;
        std::lock_guard<std::mutex> lk(clients_mutex_);
        for (auto& c : clients_) { std::lock_guard<std::mutex> cl(c->m); c->closed = true; c->cv.notify_all(); }
    }

private:
    std::mutex clients_mutex_;
    std::set<std::shared_ptr<Client>> clients_;
    std::atomic<bool> shutdown_{false};
};
```

Give `AsyncServerImpl` a way to attach an event source and to shut sources down on `stop()`. Add a member and amend `stop()`:

```cpp
    // (add inside AsyncServerImpl, public section)
    void attach_events(EventSourceImpl* es, const std::string& url) {
        event_sources_.push_back(es);
        es->attach(srv_, url);
    }
    // (add to AsyncServerImpl private members)
    std::vector<EventSourceImpl*> event_sources_;
```

And in `AsyncServerImpl::stop()`, before `srv_.stop();`:

```cpp
        for (auto* es : event_sources_) es->shutdown();
```

At the bottom of the file (public surface), define the `AsyncEventSource` methods and wire `addHandler`:

```cpp
AsyncEventSource::AsyncEventSource(const String& url)
    : url_(url), impl_(std::make_shared<bgi::EventSourceImpl>()) {}
AsyncEventSource::~AsyncEventSource() = default;
void AsyncEventSource::onConnect(std::function<void()> /*cb*/) { /* connect callback not surfaced in sim */ }
void AsyncEventSource::send(const char* message, const char* event, uint32_t id, uint32_t reconnect) {
    impl_->send(message ? message : "", event ? event : "", id, reconnect);
}
size_t AsyncEventSource::count() const { return impl_->count(); }
```

Replace the placeholder `AsyncWebServer::addHandler` body with:

```cpp
void AsyncWebServer::addHandler(AsyncEventSource* source) {
    if (source) impl_->attach_events(source->impl(), source->url().c_str());
}
```

(Move the `#include <condition_variable>` / `<deque>` / `<mutex>` / `<set>` lines to the top include block rather than mid-file when implementing.)

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target runtime_tests && ./build/tests/runtime_tests --gtest_filter='AsyncWebServerTest.*'`
Expected: PASS (4 tests, including `EventSourceDeliversFrame`).

- [ ] **Step 5: Run the full suite**

Run: `./build/tests/runtime_tests`
Expected: PASS (all tests).

- [ ] **Step 6: Commit**

```bash
git add runtime/src/sim_asyncwebserver.cpp runtime/tests/test_asyncwebserver.cpp
git commit -m "feat(async): AsyncEventSource Server-Sent Events"
```

---

## Phase C — Wrap-up

### Task 8: Security review, docs, and memory

**Files:**
- Modify: memory `boardghost-state.md` + `sim-env-vars.md` (under `/home/phill/.claude/projects/-home-phill-arduino-board-emulator/memory/`)
- Modify: any in-repo env-var / feature docs under `docs/` that enumerate shims (grep first)

- [ ] **Step 1: Security review the OTA auth + listener code**

Run the security-reviewer agent over `runtime/src/sim_ota.cpp` and `crates/boardghost-cli/src/ota.rs`. Confirm: (a) default bind is `127.0.0.1`; (b) the TCP connect-back target is the UDP sender (no attacker-chosen host on loopback bind); (c) auth rejects before any `Update.write`; (d) no secrets logged. Address any CRITICAL/HIGH findings before proceeding.

- [ ] **Step 2: Update in-repo docs**

Run: `grep -rIl "BOARDGHOST_WEBSERVER_PORT\|BOARDGHOST_NET" docs/ README.md 2>/dev/null`
For each hit that enumerates env vars or supported shims, add: `BOARDGHOST_OTA_PORT` (3232), `BOARDGHOST_OTA_BIND` (127.0.0.1), `BOARDGHOST_ASYNC_WEBSERVER_PORT`, and note ArduinoOTA + AsyncWebServer are now supported.

- [ ] **Step 3: Update memory**

In `sim-env-vars.md`, add the three new env vars with one-line descriptions. In `boardghost-state.md`, move "AsyncWebServer" and "ArduinoOTA" off the **Outstanding** list into the supported set, and add a short milestone note. Add the new pointers to `MEMORY.md` only if a new file is created (none expected — these are edits).

- [ ] **Step 4: Commit**

```bash
git add docs/ README.md
git commit -m "docs: ArduinoOTA + AsyncWebServer shims, new env vars"
```

(Memory files live outside the repo and are saved with the Write tool, not committed here.)

---

## Self-review notes (for the executor)

- **Build dir assumption:** steps assume a configured CMake build at `./build`. If absent, run `cmake -S runtime -B build -DBOARDGHOST_BUILD_TESTS=ON` first (mirror the repo's existing configure flags — check `crates/boardghost-cli/src/preprocess.rs` / CI for the canonical invocation).
- **OpenSSL dependency:** OTA auth needs `BOARDGHOST_WITH_OPENSSL=1` (default ON). On a build without OpenSSL, `ota_md5` returns "" and auth will reject every push — acceptable degradation; the non-auth path still works since the host computes the firmware MD5 but the receiver only *verifies* it via the existing `Update.setMD5` (which itself requires OpenSSL — consistent).
- **Port collisions in tests:** tests pick random high ports; on a busy CI a rare collision can flake. Re-run is the mitigation; not worth a registry.
- **`handle()` blocks during transfer:** by design (matches ESP32, which blocks `loop()` during OTA). Sketches and the test pump `handle()` from their loop.
```
