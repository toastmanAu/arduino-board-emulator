# BoardGhost M2.B — IoT Library Stubs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stub the common ESP32/Arduino IoT libraries (WiFi, HTTPClient, WiFiClientSecure, EEPROM, FS/SPIFFS/LittleFS/SD, TinyGsmClient, StreamDebugger) so real-world sketches compile and run on BoardGhost without `#ifndef BOARDGHOST_SIM` wrapping — turning the typical 40-line port cost from M2.A's first-contact test into 0 lines.

**Architecture:** Add per-API header+impl pairs to `runtime/shims/` and `runtime/src/`. Network behaviour is configurable via a single env var `BOARDGHOST_NET=fake|fail|real` (default `fake`). `real` mode shells out to libcurl when available at build time. Filesystem APIs map paths into `./sim-assets/` next to the sketch — users drop real JPGs/JSON/configs there and `SPIFFS.open("/main.jpg")` opens `./sim-assets/main.jpg`. EEPROM persists to `./.boardghost/eeprom.bin` for cross-run state.

**Tech Stack:** Same as M1/M2.A — C++17 runtime, Rust CLI/launcher. Adds optional libcurl4-openssl-dev for `BOARDGHOST_NET=real`.

**Predecessor plan / tag:** `m2a-displays-gpio-touch-screenshot`.

---

## Design contract — read first

Three rules every shim follows:

1. **Compile-time compatibility, opt-in semantic fidelity.** Every shimmed API exposes the full surface the production library exposes. Method bodies do the minimum to be useful — usually return success / empty data — unless the user opts into something realer via env var.

2. **Files in `./sim-assets/`, state in `./.boardghost/`.** Sketches reference paths like `"/main.jpg"`; we map them to `<project-dir>/sim-assets/main.jpg`. EEPROM, NVS, and any other persistent state live under `<project-dir>/.boardghost/`. Both are gitignorable per-project.

3. **Network is configured by `BOARDGHOST_NET`.** Three modes:
   - `fake` (default): `WiFi.begin()` returns `WL_CONNECTED` immediately. `HTTPClient.GET()` returns 200 with empty body.
   - `fail`: `WiFi.begin()` returns `WL_NO_SSID_AVAIL`. `HTTPClient.GET()` returns -1 (HTTPC_ERROR_CONNECTION_REFUSED).
   - `real`: passes through to libcurl. Only available when the runtime is built with `BOARDGHOST_WITH_CURL=ON` (default ON when libcurl4-openssl-dev is detected by CMake). Falls back to `fake` with a stderr warning otherwise.

---

## File structure produced by this plan

```
arduino-board-emulator/
├── runtime/
│   ├── include/
│   │   └── sim_net.h                  (new) — BOARDGHOST_NET enum + accessors
│   ├── shims/
│   │   ├── WiFi.h                     (new)
│   │   ├── WiFiClient.h               (new)
│   │   ├── WiFiClientSecure.h         (new)
│   │   ├── HTTPClient.h               (new)
│   │   ├── EEPROM.h                   (new)
│   │   ├── FS.h                       (new) — base File + FS class
│   │   ├── SPIFFS.h                   (new) — extern SPIFFS instance
│   │   ├── LittleFS.h                 (new) — extern LittleFS instance
│   │   ├── SD.h                       (new) — extern SD instance
│   │   ├── TinyGsmClient.h            (new) — minimal stub
│   │   └── StreamDebugger.h           (new) — minimal stub
│   ├── src/
│   │   ├── sim_wifi.cpp               (new)
│   │   ├── sim_http.cpp               (new)
│   │   ├── sim_eeprom.cpp             (new)
│   │   └── sim_fs.cpp                 (new)
│   ├── CMakeLists.txt                 modified — add new sources, optional libcurl detect
│   └── tests/
│       ├── test_wifi.cpp              (new)
│       ├── test_http.cpp              (new)
│       ├── test_eeprom.cpp            (new)
│       └── test_fs.cpp                (new)
├── crates/boardghost-cli/src/
│   └── libraries.rs                   modified — broaden allowlist
├── examples/
│   └── (no new examples — t-simLovyan original sketch is the real test)
├── README.md                          modified — document stubs + env vars
├── docs/getting-started.md            modified — assets dir, eeprom file
└── .github/workflows/ci.yml           modified — install libcurl4-openssl-dev
```

---

## Task 1: Net dispatch infrastructure

Lay the foundation: a small header that other shims call to read `BOARDGHOST_NET` once and expose a typed enum.

**Files:**
- Create: `runtime/include/sim_net.h`
- Create: `runtime/src/sim_net.cpp`
- Modify: `runtime/CMakeLists.txt`
- Create: `runtime/tests/test_net_mode.cpp`
- Modify: `runtime/tests/CMakeLists.txt`

- [ ] **Step 1: Write `sim_net.h`**

```c
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARDGHOST_NET_FAKE = 0,
    BOARDGHOST_NET_FAIL = 1,
    BOARDGHOST_NET_REAL = 2,
} boardghost_net_mode_t;

// Returns the current network mode. Reads BOARDGHOST_NET env var on first
// call and caches. Defaults to FAKE. Unknown values fall back to FAKE
// with a stderr warning.
boardghost_net_mode_t sim_net_mode(void);

// Returns 1 iff this build supports BOARDGHOST_NET=real (i.e. libcurl
// was found at configure time). Always 0 if BOARDGHOST_WITH_CURL=OFF.
int sim_net_real_supported(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Write `sim_net.cpp`**

```cpp
#include "sim_net.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" boardghost_net_mode_t sim_net_mode(void) {
    static boardghost_net_mode_t cached = (boardghost_net_mode_t)-1;
    if (cached != (boardghost_net_mode_t)-1) return cached;

    const char* v = std::getenv("BOARDGHOST_NET");
    if (!v || !*v) {
        cached = BOARDGHOST_NET_FAKE;
        return cached;
    }
    if (std::strcmp(v, "fake") == 0) cached = BOARDGHOST_NET_FAKE;
    else if (std::strcmp(v, "fail") == 0) cached = BOARDGHOST_NET_FAIL;
    else if (std::strcmp(v, "real") == 0) {
        if (sim_net_real_supported()) {
            cached = BOARDGHOST_NET_REAL;
        } else {
            std::fprintf(stderr,
                "[boardghost] BOARDGHOST_NET=real requested but this build "
                "has no libcurl; falling back to fake\n");
            cached = BOARDGHOST_NET_FAKE;
        }
    } else {
        std::fprintf(stderr,
            "[boardghost] BOARDGHOST_NET=%s unrecognised; using fake\n", v);
        cached = BOARDGHOST_NET_FAKE;
    }
    return cached;
}

