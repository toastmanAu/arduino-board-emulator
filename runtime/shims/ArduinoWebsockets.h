// BoardGhost shim for ArduinoWebsockets (https://github.com/gilmaimon/ArduinoWebsockets).
// In sim builds, this header wins over the real library at preprocess time.
// All operations are routed through sim_net.h so behavior matches the
// BOARDGHOST_NET=fake|fail|real mode.
//
// Surface coverage:
//   - websockets::WebsocketsClient
//   - websockets::WebsocketsMessage
//   - WSInterfaceEvents / WSInterfaceMessages
//   - onMessage(handler), onEvent(handler)
//   - connect(url), close(), poll(), send(), sendBinary(), available()
//
// Real-mode WebSocket support via libcurl is future work; for now REAL falls
// back to FAKE behavior with a one-line stderr notice.
#pragma once

#include <string>
#include <functional>
#include <cstdio>
#include "sim_net.h"

namespace websockets {

class WebsocketsMessage {
public:
    WebsocketsMessage() = default;
    explicit WebsocketsMessage(const std::string& data) : data_(data) {}
    const std::string& data() const { return data_; }
    std::string c_str() const { return data_; }
    bool isText() const { return true; }
    bool isBinary() const { return false; }
    bool isPing() const { return false; }
    bool isPong() const { return false; }
    bool isComplete() const { return true; }
    int length() const { return (int)data_.size(); }
private:
    std::string data_;
};

enum class WebsocketsEvent {
    ConnectionOpened,
    ConnectionClosed,
    GotPing,
    GotPong,
};

class WebsocketsClient {
public:
    using MessageCallback = std::function<void(WebsocketsMessage)>;
    using EventCallback   = std::function<void(WebsocketsEvent, std::string)>;

    bool connect(const std::string& url) {
        url_ = url;
        if (sim_net_mode() == BOARDGHOST_NET_FAIL) return false;
        connected_ = true;
        if (event_cb_) event_cb_(WebsocketsEvent::ConnectionOpened, "");
        return true;
    }
    bool connect(const char* url) { return connect(std::string(url)); }
    bool connect(const char* host, int port, const char* path) {
        char url[512];
        snprintf(url, sizeof(url), "ws://%s:%d%s", host, port, path);
        return connect(std::string(url));
    }

    void close() {
        if (connected_ && event_cb_) {
            event_cb_(WebsocketsEvent::ConnectionClosed, "");
        }
        connected_ = false;
    }

    bool available() const { return connected_; }
    bool ping() { return connected_; }

    bool send(const std::string&) { return connected_; }
    bool send(const char*)        { return connected_; }
    bool sendBinary(const char*, size_t) { return connected_; }

    void onMessage(MessageCallback cb) { message_cb_ = std::move(cb); }
    void onEvent(EventCallback cb)     { event_cb_   = std::move(cb); }

    void poll() {
        if (!connected_) return;
        if (sim_net_mode() == BOARDGHOST_NET_REAL) {
            static bool warned = false;
            if (!warned) {
                fprintf(stderr,
                    "[boardghost] WebsocketsClient: REAL mode not yet wired "
                    "(libcurl WS pending) — behaving as FAKE.\n");
                warned = true;
            }
        }
    }

private:
    bool connected_ = false;
    std::string url_;
    MessageCallback message_cb_;
    EventCallback   event_cb_;
};

} // namespace websockets

using websockets::WebsocketsClient;
using websockets::WebsocketsMessage;
using websockets::WebsocketsEvent;
