// BoardGhost shim for ArduinoWebsockets (gilmaimon/ArduinoWebsockets).
// In sim builds, this header wins over the real library at preprocess time.
//
// BOARDGHOST_NET=real:  wired through cpp-httplib's WebSocketClient. Real
//                       ws:// and wss:// handshakes; reads happen on a
//                       background thread and get drained by poll() so
//                       callbacks fire on the sketch's loop() thread, matching
//                       the Arduino single-threaded contract.
// BOARDGHOST_NET=fake:  connect() succeeds immediately, no traffic flows.
// BOARDGHOST_NET=fail:  connect() returns false.
#pragma once

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include "sim_net.h"

namespace boardghost_internal { class WsClientImpl; }

namespace websockets {

class WebsocketsMessage {
public:
    WebsocketsMessage() = default;
    WebsocketsMessage(std::string data, bool is_binary)
        : data_(std::move(data)), is_binary_(is_binary) {}

    const std::string& data() const { return data_; }
    const char*        c_str() const { return data_.c_str(); }
    int                length() const { return (int)data_.size(); }
    bool isText()     const { return !is_binary_; }
    bool isBinary()   const { return is_binary_;  }
    bool isPing()     const { return false; }   // cpp-httplib handles auto
    bool isPong()     const { return false; }
    bool isComplete() const { return true; }

private:
    std::string data_;
    bool        is_binary_ = false;
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

    WebsocketsClient();
    ~WebsocketsClient();

    // Disable copy — owns a background thread + queue.
    WebsocketsClient(const WebsocketsClient&) = delete;
    WebsocketsClient& operator=(const WebsocketsClient&) = delete;

    bool connect(const std::string& url);
    bool connect(const char* url) { return connect(std::string(url ? url : "")); }
    bool connect(const char* host, int port, const char* path) {
        char url[1024];
        std::snprintf(url, sizeof(url), "ws://%s:%d%s",
                      host ? host : "localhost", port, path ? path : "/");
        return connect(std::string(url));
    }

    void close();
    bool available() const;
    bool ping();

    bool send(const std::string& s);
    bool send(const char* s) { return send(std::string(s ? s : "")); }
    bool sendBinary(const char* data, size_t len);
    bool sendBinary(const std::string& data) { return sendBinary(data.data(), data.size()); }

    void onMessage(MessageCallback cb);
    void onEvent(EventCallback cb);

    // Drain queued messages + events into the user's callbacks. Sketches call
    // this from loop() — matches Arduino semantics where callbacks fire on
    // the main thread, not the network thread.
    void poll();

private:
    std::unique_ptr<boardghost_internal::WsClientImpl> impl_;
};

}  // namespace websockets

using websockets::WebsocketsClient;
using websockets::WebsocketsMessage;
using websockets::WebsocketsEvent;