extern "C" int sim_net_real_supported(void) {
#ifdef BOARDGHOST_WITH_CURL
    return 1;
#else
    return 0;
#endif
}
```

- [ ] **Step 3: Detect libcurl in `runtime/CMakeLists.txt`**

Append after the existing SDL2 find_package block:

```cmake
# Optional libcurl for BOARDGHOST_NET=real network mode.
option(BOARDGHOST_WITH_CURL "Enable real-network mode for WiFi/HTTPClient shims" ON)
if(BOARDGHOST_WITH_CURL)
    find_package(CURL QUIET)
    if(CURL_FOUND)
        target_link_libraries(sim_runtime PUBLIC CURL::libcurl)
        target_compile_definitions(sim_runtime PUBLIC BOARDGHOST_WITH_CURL=1)
        message(STATUS "BoardGhost: libcurl ${CURL_VERSION_STRING} found — real-net mode enabled")
    else()
        set(BOARDGHOST_WITH_CURL OFF CACHE BOOL "" FORCE)
        message(STATUS "BoardGhost: libcurl not found — real-net mode disabled (BOARDGHOST_NET=real will fall back to fake)")
    endif()
endif()
```

Add `src/sim_net.cpp` to the `add_library(sim_runtime STATIC …)` source list.

- [ ] **Step 4: Write the test**

`runtime/tests/test_net_mode.cpp`:

```cpp
#include <gtest/gtest.h>
#include "sim_net.h"
#include <cstdlib>

// sim_net_mode caches on first read. To test all branches in one binary
// we'd need to expose a reset hook — but tests run in independent gtest
// processes via gtest_discover_tests, so the cache is fresh per process.
// We exercise only one mode per test process via TEST_F + setenv before
// any call.

TEST(NetMode, RealSupportedReturnsZeroOrOne) {
    int r = sim_net_real_supported();
    EXPECT_TRUE(r == 0 || r == 1);
}

TEST(NetMode, DefaultsToFake) {
    unsetenv("BOARDGHOST_NET");
    EXPECT_EQ(sim_net_mode(), BOARDGHOST_NET_FAKE);
}
```

> Note: because `sim_net_mode` caches, only one assertion about the mode is reliable per gtest process. `gtest_discover_tests` runs each TEST as a separate process by default in recent CMake configurations — verify this is the case in the existing setup. If tests share a process, split into multiple test binaries.

- [ ] **Step 5: Register the test**

Add `test_net_mode.cpp` to `runtime/tests/CMakeLists.txt`.

- [ ] **Step 6: Build + run**

```bash
cmake -S runtime -B runtime/build 2>&1 | grep -E "BoardGhost:|CURL"
cmake --build runtime/build -j 2>&1 | tail -3
ctest --test-dir runtime/build 2>&1 | tail -3
```

Expected: status line says whether libcurl was found. 39 tests pass (37 + 2 new).

If libcurl not installed: install via `sudo apt install libcurl4-openssl-dev` on Ubuntu. Document in README.

- [ ] **Step 7: Commit**

```bash
git add runtime/include/sim_net.h runtime/src/sim_net.cpp runtime/CMakeLists.txt \
        runtime/tests/test_net_mode.cpp runtime/tests/CMakeLists.txt
git commit -m "feat(runtime): sim_net mode dispatch (BOARDGHOST_NET=fake|fail|real)"
```

---

## Task 2: WiFi + WiFiClient shims

**Files:**
- Create: `runtime/shims/WiFi.h`
- Create: `runtime/shims/WiFiClient.h`
- Create: `runtime/src/sim_wifi.cpp`
- Create: `runtime/tests/test_wifi.cpp`
- Modify: `runtime/CMakeLists.txt`
- Modify: `runtime/tests/CMakeLists.txt`

- [ ] **Step 1: Write `runtime/shims/WiFi.h`**

```cpp
#pragma once
#include <stdint.h>
#include "WString.h"
#include "WiFiClient.h"

// wl_status_t enum (matches ESP32 Arduino core).
typedef enum {
    WL_NO_SHIELD       = 255,
    WL_IDLE_STATUS     = 0,
    WL_NO_SSID_AVAIL   = 1,
    WL_SCAN_COMPLETED  = 2,
    WL_CONNECTED       = 3,
    WL_CONNECT_FAILED  = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED    = 6,
} wl_status_t;

// IPAddress — minimal subset matching Arduino's API.
class IPAddress {
public:
    IPAddress() : a_(0), b_(0), c_(0), d_(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : a_(a), b_(b), c_(c), d_(d) {}
    String toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a_, b_, c_, d_);
        return String(buf);
    }
    operator uint32_t() const { return ((uint32_t)a_ << 24) | ((uint32_t)b_ << 16) | ((uint32_t)c_ << 8) | d_; }
private:
    uint8_t a_, b_, c_, d_;
};

class WiFiClass {
public:
    wl_status_t  begin(const char* ssid = nullptr, const char* passphrase = nullptr);
    wl_status_t  begin(const String& ssid, const String& passphrase) { return begin(ssid.c_str(), passphrase.c_str()); }
    int          disconnect(bool wifioff = false);
    wl_status_t  status();
    bool         isConnected();
    IPAddress    localIP();
    IPAddress    gatewayIP();
    IPAddress    subnetMask();
    String       SSID();
    String       macAddress();
    int          RSSI();
    void         mode(int /*m*/) {}
    void         setSleep(bool /*s*/) {}
    void         setHostname(const char* /*h*/) {}
};

extern WiFiClass WiFi;

#define WIFI_OFF     0
#define WIFI_STA     1
#define WIFI_AP      2
#define WIFI_AP_STA  3
```

- [ ] **Step 2: Write `runtime/shims/WiFiClient.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WString.h"

class WiFiClient {
public:
    WiFiClient() = default;
    virtual ~WiFiClient() = default;

    virtual int  connect(const char* host, uint16_t port);
    virtual int  connect(const String& host, uint16_t port) { return connect(host.c_str(), port); }
    virtual void stop();
    virtual bool connected();
    virtual int  available()                 { return 0; }
    virtual int  read()                      { return -1; }
    virtual int  read(uint8_t* buf, size_t n);
    virtual size_t write(uint8_t b)          { (void)b; return 1; }
    virtual size_t write(const uint8_t* buf, size_t n) { (void)buf; return n; }
    virtual void flush()                     {}
    virtual void setTimeout(uint32_t /*ms*/) {}
    virtual operator bool() const            { return connected_; }

protected:
    bool connected_ = false;
    // Backing storage for canned responses in fake mode etc.
};
```

- [ ] **Step 3: Implement `runtime/src/sim_wifi.cpp`**

```cpp
#include "WiFi.h"
#include "WiFiClient.h"
#include "sim_net.h"
#include <cstdio>
#include <cstring>

