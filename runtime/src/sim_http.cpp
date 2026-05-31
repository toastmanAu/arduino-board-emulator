#include "HTTPClient.h"
#include "sim_net.h"
#include <cstdio>
#include <cstring>
#include <string>

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
    hdr_count_ = 0;   // begin() resets headers — matches Arduino HTTPClient semantics
    auth_header_.operator=("");
    return true;
}

void HTTPClient::addHeader(const String& name, const String& value) {
    if (hdr_count_ >= 16) return;
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s: %s", name.c_str(), value.c_str());
    hdr_lines_[hdr_count_++] = String(buf);
}

namespace {
// Tiny base64 encoder for HTTP Basic auth.
std::string b64_encode(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(T[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(T[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
}  // namespace

void HTTPClient::setAuthorization(const char* user, const char* pw) {
    std::string creds = std::string(user ? user : "") + ":" + (pw ? pw : "");
    std::string b64 = b64_encode(creds);
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "Authorization: Basic %s", b64.c_str());
    auth_header_ = String(buf);
}

bool HTTPClient::begin(const String& host, uint16_t port, const String& uri) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "http://%s:%u%s", host.c_str(), port, uri.c_str());
    return begin(String(buf));
}

void HTTPClient::end() { body_.operator=(""); last_code_ = HTTPC_ERROR_NOT_CONNECTED; }

static int do_request(
    const String& url,
    const char* method,
    const String& body,
    String& out_body,
    const String* hdr_lines,
    int hdr_count,
    const String& user_agent,
    const String& auth_header,
    uint32_t timeout_ms)
{
    if (url.length() == 0) {
        std::fprintf(stderr, "[boardghost] HTTP: empty URL\n");
        return HTTPC_ERROR_NOT_CONNECTED;
    }
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAKE:
            std::fprintf(stderr, "[boardghost] HTTP fake %s → 200 empty (%s)\n",
                         method, url.c_str());
            out_body.operator=("");
            return 200;
        case BOARDGHOST_NET_FAIL:
            std::fprintf(stderr, "[boardghost] HTTP fail %s → -1 (%s)\n",
                         method, url.c_str());
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
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
            // User-Agent: sketch's setUserAgent wins; default to a
            // descriptive UA so APIs like CoinGecko that 403 anonymous
            // requests get something useful.
            if (user_agent.length() > 0) {
                curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
            } else {
                curl_easy_setopt(curl, CURLOPT_USERAGENT,
                                 "boardghost-sim/1.0 (+https://github.com/toastmanAu/arduino-board-emulator)");
            }
            // Forward addHeader() entries + optional auth.
            struct curl_slist* hdrs = nullptr;
            for (int i = 0; i < hdr_count; ++i) hdrs = curl_slist_append(hdrs, hdr_lines[i].c_str());
            if (auth_header.length() > 0) hdrs = curl_slist_append(hdrs, auth_header.c_str());
            if (hdrs) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
            // Method dispatch.
            if (std::strcmp(method, "GET") == 0) {
                curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            } else if (std::strcmp(method, "POST") == 0) {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.length());
            } else {
                // PUT, DELETE, PATCH, etc.
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
                if (body.length() > 0) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.length());
                }
            }
            CURLcode rc = curl_easy_perform(curl);
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_easy_cleanup(curl);
            if (hdrs) curl_slist_free_all(hdrs);
            if (rc != CURLE_OK) {
                std::fprintf(stderr, "[boardghost] HTTP real %s → curl error: %s (%s)\n",
                             method, curl_easy_strerror(rc), url.c_str());
                return HTTPC_ERROR_CONNECTION_REFUSED;
            }
            std::fprintf(stderr, "[boardghost] HTTP real %s → %ld (%s)\n",
                         method, http_code, url.c_str());
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

#define BG_DO_REQ(method, body)                                                  \
    last_code_ = do_request(url_, method, body, body_, hdr_lines_, hdr_count_,   \
                            user_agent_, auth_header_, timeout_ms_);             \
    return last_code_;

int HTTPClient::GET()                                { BG_DO_REQ("GET",    String("")); }
int HTTPClient::POST(const String& payload)          { BG_DO_REQ("POST",   payload); }
int HTTPClient::POST(uint8_t* payload, size_t size)  {
    String p; p.concat(""); // empty start
    if (payload && size) {
        // Treat as opaque bytes; HTTPClient API doesn't preserve binary
        // perfectly here but matches the no-Content-Type-set pattern.
        char buf[256]; size_t off = 0;
        while (off < size) {
            size_t n = std::min<size_t>(sizeof(buf) - 1, size - off);
            std::memcpy(buf, payload + off, n);
            buf[n] = '\0';
            p.concat(buf);
            off += n;
        }
    }
    BG_DO_REQ("POST", p);
}
int HTTPClient::PUT(const String& payload)           { BG_DO_REQ("PUT",    payload); }
int HTTPClient::DELETE()                             { BG_DO_REQ("DELETE", String("")); }

String HTTPClient::getString() { return body_; }

// After a successful request the body is buffered in body_ — the sketch's
// "connected" check then drives a download loop. We report the connection as
// "live" once (so the gate opens) and "closed" thereafter, so loops that copy
// bytes via `getStreamPtr()` exit on the next iteration even though our stream
// is empty.
bool HTTPClient::connected() {
    bool was = connected_;
    connected_ = false;
    return was;
}

WiFiClient* HTTPClient::getStreamPtr() {
    connected_ = (last_code_ > 0 && last_code_ < 400);
    return &stream_;
}
