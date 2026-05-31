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

// Follow-redirect policy — values mirror ESP32 Arduino core's
// `followRedirects_t` enum. Sketches that pass HTTPC_STRICT_FOLLOW_REDIRECTS
// to setFollowRedirects() compile against the same constant we expose here.
typedef enum {
    HTTPC_DISABLE_FOLLOW_REDIRECTS = 0,
    HTTPC_STRICT_FOLLOW_REDIRECTS  = 1,
    HTTPC_FORCE_FOLLOW_REDIRECTS   = 2,
} followRedirects_t;

// HTTP status code constants — Arduino code commonly checks against these.
#define HTTP_CODE_OK                    200
#define HTTP_CODE_CREATED               201
#define HTTP_CODE_NO_CONTENT            204
#define HTTP_CODE_MOVED_PERMANENTLY     301
#define HTTP_CODE_FOUND                 302
#define HTTP_CODE_NOT_MODIFIED          304
#define HTTP_CODE_BAD_REQUEST           400
#define HTTP_CODE_UNAUTHORIZED          401
#define HTTP_CODE_FORBIDDEN             403
#define HTTP_CODE_NOT_FOUND             404
#define HTTP_CODE_INTERNAL_SERVER_ERROR 500
#define HTTP_CODE_BAD_GATEWAY           502
#define HTTP_CODE_SERVICE_UNAVAILABLE   503

class HTTPClient {
public:
    bool   begin(const String& url);
    bool   begin(WiFiClient& /*client*/, const String& url) { return begin(url); }
    bool   begin(const String& host, uint16_t port, const String& uri);
    void   end();

    void   addHeader(const String& name, const String& value);
    void   setTimeout(uint32_t ms)                { timeout_ms_ = ms; }
    void   setUserAgent(const String& ua)         { user_agent_ = ua; }
    void   setAuthorization(const char* user, const char* pw);
    void   setReuse(bool /*reuse*/) {}

    // Redirect / header-collection / connection introspection — added for
    // OTA-style sketches that follow a download URL through a CDN and then
    // stream the body into Update. The sim doesn't honour the redirect mode
    // (libcurl does its own thing in real mode; fake mode never connects),
    // but the API surface keeps the sketch compiling and the call no-ops
    // semantically.
    void   setFollowRedirects(followRedirects_t /*mode*/) {}
    void   setRedirectLimit(uint16_t /*limit*/) {}
    void   collectHeaders(const char* /*headerKeys*/[], size_t /*headerKeysCount*/) {}
    bool   connected();
    // getStreamPtr — real impl returns the underlying WiFiClient so the
    // sketch can pump bytes itself via `stream->read(...)`. In the sim the
    // returned client behaves as a 0-byte stream (read() == -1), so download
    // loops gated on connected() && contentLength terminate cleanly without
    // actually copying anything. Stored on the HTTPClient instance so the
    // returned pointer outlives the call.
    WiFiClient* getStreamPtr();

    int    GET();
    int    POST(const String& payload);
    int    POST(uint8_t* payload, size_t size);
    int    PUT(const String& payload);
    int    DELETE();

    String getString();
    int    getSize()  { return static_cast<int>(body_.length()); }

    String header(const char* /*name*/) { return String(""); }

    // writeToStream — Arduino-side helper used to copy the response body into
    // an open File / Stream. Templated so we accept fs::File*, Print*, or any
    // duck-typed sink that has a `write(const uint8_t*, size_t)` method.
    // In fake mode the body_ is empty so 0 bytes get written. In real mode
    // we already buffer the body via libcurl into body_; flush it once here.
    template <typename Stream>
    int writeToStream(Stream* sink) {
        if (!sink || body_.length() == 0) return 0;
        const auto* p = reinterpret_cast<const uint8_t*>(body_.c_str());
        return static_cast<int>(sink->write(p, body_.length()));
    }

private:
    String url_;
    String body_;
    int    last_code_ = HTTPC_ERROR_NOT_CONNECTED;
    // Stored headers + auth + UA + timeout — forwarded to libcurl in
    // BOARDGHOST_NET=real mode. Up to 16 user headers; more is rare and
    // keeps the shim simple. Each header stored as a single "Name: Value"
    // line for direct curl_slist_append.
    String  hdr_lines_[16];
    int     hdr_count_      = 0;
    String  user_agent_;
    String  auth_header_;   // "Authorization: Basic <base64>" when set
    uint32_t timeout_ms_    = 30000;
    WiFiClient stream_;     // returned by getStreamPtr(); empty/disconnected
    bool    connected_      = false;
};