WiFiClass WiFi;

wl_status_t WiFiClass::begin(const char* /*ssid*/, const char* /*pass*/) {
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAKE: return WL_CONNECTED;
        case BOARDGHOST_NET_FAIL: return WL_NO_SSID_AVAIL;
        case BOARDGHOST_NET_REAL: return WL_CONNECTED;   // real-mode "associate" is a no-op; libcurl handles its own DNS
    }
    return WL_DISCONNECTED;
}

int WiFiClass::disconnect(bool /*wifioff*/) { return 1; }

wl_status_t WiFiClass::status() {
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAIL: return WL_DISCONNECTED;
        default:                  return WL_CONNECTED;
    }
}

bool WiFiClass::isConnected() { return status() == WL_CONNECTED; }

IPAddress WiFiClass::localIP()    { return IPAddress(127, 0, 0, 1); }
IPAddress WiFiClass::gatewayIP()  { return IPAddress(127, 0, 0, 1); }
IPAddress WiFiClass::subnetMask() { return IPAddress(255, 255, 255, 0); }

String WiFiClass::SSID()       { return String("boardghost-sim"); }
String WiFiClass::macAddress() { return String("DE:AD:BE:EF:00:01"); }

int WiFiClass::RSSI() {
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAIL: return 0;
        default:                  return -42;
    }
}

// --- WiFiClient ---

int WiFiClient::connect(const char* host, uint16_t port) {
    (void)host; (void)port;
    if (sim_net_mode() == BOARDGHOST_NET_FAIL) {
        connected_ = false;
        return 0;
    }
    connected_ = true;
    return 1;
}

void WiFiClient::stop() { connected_ = false; }
bool WiFiClient::connected() { return connected_; }

int WiFiClient::read(uint8_t* buf, size_t n) {
    (void)buf; (void)n;
    return 0;   // no incoming data in fake mode
}
```

- [ ] **Step 4: Add sources to `runtime/CMakeLists.txt`**

In the existing `add_library(sim_runtime STATIC …)` block, append `src/sim_wifi.cpp` to the source list.

- [ ] **Step 5: Write the test**

`runtime/tests/test_wifi.cpp`:

```cpp
#include <gtest/gtest.h>
#include "WiFi.h"
#include <cstdlib>

TEST(WiFi, FakeModeConnectsImmediately) {
    setenv("BOARDGHOST_NET", "fake", 1);
    EXPECT_EQ(WiFi.begin("ssid", "pw"), WL_CONNECTED);
    EXPECT_EQ(WiFi.status(), WL_CONNECTED);
    EXPECT_TRUE(WiFi.isConnected());
    EXPECT_EQ(WiFi.localIP().toString(), String("127.0.0.1"));
}

TEST(WiFi, FailModeReportsNoSsid) {
    setenv("BOARDGHOST_NET", "fail", 1);
    EXPECT_EQ(WiFi.begin("ssid", "pw"), WL_NO_SSID_AVAIL);
    EXPECT_EQ(WiFi.status(), WL_DISCONNECTED);
    EXPECT_FALSE(WiFi.isConnected());
}

TEST(WiFi, ClientConnectInFakeMode) {
    setenv("BOARDGHOST_NET", "fake", 1);
    WiFiClient client;
    EXPECT_EQ(client.connect("example.com", 80), 1);
    EXPECT_TRUE(client.connected());
    client.stop();
    EXPECT_FALSE(client.connected());
}
```

> Caveat documented in Task 1: `sim_net_mode` caches, so each test process sees only one mode. Each TEST is a separate process via gtest_discover_tests — verify by checking CMake config. If the cache isn't process-scoped, split into individual TEST binaries.

- [ ] **Step 6: Register the test**

Add `test_wifi.cpp` to `runtime/tests/CMakeLists.txt`.

- [ ] **Step 7: Build + run**

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build 2>&1 | tail -3
```

Expected: 42 tests pass (39 + 3 new). If the caching gotcha bites and tests fail, split test_wifi.cpp into multiple test binaries OR add a `sim_net_reset_for_testing()` helper called in SetUp.

- [ ] **Step 8: Commit**

```bash
git add runtime/shims/WiFi.h runtime/shims/WiFiClient.h runtime/src/sim_wifi.cpp \
        runtime/CMakeLists.txt runtime/tests/test_wifi.cpp runtime/tests/CMakeLists.txt
git commit -m "feat(runtime): WiFi + WiFiClient shims with BOARDGHOST_NET dispatch"
```

---

## Task 3: WiFiClientSecure shim

Tiny — extends WiFiClient with TLS-config no-ops.

**Files:**
- Create: `runtime/shims/WiFiClientSecure.h`
- Modify: `runtime/tests/test_wifi.cpp` (add a brief test)

- [ ] **Step 1: Write the header**

`runtime/shims/WiFiClientSecure.h`:

```cpp
#pragma once
#include "WiFiClient.h"

class WiFiClientSecure : public WiFiClient {
public:
    void setCACert(const char* /*cert*/)       {}
    void setCertificate(const char* /*cert*/)  {}
    void setPrivateKey(const char* /*key*/)    {}
    void setInsecure()                          {}
    void setHandshakeTimeout(uint32_t /*ms*/)  {}
};
```

- [ ] **Step 2: Add a test**

Append to `runtime/tests/test_wifi.cpp`:

```cpp
#include "WiFiClientSecure.h"

TEST(WiFiClientSecure, CompilesAndDelegates) {
    setenv("BOARDGHOST_NET", "fake", 1);
    WiFiClientSecure client;
    client.setCACert("dummy");
    client.setInsecure();
    EXPECT_EQ(client.connect("example.com", 443), 1);
}
```

- [ ] **Step 3: Build + run**

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build -R WiFiClientSecure
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add runtime/shims/WiFiClientSecure.h runtime/tests/test_wifi.cpp
git commit -m "feat(runtime): WiFiClientSecure shim"
```

---

## Task 4: HTTPClient shim — fake + fail modes

`real` mode lands in Task 5 separately so we can gate the libcurl integration.

**Files:**
- Create: `runtime/shims/HTTPClient.h`
- Create: `runtime/src/sim_http.cpp`
- Create: `runtime/tests/test_http.cpp`
- Modify: `runtime/CMakeLists.txt`
- Modify: `runtime/tests/CMakeLists.txt`

- [ ] **Step 1: Write `HTTPClient.h`**

```cpp
#pragma once
#include "WString.h"
#include "WiFiClient.h"
#include <stdint.h>

