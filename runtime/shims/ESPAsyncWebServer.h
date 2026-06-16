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
    // Internal: lets AsyncWebServer co-own the impl so registered httplib
    // lambdas can't outlive it (see addHandler).
    std::shared_ptr<boardghost_internal::EventSourceImpl> shared_impl() const { return impl_; }
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
