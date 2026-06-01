#pragma once
#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "WString.h"
#include "WiFi.h"

// Forward-decl so the public header doesn't drag cpp-httplib into every TU
// that #includes WebServer.h. The .cpp implementation does the real work.
namespace boardghost_internal { class WebServerImpl; }

// HTTP method constants — match ESP32 Arduino core values where it matters
// (mostly for `case HTTP_GET:` style switches).
enum HTTPMethod : int {
    HTTP_ANY     = 0,
    HTTP_GET     = 1,
    HTTP_POST    = 2,
    HTTP_PUT     = 3,
    HTTP_PATCH   = 4,
    HTTP_DELETE  = 5,
    HTTP_OPTIONS = 6,
};

enum HTTPUploadStatus : int {
    UPLOAD_FILE_START   = 0,
    UPLOAD_FILE_WRITE   = 1,
    UPLOAD_FILE_END     = 2,
    UPLOAD_FILE_ABORTED = 3,
};

// HTTPUpload — mirrors the ESP32 WebServer struct. The sim never invokes
// upload handlers (no real HTTP path), but sketches that pattern-match on
// `.status` and read `.buf` / `.currentSize` still need the struct to exist.
struct HTTPUpload {
    HTTPUploadStatus status   = UPLOAD_FILE_ABORTED;
    String           filename;
    String           name;
    String           type;
    size_t           totalSize    = 0;
    size_t           currentSize  = 0;
    uint8_t          buf[1460]    = {};  // ESP32 default upload buffer size
};

// WebServer client stub — just enough surface for `server.client().remoteIP()`.
class WebServerClient {
public:
    IPAddress remoteIP() const { return IPAddress(0, 0, 0, 0); }
};

class WebServer {
public:
    explicit WebServer(uint16_t port = 80);
    ~WebServer();

    using THandlerFunction         = std::function<void(void)>;
    using THandlerFunctionUpload   = std::function<void(void)>;

    void begin();
    void begin(uint16_t port);
    void stop();
    void close();
    // No-op: cpp-httplib drives requests on its own thread. The sketch can
    // still call this from loop() to match ESP32 API; it does nothing.
    void handleClient() {}

    void on(const char* uri, THandlerFunction handler);
    void on(const char* uri, HTTPMethod method, THandlerFunction handler);
    void on(const char* uri, HTTPMethod method, THandlerFunction handler,
            THandlerFunctionUpload upload);
    void onNotFound(THandlerFunction handler);
    void onFileUpload(THandlerFunctionUpload handler);

    void send(int code);
    void send(int code, const char* content_type, const String& content = String());
    void send(int code, const String& content_type, const String& content);
    void send(int code, const char* content_type, const char* content);
    void send_P(int code, const char* content_type, const char* content);
    void sendHeader(const String& name, const String& value, bool first = false);
    void sendContent(const String& content);

    String arg(const char* name) const;
    String arg(int i) const;
    String argName(int i) const;
    int    args() const;
    bool   hasArg(const char* name) const;
    String header(const char* name) const;
    String header(int i) const;
    String headerName(int i) const;
    int    headers() const;
    bool   hasHeader(const char* name) const;
    String hostHeader() const;
    String uri() const;
    HTTPMethod method() const;
    HTTPUpload& upload()                                       { return upload_; }
    WebServerClient client()                                   { return WebServerClient(); }

    // Headers the sketch wants exposed via header()/headerName(). cpp-httplib
    // surfaces all headers anyway so this just records which keys the sketch
    // is interested in — we don't filter on the server side.
    void collectHeaders(const char* headerKeys[], const size_t headerKeysCount);

    template <typename T>
    size_t streamFile(T& /*file*/, const String& /*contentType*/) { return 0; }

    void enableCORS(bool enable = true);
    void enableCrossOrigin(bool enable = true)                 { enableCORS(enable); }
    void enableDelay(bool /*enable*/)                          {}

private:
    uint16_t                                          port_;
    HTTPUpload                                        upload_;
    std::unique_ptr<boardghost_internal::WebServerImpl> impl_;
};