// Standard HTTPClient error codes matching ESP32 Arduino core.
#define HTTPC_ERROR_CONNECTION_REFUSED  (-1)
#define HTTPC_ERROR_SEND_HEADER_FAILED  (-2)
#define HTTPC_ERROR_SEND_PAYLOAD_FAILED (-3)
#define HTTPC_ERROR_NOT_CONNECTED       (-4)
#define HTTPC_ERROR_CONNECTION_LOST     (-5)
#define HTTPC_ERROR_NO_STREAM           (-6)
#define HTTPC_ERROR_NO_HTTP_SERVER      (-7)
#define HTTPC_ERROR_TOO_LESS_RAM        (-8)
#define HTTPC_ERROR_ENCODING            (-9)
#define HTTPC_ERROR_STREAM_WRITE        (-10)
#define HTTPC_ERROR_READ_TIMEOUT        (-11)

class HTTPClient {
public:
    bool   begin(const String& url);
    bool   begin(WiFiClient& /*client*/, const String& url) { return begin(url); }
    bool   begin(const String& host, uint16_t port, const String& uri);
    void   end();

    void   addHeader(const String& /*name*/, const String& /*value*/) {}
    void   setTimeout(uint32_t /*ms*/) {}
    void   setUserAgent(const String& /*ua*/) {}
    void   setAuthorization(const char* /*user*/, const char* /*pw*/) {}
    void   setReuse(bool /*reuse*/) {}

    int    GET();
    int    POST(const String& payload);
    int    POST(uint8_t* payload, size_t size);
    int    PUT(const String& payload);
    int    DELETE();

    String getString();
    int    getSize()  { return static_cast<int>(body_.length()); }

    String header(const char* /*name*/) { return String(""); }

private:
    String url_;
    String body_;
    int    last_code_ = HTTPC_ERROR_NOT_CONNECTED;
};
```

- [ ] **Step 2: Implement `runtime/src/sim_http.cpp` — fake + fail only**

```cpp
#include "HTTPClient.h"
#include "sim_net.h"
#include <cstdio>

bool HTTPClient::begin(const String& url) {
    url_ = url;
    body_.operator=("");
    last_code_ = 0;
    return true;
}

bool HTTPClient::begin(const String& host, uint16_t port, const String& uri) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "http://%s:%u%s", host.c_str(), port, uri.c_str());
    return begin(String(buf));
}

void HTTPClient::end() { body_.operator=(""); last_code_ = HTTPC_ERROR_NOT_CONNECTED; }

static int simulated_request(const String& url, const String& /*method*/, const String& /*body*/, String& out_body) {
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAKE:
            std::fprintf(stderr, "[boardghost] HTTP fake → 200 empty (%s)\n", url.c_str());
            out_body.operator=("");
            return 200;
        case BOARDGHOST_NET_FAIL:
            std::fprintf(stderr, "[boardghost] HTTP fail → -1 (%s)\n", url.c_str());
            out_body.operator=("");
            return HTTPC_ERROR_CONNECTION_REFUSED;
        case BOARDGHOST_NET_REAL:
            // Real-mode implementation lands in Task 5 (libcurl).
            // Fall through to fake until then.
            std::fprintf(stderr, "[boardghost] HTTP real → not implemented yet, returning fake 200\n");
            out_body.operator=("");
            return 200;
    }
    return HTTPC_ERROR_CONNECTION_REFUSED;
}

int HTTPClient::GET()                                { last_code_ = simulated_request(url_, "GET",  String(""), body_); return last_code_; }
int HTTPClient::POST(const String& payload)          { last_code_ = simulated_request(url_, "POST", payload,    body_); return last_code_; }
int HTTPClient::POST(uint8_t* payload, size_t size)  { (void)payload; (void)size; last_code_ = simulated_request(url_, "POST", String(""), body_); return last_code_; }
int HTTPClient::PUT(const String& payload)           { last_code_ = simulated_request(url_, "PUT",  payload,    body_); return last_code_; }
int HTTPClient::DELETE()                             { last_code_ = simulated_request(url_, "DELETE", String(""), body_); return last_code_; }

String HTTPClient::getString() { return body_; }
```

- [ ] **Step 3: Add to runtime CMake**

Add `src/sim_http.cpp` to the `add_library(sim_runtime STATIC …)` source list.

- [ ] **Step 4: Write `test_http.cpp`**

```cpp
#include <gtest/gtest.h>
#include "HTTPClient.h"
#include <cstdlib>

TEST(HTTPClient, FakeModeReturns200Empty) {
    setenv("BOARDGHOST_NET", "fake", 1);
    HTTPClient http;
    http.begin(String("http://example.com/data.json"));
    int code = http.GET();
    EXPECT_EQ(code, 200);
    EXPECT_EQ(http.getString(), String(""));
    http.end();
}

TEST(HTTPClient, FailModeReturnsNegative) {
    setenv("BOARDGHOST_NET", "fail", 1);
    HTTPClient http;
    http.begin(String("http://example.com/data.json"));
    int code = http.GET();
    EXPECT_LT(code, 0);
    http.end();
}
```

- [ ] **Step 5: Register + run**

Add `test_http.cpp` to `runtime/tests/CMakeLists.txt`.

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build 2>&1 | tail -3
```

Expected: 44 tests pass (42 + 2 new).

- [ ] **Step 6: Commit**

```bash
git add runtime/shims/HTTPClient.h runtime/src/sim_http.cpp runtime/CMakeLists.txt \
        runtime/tests/test_http.cpp runtime/tests/CMakeLists.txt
git commit -m "feat(runtime): HTTPClient shim — fake/fail modes (real lands in next task)"
```

---

## Task 5: HTTPClient real mode via libcurl

Wires `BOARDGHOST_NET=real` to actually fetch the URL. Gated behind `BOARDGHOST_WITH_CURL`.

**Files:**
- Modify: `runtime/src/sim_http.cpp`

- [ ] **Step 1: Add curl-backed request implementation**

In `runtime/src/sim_http.cpp`, add at the top:

```cpp
#ifdef BOARDGHOST_WITH_CURL
#include <curl/curl.h>

namespace {
size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* out = static_cast<String*>(userp);
    String chunk;
    // Concat bytes — String has no append(ptr, len) but += String works.
    char tmp[1024];
    const char* src = static_cast<const char*>(contents);
    size_t remain = total;
    while (remain > 0) {
        size_t chunk_n = remain < sizeof(tmp) - 1 ? remain : sizeof(tmp) - 1;
        std::memcpy(tmp, src, chunk_n);
        tmp[chunk_n] = '\0';
        *out += String(tmp);
        src += chunk_n;
        remain -= chunk_n;
    }
    return total;
}
}  // namespace
#endif
```

