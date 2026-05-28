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
