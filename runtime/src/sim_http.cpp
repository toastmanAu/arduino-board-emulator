#include "HTTPClient.h"
#include "sim_net.h"
#include <cstdio>
#include <cstring>

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
    }
    return HTTPC_ERROR_CONNECTION_REFUSED;
}

int HTTPClient::GET()                                { last_code_ = simulated_request(url_, "GET",  String(""), body_); return last_code_; }
int HTTPClient::POST(const String& payload)          { last_code_ = simulated_request(url_, "POST", payload,    body_); return last_code_; }
int HTTPClient::POST(uint8_t* payload, size_t size)  { (void)payload; (void)size; last_code_ = simulated_request(url_, "POST", String(""), body_); return last_code_; }
int HTTPClient::PUT(const String& payload)           { last_code_ = simulated_request(url_, "PUT",  payload,    body_); return last_code_; }
int HTTPClient::DELETE()                             { last_code_ = simulated_request(url_, "DELETE", String(""), body_); return last_code_; }

String HTTPClient::getString() { return body_; }