Add `#include <cstring>` near the top.

Now replace the `BOARDGHOST_NET_REAL` branch in `simulated_request`:

```cpp
        case BOARDGHOST_NET_REAL: {
#ifdef BOARDGHOST_WITH_CURL
            CURL* curl = curl_easy_init();
            if (!curl) return HTTPC_ERROR_CONNECTION_REFUSED;
            out_body.operator=("");
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_body);
            // Method dispatch — defaults to GET; POST/PUT use the same body.
            // Simplified: set custom request for non-GET. Body included as
            // post-fields for POST/PUT.
            // (method/body args unused in this minimal version — patch in M2.C if needed)
            CURLcode rc = curl_easy_perform(curl);
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_easy_cleanup(curl);
            if (rc != CURLE_OK) {
                std::fprintf(stderr, "[boardghost] HTTP real → curl error: %s (%s)\n",
                             curl_easy_strerror(rc), url.c_str());
                return HTTPC_ERROR_CONNECTION_REFUSED;
            }
            std::fprintf(stderr, "[boardghost] HTTP real → %ld (%s)\n", http_code, url.c_str());
            return static_cast<int>(http_code);
#else
            std::fprintf(stderr, "[boardghost] HTTP real → not built with curl, returning fake 200 (%s)\n", url.c_str());
            out_body.operator=("");
            return 200;
#endif
        }
```

- [ ] **Step 2: Add a curl test (skip if libcurl missing)**

Append to `runtime/tests/test_http.cpp`:

```cpp
TEST(HTTPClient, RealModeWhenCurlAvailable) {
#ifdef BOARDGHOST_WITH_CURL
    setenv("BOARDGHOST_NET", "real", 1);
    HTTPClient http;
    // example.com is a stable test target maintained by IANA.
    http.begin(String("http://example.com/"));
    int code = http.GET();
    EXPECT_EQ(code, 200);
    EXPECT_GT(http.getString().length(), 100u);
    http.end();
#else
    GTEST_SKIP() << "BOARDGHOST_WITH_CURL not enabled";
#endif
}
```

This test requires internet access to example.com. CI will hit it. If you want to skip in CI, set `BOARDGHOST_SKIP_NET_TESTS=1` and gate the test on that env var.

- [ ] **Step 3: Build + run**

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build -R HTTPClient 2>&1 | tail -5
```

Expected: 3 tests pass (RealMode might be skipped if libcurl missing).

- [ ] **Step 4: Commit**

```bash
git add runtime/src/sim_http.cpp runtime/tests/test_http.cpp
git commit -m "feat(runtime): HTTPClient real mode via libcurl"
```

---

## Task 6: EEPROM shim — file-backed at .boardghost/eeprom.bin

**Files:**
- Create: `runtime/shims/EEPROM.h`
- Create: `runtime/src/sim_eeprom.cpp`
- Create: `runtime/tests/test_eeprom.cpp`
- Modify: `runtime/CMakeLists.txt`
- Modify: `runtime/tests/CMakeLists.txt`

- [ ] **Step 1: Write `EEPROM.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

class EEPROMClass {
public:
    bool    begin(size_t size);
    void    end();

    uint8_t read(int addr);
    void    write(int addr, uint8_t val);
    bool    commit();

    // ESP32 Arduino-core helpers used by some sketches:
    template <typename T>
    T readInt() { return readInt(0); }
    int  readInt(int addr);
    void writeInt(int addr, int value);

    size_t length() const { return size_; }

private:
    size_t   size_  = 0;
    uint8_t* data_  = nullptr;
};

extern EEPROMClass EEPROM;
```

- [ ] **Step 2: Implement `sim_eeprom.cpp`**

```cpp
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
```

- [ ] **Step 3: Add to CMake**

Add `src/sim_eeprom.cpp` to sim_runtime sources.

- [ ] **Step 4: Test**

`runtime/tests/test_eeprom.cpp`:

```cpp
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
```

- [ ] **Step 5: Register + run**

Add `test_eeprom.cpp` to `runtime/tests/CMakeLists.txt`.

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build -R Eeprom 2>&1 | tail -5
```

Expected: 3 PASS.

- [ ] **Step 6: Commit**

```bash
git add runtime/shims/EEPROM.h runtime/src/sim_eeprom.cpp runtime/CMakeLists.txt \
        runtime/tests/test_eeprom.cpp runtime/tests/CMakeLists.txt
git commit -m "feat(runtime): EEPROM shim persisted to .boardghost/eeprom.bin"
```

---

## Task 7: FS + SPIFFS + LittleFS + SD shims — map to ./sim-assets/

**Files:**
- Create: `runtime/shims/FS.h`
- Create: `runtime/shims/SPIFFS.h`
- Create: `runtime/shims/LittleFS.h`
- Create: `runtime/shims/SD.h`
- Create: `runtime/src/sim_fs.cpp`
- Create: `runtime/tests/test_fs.cpp`
- Modify: `runtime/CMakeLists.txt`
- Modify: `runtime/tests/CMakeLists.txt`

- [ ] **Step 1: Write `FS.h`**

```cpp
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
```

- [ ] **Step 2: Write the SPIFFS / LittleFS / SD headers**

`runtime/shims/SPIFFS.h`:

```cpp
#pragma once
#include "FS.h"

extern fs::FS SPIFFS;
```

`runtime/shims/LittleFS.h`:

```cpp
#pragma once
#include "FS.h"

extern fs::FS LittleFS;
```

`runtime/shims/SD.h`:

```cpp
#pragma once
#include "FS.h"

extern fs::FS SD;

// Many SD users also include this:
class SDClass {
public:
    bool begin(int /*cs*/ = -1) { return true; }
    void end() {}
};
extern SDClass SDLib;
```

- [ ] **Step 3: Implement `sim_fs.cpp`**

```cpp
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
```

- [ ] **Step 4: Add to CMake**

Add `src/sim_fs.cpp` to sim_runtime sources.

- [ ] **Step 5: Test**

`runtime/tests/test_fs.cpp`:

