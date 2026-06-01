// Backing for ArduinoWebsockets.h: real ws:// and wss:// via cpp-httplib's
// WebSocketClient when BOARDGHOST_NET=real; no-op stubs in fake/fail mode.
//
// Threading model. The Arduino API is: register callbacks with onMessage /
// onEvent, then call poll() from loop() so callbacks fire on the sketch
// thread. cpp-httplib's WS client is blocking. To bridge:
//   - On connect(), spawn a reader thread that loops on WS::read.
//   - Pushes (message, event) records onto a mutex-guarded queue.
//   - poll() drains the queue from the sketch's loop() thread and invokes
//     the user's callbacks there. No callback ever fires off-thread.

#include "ArduinoWebsockets.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace boardghost_internal {

struct EventRecord {
    bool                            is_message;
    websockets::WebsocketsMessage   message;        // when is_message
    websockets::WebsocketsEvent     event;          // when !is_message
    std::string                     event_data;     // close reason etc.
};

class WsClientImpl {
public:
    WsClientImpl() = default;
    ~WsClientImpl() { close(); }

    bool connect(const std::string& url) {
        close();
        url_ = url;
        if (sim_net_mode() == BOARDGHOST_NET_FAIL) return false;
        if (sim_net_mode() == BOARDGHOST_NET_FAKE || url.empty()) {
            connected_.store(true);
            push_event(websockets::WebsocketsEvent::ConnectionOpened, "");
            return true;
        }

        // Real mode — drive cpp-httplib's WebSocketClient on a background
        // thread so reads don't block the sketch.
        client_ = std::make_unique<httplib::ws::WebSocketClient>(url);
        if (!client_->is_valid()) {
            client_.reset();
            return false;
        }
        if (!client_->connect()) {
            client_.reset();
            return false;
        }
        connected_.store(true);
        should_stop_.store(false);
        reader_ = std::thread([this]() { reader_loop(); });
        push_event(websockets::WebsocketsEvent::ConnectionOpened, "");
        return true;
    }

    void close() {
        if (!connected_.exchange(false)) {
            // Not connected; still clean up thread if it lingered.
            should_stop_.store(true);
            if (reader_.joinable()) reader_.join();
            client_.reset();
            return;
        }
        should_stop_.store(true);
        if (client_) client_->close();
        if (reader_.joinable()) reader_.join();
        client_.reset();
        push_event(websockets::WebsocketsEvent::ConnectionClosed, "");
    }

    bool available() const { return connected_.load(); }

    bool ping() {
        // cpp-httplib's WS client doesn't expose explicit ping send — its
        // heartbeat handles it internally. Sketches that ping for liveness
        // get a truthful "yes connected" reply.
        return connected_.load();
    }

    bool send(const std::string& s) {
        if (!connected_.load()) return false;
        if (!client_) return true;   // fake mode: pretend success
        return client_->send(s);
    }

    bool send_binary(const char* data, size_t len) {
        if (!connected_.load()) return false;
        if (!client_ || !data) return true;
        return client_->send(data, len);
    }

    void on_message(websockets::WebsocketsClient::MessageCallback cb) {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        message_cb_ = std::move(cb);
    }
    void on_event(websockets::WebsocketsClient::EventCallback cb) {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        event_cb_ = std::move(cb);
    }

    void poll() {
        std::deque<EventRecord> drained;
        {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            drained.swap(queue_);
        }
        // Snapshot callbacks under the cb mutex so the caller can re-register
        // mid-poll without crashing.
        websockets::WebsocketsClient::MessageCallback mcb;
        websockets::WebsocketsClient::EventCallback   ecb;
        {
            std::lock_guard<std::mutex> lk(cb_mutex_);
            mcb = message_cb_;
            ecb = event_cb_;
        }
        for (auto& r : drained) {
            try {
                if (r.is_message) { if (mcb) mcb(r.message); }
                else              { if (ecb) ecb(r.event, r.event_data); }
            } catch (...) { /* user callback threw — swallow */ }
        }
    }

private:
    void push_message(websockets::WebsocketsMessage msg) {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        EventRecord r{true, std::move(msg), {}, {}};
        queue_.push_back(std::move(r));
    }
    void push_event(websockets::WebsocketsEvent ev, std::string data) {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        EventRecord r{false, {}, ev, std::move(data)};
        queue_.push_back(std::move(r));
    }

    void reader_loop() {
        // Short read timeout so should_stop_ is observed quickly when the
        // sketch calls close().
        client_->set_read_timeout(0, 200 * 1000);  // 200ms
        std::string msg;
        while (!should_stop_.load()) {
            auto rc = client_->read(msg);
            if (rc == httplib::ws::ReadResult::Fail) {
                if (!client_->is_open()) {
                    connected_.store(false);
                    push_event(websockets::WebsocketsEvent::ConnectionClosed, "");
                    return;
                }
                // Timeout — loop again to re-check should_stop_.
                continue;
            }
            bool is_binary = (rc == httplib::ws::ReadResult::Binary);
            push_message(websockets::WebsocketsMessage(std::move(msg), is_binary));
            msg.clear();
        }
    }

    std::string url_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> should_stop_{false};
    std::unique_ptr<httplib::ws::WebSocketClient> client_;
    std::thread reader_;
    std::mutex  queue_mutex_;
    std::deque<EventRecord> queue_;
    std::mutex  cb_mutex_;
    websockets::WebsocketsClient::MessageCallback message_cb_;
    websockets::WebsocketsClient::EventCallback   event_cb_;
};

}  // namespace boardghost_internal

namespace bgi = boardghost_internal;
namespace websockets {

WebsocketsClient::WebsocketsClient() : impl_(std::make_unique<bgi::WsClientImpl>()) {}
WebsocketsClient::~WebsocketsClient() = default;

bool WebsocketsClient::connect(const std::string& url) { return impl_->connect(url); }
void WebsocketsClient::close()                          { impl_->close(); }
bool WebsocketsClient::available() const                { return impl_->available(); }
bool WebsocketsClient::ping()                           { return impl_->ping(); }
bool WebsocketsClient::send(const std::string& s)       { return impl_->send(s); }
bool WebsocketsClient::sendBinary(const char* d, size_t n) { return impl_->send_binary(d, n); }
void WebsocketsClient::onMessage(MessageCallback cb)    { impl_->on_message(std::move(cb)); }
void WebsocketsClient::onEvent(EventCallback cb)        { impl_->on_event(std::move(cb)); }
void WebsocketsClient::poll()                           { impl_->poll(); }

}  // namespace websockets
