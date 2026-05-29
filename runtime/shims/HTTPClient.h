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
};