```cpp
#include <gtest/gtest.h>
#include "SPIFFS.h"
#include "LittleFS.h"
#include "FS.h"
#include <filesystem>
#include <cstdlib>
namespace std_fs = std::filesystem;

class FsTest : public ::testing::Test {
protected:
    std_fs::path assets_;
    void SetUp() override {
        assets_ = std_fs::temp_directory_path() / "bg-fs-test";
        std_fs::remove_all(assets_);
        std_fs::create_directories(assets_);
        setenv("BOARDGHOST_ASSETS_DIR", assets_.c_str(), 1);
    }
    void TearDown() override {
        std_fs::remove_all(assets_);
    }
};

TEST_F(FsTest, BeginCreatesMountRoot) {
    ASSERT_TRUE(SPIFFS.begin());
    EXPECT_TRUE(std_fs::exists(assets_ / "spiffs"));
}

TEST_F(FsTest, WriteThenReadFile) {
    ASSERT_TRUE(SPIFFS.begin());
    {
        auto f = SPIFFS.open("/hello.txt", FILE_WRITE);
        ASSERT_TRUE(f);
        const char* msg = "hello";
        f.write(reinterpret_cast<const uint8_t*>(msg), 5);
    }

    auto f = SPIFFS.open("/hello.txt", FILE_READ);
    ASSERT_TRUE(f);
    uint8_t buf[16] = {};
    size_t n = f.read(buf, sizeof(buf));
    EXPECT_EQ(n, 5u);
    EXPECT_EQ(std::string((char*)buf, 5), "hello");
}

TEST_F(FsTest, ExistsReturnsFalseForMissing) {
    ASSERT_TRUE(SPIFFS.begin());
    EXPECT_FALSE(SPIFFS.exists("/no-such-file"));
}

TEST_F(FsTest, SpiffsAndLittleFsAreIsolated) {
    ASSERT_TRUE(SPIFFS.begin());
    ASSERT_TRUE(LittleFS.begin());
    {
        auto f = SPIFFS.open("/x.bin", FILE_WRITE);
        ASSERT_TRUE(f);
        uint8_t b = 0xAA;
        f.write(&b, 1);
    }
    // LittleFS shouldn't see SPIFFS's file.
    EXPECT_FALSE(LittleFS.exists("/x.bin"));
    EXPECT_TRUE (SPIFFS.exists("/x.bin"));
}
```

- [ ] **Step 6: Register + run**

Add `test_fs.cpp` to `runtime/tests/CMakeLists.txt`.

```bash
cmake --build runtime/build -j && ctest --test-dir runtime/build -R FsTest 2>&1 | tail -5
```

Expected: 4 PASS.

- [ ] **Step 7: Commit**

```bash
git add runtime/shims/FS.h runtime/shims/SPIFFS.h runtime/shims/LittleFS.h runtime/shims/SD.h \
        runtime/src/sim_fs.cpp runtime/CMakeLists.txt \
        runtime/tests/test_fs.cpp runtime/tests/CMakeLists.txt
git commit -m "feat(runtime): FS/SPIFFS/LittleFS/SD shims mapping to ./sim-assets/<mount>/"
```

---

## Task 8: TinyGsmClient + StreamDebugger stubs

Both are tiny — just enough to compile. Bundled in one task.

**Files:**
- Create: `runtime/shims/TinyGsmClient.h`
- Create: `runtime/shims/StreamDebugger.h`

- [ ] **Step 1: Write `TinyGsmClient.h`**

```cpp
#pragma once
#include "WString.h"
#include "WiFiClient.h"

// Minimal stub for TinyGsm. Real lib has many modem-specific subclasses
// (A7670, SIM800, etc.) — they all share the same TinyGsm public surface,
// so a single stub is sufficient to compile sketches.
class TinyGsm {
public:
    template <typename T>
    TinyGsm(T&) {}
    TinyGsm() {}

    bool   init(const char* /*pin*/ = nullptr)        { return false; }
    bool   restart()                                   { return false; }
    bool   testAT(uint32_t /*timeout*/ = 10000)        { return false; }
    bool   isNetworkConnected()                        { return false; }
    bool   isGprsConnected()                           { return false; }
    bool   waitForNetwork(uint32_t /*timeout*/ = 60000){ return false; }
    bool   gprsConnect(const char* /*apn*/, const char* /*user*/ = nullptr, const char* /*pw*/ = nullptr) { return false; }
    bool   gprsDisconnect()                            { return false; }

    String getModemInfo()  { return String("BoardGhost-TinyGsm-stub"); }
    String getOperator()   { return String(""); }
    String getIMEI()       { return String("000000000000000"); }
    int    getSignalQuality() { return 0; }
};

class TinyGsmClient : public WiFiClient {
public:
    template <typename T>
    TinyGsmClient(T& /*modem*/) {}
    TinyGsmClient() = default;
};
```

- [ ] **Step 2: Write `StreamDebugger.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

// StreamDebugger wraps a Stream and a debug-output Stream. In sim we just
// satisfy the constructor signature; reads/writes are no-ops.
template <typename A, typename B>
class StreamDebuggerT {
public:
    StreamDebuggerT(A& /*upstream*/, B& /*debug*/) {}
    int  available()                  { return 0; }
    int  read()                       { return -1; }
    size_t write(uint8_t /*b*/)       { return 1; }
    size_t write(const uint8_t* /*b*/, size_t n) { return n; }
    void flush()                      {}
};

// Real lib uses StreamDebugger as a non-template. Provide a forwarding alias.
class HardwareSerial;  // forward-declare; ESP32 core defines this.
using StreamDebugger = StreamDebuggerT<HardwareSerial, HardwareSerial>;
```

- [ ] **Step 3: Verify Arduino.h pulls them where needed (no change needed — sketches `#include` them directly)**

These headers don't need to be umbrella-included. Sketches that use TinyGsm will `#include <TinyGsmClient.h>`.

- [ ] **Step 4: Build to verify they at least compile**

```bash
cmake --build runtime/build -j 2>&1 | tail -3
```

Expected: clean. No new tests — these compile-only stubs are exercised by the t-simLovyan test in Task 10.

- [ ] **Step 5: Commit**

```bash
git add runtime/shims/TinyGsmClient.h runtime/shims/StreamDebugger.h
git commit -m "feat(runtime): TinyGsmClient + StreamDebugger compile-stubs"
```

---

## Task 9: Update library allowlist

The CLI's Stage 3 allowlist (`crates/boardghost-cli/src/libraries.rs`) restricts which arduino-cli-resolved libraries make it through to the build. With M2.B's shims, we want to allow real-project libraries that map onto our stubs.

**Files:**
- Modify: `crates/boardghost-cli/src/libraries.rs`
- Modify: `crates/boardghost-cli/tests/libraries_test.rs` (optional — add a test for the new entries)

