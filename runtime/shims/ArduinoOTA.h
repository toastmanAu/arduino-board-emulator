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
// an ESP32 image. See docs/superpowers/specs/2026-06-15-ota-asyncwebserver-design.md.
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
