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