- [ ] **Step 1: Broaden the allowlist**

In `crates/boardghost-cli/src/libraries.rs`, replace the `ALLOWLIST` constant:

```rust
pub const ALLOWLIST: &[&str] = &[
    // M1 — graphics
    "LovyanGFX",
    "lvgl",
    "Adafruit_GFX",
    // M2.B — IoT stubs (these names match arduino-cli's library reports)
    "WiFi",
    "WiFiClient",
    "WiFiClientSecure",
    "HTTPClient",
    "EEPROM",
    "FS",
    "SPIFFS",
    "LittleFS",
    "SD",
    "TinyGSM",
    "StreamDebugger",
];
```

Note: arduino-cli reports library names case-sensitively as they appear in `library.properties`. `is_allowed` already does case-insensitive matching so minor casing differences (`Wifi` vs `WiFi`) won't matter.

- [ ] **Step 2: Update the test**

In `crates/boardghost-cli/tests/libraries_test.rs`, update `allowlist_contains_expected_entries`:

```rust
#[test]
fn allowlist_contains_expected_entries() {
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("LovyanGFX")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("lvgl")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("Adafruit_GFX")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("WiFi")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("HTTPClient")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("EEPROM")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("SPIFFS")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("TinyGSM")));
}
```

- [ ] **Step 3: Cargo test**

```bash
cargo test -p boardghost-cli 2>&1 | tail -3
```

Expected: previous + this one PASS.

- [ ] **Step 4: Commit**

```bash
git add crates/boardghost-cli/src/libraries.rs crates/boardghost-cli/tests/libraries_test.rs
git commit -m "feat(cli): allowlist M2.B shimmed libraries (WiFi, HTTPClient, EEPROM, FS, SPIFFS, ...)"
```

---

## Task 10: First-contact test — t-simLovyan ORIGINAL sketch

The whole point of M2.B: a real sketch should compile against our shims **without `#ifndef BOARDGHOST_SIM` wraps**.

**Files:** none — this is a verification task.

- [ ] **Step 1: Copy the user's untouched sketch to a fresh sandbox**

```bash
rm -rf /tmp/t-simLovyan-m2b
cp -r /home/phill/Arduino/t-simLovyan /tmp/t-simLovyan-m2b
```

> Note: we keep the user's pristine `lvgfx_setup.h` (with `lgfx::Panel_ST7796 _panel_instance; lgfx::Bus_SPI _bus_instance; lgfx::Touch_XPT2046 _touch_instance;`) — that's the hardware-platform-class problem from the M2.A first-contact test. We do NOT solve that in M2.B (it's an LGFX codemod problem, planned for M2.C). For M2.B we just need to ensure the user's source-as-written has SHIM coverage for everything else.

Because the LGFX class is the obstinate-on-purpose remaining gap, the user still needs ONE manual edit: swap their lvgfx_setup.h to `using LGFX = LGFX_ST7796_SDL;` under `#ifdef BOARDGHOST_SIM`. That's the M2.C codemod target.

For THIS task we apply only that one edit to the sandbox and verify everything else compiles unwrapped:

```bash
cat > /tmp/t-simLovyan-m2b/lvgfx_setup.h <<'EOF'
// M2.B test: only the LGFX class definition is sim-wrapped.
// All other unshimmed-in-M2.A APIs (WiFi, HTTPClient, SPIFFS, EEPROM, TinyGsm,
// StreamDebugger, WiFiClientSecure, SD, FS) should compile through M2.B shims
// without #ifndef BOARDGHOST_SIM wraps.

#ifdef BOARDGHOST_SIM
#include <LGFX_ST7796_SDL.hpp>
using LGFX = LGFX_ST7796_SDL;
#else
// (original hardware-specific class — omitted in test variant)
#endif
EOF
```

- [ ] **Step 2: Rename to match dir convention and clean stale build**

```bash
# (Sketch is already named t-simLovyan.ino — dir is t-simLovyan-m2b, mismatch).
mv /tmp/t-simLovyan-m2b/t-simLovyan.ino /tmp/t-simLovyan-m2b/t-simLovyan-m2b.ino
rm -rf /tmp/t-simLovyan-m2b/.boardghost
```

- [ ] **Step 3: Build + run headlessly with a screenshot**

```bash
SDL_VIDEODRIVER=dummy boardghost run /tmp/t-simLovyan-m2b \
    --board st7796_esp32s3_sim \
    --screenshot /tmp/t-sim-m2b.png 2>&1 | tail -20
```

This will exercise:
- arduino-cli preprocess of the user's pristine `.ino` (with WiFi/HTTPClient/SPIFFS/EEPROM/TinyGsm/StreamDebugger/WiFiClientSecure/SD/FS includes — all should resolve via Arduino-CLI to real libraries; the preprocessed `.cpp` keeps those `#include` lines intact).
- CMake compile of the preprocessed `.cpp` against our shim headers (which take precedence on the include path).
- Linker resolves all the API calls against our sim_* implementations.
- Runtime: SPIFFS.begin() succeeds (sim-assets/spiffs/ gets created), drawJpgFile(SPIFFS, "/main.jpg") fails gracefully because the file isn't there, sketch continues, draws calibration text, exits.

If the build fails, fix forward: any missing API method on a shim class. Each fix is a one-line addition to a shim header and (if needed) a one-line method body in the corresponding .cpp.

If `drawJpgFile` segfaults instead of failing gracefully, that's a LovyanGFX-PC behaviour to investigate — for M2.B acceptance, dropping a real JPG into `/tmp/t-simLovyan-m2b/sim-assets/spiffs/main.jpg` and seeing it render counts as success.

- [ ] **Step 4: Verify the screenshot**

```bash
file /tmp/t-sim-m2b.png
```

Expected: `PNG image data, 480 x 320, 8-bit/color RGBA, non-interlaced`.

Open the PNG (or feed to the controller for visual confirmation). Should show something the user recognises from their sketch's drawing path — at minimum the green "SCREEN CALIBRATION" text from `touch_calibration()`.

- [ ] **Step 5: Document the result**

This is verification only — no commit unless you had to add missing shim methods. If you did add anything, commit those.

The success criterion: **the unmodified user sketch (minus the LGFX class) builds and produces a render**. Capture the screenshot for the M2.B docs in Task 11.

---

## Task 11: Docs + acceptance + tag

**Files:**
- Modify: `README.md`
- Modify: `docs/getting-started.md`
- Modify: `.github/workflows/ci.yml` (add libcurl install on Linux runner)

- [ ] **Step 1: README update**

Replace the "What works" section:

```markdown
## What works

- 7 simulated panels (LovyanGFX + LVGL): ILI9488 480×320, ILI9341 320×240,
  ST7789 240×320, ST7796 480×320, GC9A01 240×240 round, ST7735 160×128,
  SSD1306 128×64 mono
- Touch driver (XPT2046 / FT6236 / GT911 in board profiles; SDL-mouse-backed in sim)
- Mouse + LovyanGFX touch API + LVGL indev
- IoT library stubs: WiFi, WiFiClient, WiFiClientSecure, HTTPClient, EEPROM,
  FS, SPIFFS, LittleFS, SD, TinyGsmClient, StreamDebugger
- Configurable network: `BOARDGHOST_NET=fake|fail|real` (real mode uses libcurl)
- Filesystem assets in `./sim-assets/<mount>/` (per-project, gitignorable)
- EEPROM persists to `./.boardghost/eeprom.bin`
- GPIO inspector (live pin-state grid in the launcher)
- CLI `--screenshot PATH` flag
- Tauri desktop launcher
- Headless mode (`SDL_VIDEODRIVER=dummy`) for CI
```

- [ ] **Step 2: getting-started additions**

Append:

```markdown
## 11. IoT stubs and network modes

Most sketches reach for WiFi, HTTPClient, SPIFFS, EEPROM, TinyGsm etc.
BoardGhost ships stubs for all of these. By default they pretend to
succeed (`BOARDGHOST_NET=fake`) — `WiFi.begin()` returns `WL_CONNECTED`,
`HTTPClient.GET()` returns 200 with empty body. Your UI paths run.

Other modes:

```bash
BOARDGHOST_NET=fail boardghost run my-project --board ili9488_esp32s3_sim
# WiFi.begin() returns WL_NO_SSID_AVAIL; HTTPClient.GET() returns -1.

BOARDGHOST_NET=real boardghost run my-project --board ili9488_esp32s3_sim
# HTTPClient.GET() actually fetches via libcurl. Requires libcurl at build
# time; check with `cmake -S runtime -B runtime/build` (it logs whether
# libcurl was found).
```

## 12. Sketch assets — `./sim-assets/`

SPIFFS, LittleFS, and SD map to subdirectories under `<project>/sim-assets/`:

- `SPIFFS.open("/main.jpg")` → `<project>/sim-assets/spiffs/main.jpg`
- `LittleFS.open("/data.json")` → `<project>/sim-assets/littlefs/data.json`
- `SD.open("/log.csv")` → `<project>/sim-assets/sd/log.csv`

Drop your real files into those directories. Gitignore the whole
`sim-assets/` if you don't want test fixtures in your repo.

EEPROM/NVS state lives at `<project>/.boardghost/eeprom.bin` and survives
across runs of `boardghost run`.

Override either with env vars:

```bash
BOARDGHOST_ASSETS_DIR=/path/to/shared/test-fixtures \
BOARDGHOST_EEPROM_PATH=/tmp/my-eeprom.bin \
  boardghost run my-project --board ili9488_esp32s3_sim
```
```

- [ ] **Step 3: CI workflow update**

In `.github/workflows/ci.yml`, add `libcurl4-openssl-dev` to the `Install host deps` apt-get install command (in the `build-and-test` job).

Change:

```yaml
          sudo apt-get install -y cmake ninja-build libsdl2-dev
```

to:

```yaml
          sudo apt-get install -y cmake ninja-build libsdl2-dev libcurl4-openssl-dev
```

- [ ] **Step 4: Run the full local check**

```bash
cargo test --workspace 2>&1 | grep -E '^test result'
ctest --test-dir runtime/build 2>&1 | tail -3
./tests/e2e/run_hello_serial.sh
./tests/e2e/run_ssd1306_text.sh
./tests/e2e/run_lvgl_hello.sh
```

Expected: all green. Unit tests now ~50+ total (37 from M2.A + ~13 new).

- [ ] **Step 5: Commit + push**

```bash
git add README.md docs/getting-started.md .github/workflows/ci.yml
git commit -m "docs(m2b): IoT stubs, network modes, sim-assets directory"
git push 2>&1 | tail -3
```

- [ ] **Step 6: Watch CI**

```bash
gh run list --limit 1
```

After CI is green:

- [ ] **Step 7: Tag**

```bash
git tag -a m2b-iot-stubs -m "BoardGhost M2.B — WiFi/HTTP/EEPROM/SPIFFS/LittleFS/SD/TinyGsm shims"
git push --tags
```

---

## Self-review

**Spec coverage:**

| Decision | Task |
|---|---|
| `BOARDGHOST_NET=fake|fail|real` dispatch | Task 1 |
| WiFi/WiFiClient | Task 2 |
| WiFiClientSecure | Task 3 |
| HTTPClient fake/fail | Task 4 |
| HTTPClient real via libcurl | Task 5 |
| EEPROM file-backed at `.boardghost/eeprom.bin` | Task 6 |
| FS/SPIFFS/LittleFS/SD → `./sim-assets/` | Task 7 |
| TinyGsmClient + StreamDebugger stubs | Task 8 |
| CLI library allowlist updated | Task 9 |
| Real-project verification (t-simLovyan unwrapped) | Task 10 |
| Docs + tag + CI deps | Task 11 |

**Placeholder scan:**

- Task 4 Step 2 comment "patch in M2.C if needed" — not a placeholder, it's a deliberate scope deferral note.
- Task 10 Step 1 keeps the LGFX class swap edit — explicitly called out as the one remaining manual change, deferred to M2.C codemod work.

**Type consistency:**

- `wl_status_t` enum (Task 2) used in tests (Task 2 tests) — consistent.
- `boardghost_net_mode_t` enum (Task 1) used across `sim_wifi.cpp`, `sim_http.cpp` — consistent.
- `fs::File` + `fs::FS` namespace (Task 7) — consistent across SPIFFS.h, LittleFS.h, SD.h via `extern fs::FS`.
- `EEPROMClass` singleton pattern (Task 6) matches Arduino convention.
- `HTTPC_ERROR_*` constants (Task 4) match ESP32 Arduino core values exactly.

**Known scope deferrals to M2.C / later:**

- LGFX class codemod tool (auto-wrap user's hardware-platform LGFX class).
- WebServer.h, ESPmDNS.h, ESPAsyncWebServer (server-side networking).
- ESP32Time, TimeLib (time helpers — most use stdlib time(), which already works).
- Update.h (OTA).
- HTTPClient POST body forwarding to libcurl in real mode (currently no-op).
- Async network behaviour (most sketches use blocking APIs; if needed, defer to M2.D).

---

**End of plan.**
