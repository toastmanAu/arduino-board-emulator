// AsyncWebServer backing — own cpp-httplib server, per-request state objects.
#include "ESPAsyncWebServer.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace boardghost_internal {

// Set true by a matched route wrapper; reset before routing each request.
// Lets the not-found error handler distinguish "no route matched" (real 404)
// from "a matched route chose to send 404" (must NOT trigger onNotFound).
static thread_local bool tls_route_handled = false;

// Per-request scratch the AsyncWebServerRequest reads/writes. One per request,
// stack-lived inside the httplib handler — naturally concurrent, no mutex.
struct AsyncReqState {
    const httplib::Request* req = nullptr;
    httplib::Response*      res = nullptr;
    std::vector<AsyncWebParameter> params;   // parsed query+form params
    bool responded = false;
};

static uint16_t resolve_async_port(uint16_t def) {
    if (const char* e = std::getenv("BOARDGHOST_ASYNC_WEBSERVER_PORT")) {
        int p = std::atoi(e);
        if (p > 0 && p < 65536) return (uint16_t)p;
    }
    return def;
}

// Expand %TOKEN% using the processor callback (ESPAsyncWebServer semantics).
static std::string apply_template(const std::string& in, const AwsTemplateProcessor& cb) {
    if (!cb) return in;
    std::string out; out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        if (in[i] == '%') {
            size_t end = in.find('%', i + 1);
            if (end != std::string::npos) {
                std::string token = in.substr(i + 1, end - i - 1);
                if (token.empty()) { out += '%'; i = end + 1; continue; }  // "%%" -> "%"
                out += cb(String(token.c_str())).c_str();
                i = end + 1;
                continue;
            }
        }
        out += in[i++];
    }
    return out;
}

class EventSourceImpl {
    static constexpr size_t kMaxQueueDepth = 256;
public:
    // One connected browser.
    struct Client {
        std::mutex m;
        std::condition_variable cv;
        std::deque<std::string> queue;   // formatted SSE frames
        bool closed = false;
    };

    // Register the streaming route on the httplib server.
    void attach(httplib::Server& srv, const std::string& url) {
        srv.Get(url, [this](const httplib::Request&, httplib::Response& res) {
            auto client = std::make_shared<Client>();
            { std::lock_guard<std::mutex> lk(clients_mutex_); clients_.insert(client); }
            res.set_chunked_content_provider("text/event-stream",
                [this, client](size_t, httplib::DataSink& sink) {
                    std::unique_lock<std::mutex> lk(client->m);
                    client->cv.wait(lk, [&]{ return client->closed || !client->queue.empty() || shutdown_.load(); });
                    if (shutdown_.load() || client->closed) return false;   // end the stream
                    while (!client->queue.empty()) {
                        std::string frame = std::move(client->queue.front());
                        client->queue.pop_front();
                        lk.unlock();
                        if (!sink.write(frame.data(), frame.size())) { lk.lock(); return false; }
                        lk.lock();
                    }
                    return true;
                },
                [this, client](bool) {   // on connection close
                    std::lock_guard<std::mutex> lk(clients_mutex_);
                    clients_.erase(client);
                });
        });
    }

    void send(const std::string& message, const std::string& event,
              uint32_t id, uint32_t reconnect) {
        std::string frame;
        if (reconnect) frame += "retry: " + std::to_string(reconnect) + "\n";
        if (id)        frame += "id: " + std::to_string(id) + "\n";
        if (!event.empty()) frame += "event: " + event + "\n";
        frame += "data: " + message + "\n\n";
        std::lock_guard<std::mutex> lk(clients_mutex_);
        for (auto& c : clients_) {
            std::lock_guard<std::mutex> cl(c->m);
            while (c->queue.size() >= kMaxQueueDepth) c->queue.pop_front();   // drop oldest; keep latest telemetry
            c->queue.push_back(frame);
            c->cv.notify_one();
        }
    }

    size_t count() const {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        return clients_.size();
    }

    // Release all held connections so the server thread can join.
    void shutdown() {
        // Set the atomic first: any client that connects in the window between here
        // and acquiring clients_mutex_ will observe shutdown_==true in its wait
        // predicate and exit immediately. Do NOT hold clients_mutex_ across both —
        // the GET handler also takes it, which would deadlock.
        shutdown_ = true;
        std::lock_guard<std::mutex> lk(clients_mutex_);
        for (auto& c : clients_) {
            std::lock_guard<std::mutex> cl(c->m);
            c->closed = true;
            c->cv.notify_all();
        }
    }

private:
    mutable std::mutex clients_mutex_;
    std::set<std::shared_ptr<Client>> clients_;
    std::atomic<bool> shutdown_{false};
};

class AsyncServerImpl {
public:
    explicit AsyncServerImpl(uint16_t port) : port_(port) {}
    ~AsyncServerImpl() { stop(); }

    void add_route(const std::string& uri, WebRequestMethodComposite method,
                   ArRequestHandlerFunction handler) {
        auto wrap = [h = std::move(handler)](const httplib::Request& req, httplib::Response& res) {
            AsyncReqState st; st.req = &req; st.res = &res;
            for (auto it = req.params.begin(); it != req.params.end(); ++it)
                st.params.emplace_back(String(it->first.c_str()), String(it->second.c_str()));
            AsyncWebServerRequest r(&st);
            try { h(&r); } catch (...) { res.status = 500; res.set_content("handler threw", "text/plain"); }
            if (!st.responded) { res.status = 404; }
            tls_route_handled = true;   // mark that a route matched (even on deliberate 404)
        };
        if (method & HTTP_GET)    srv_.Get(uri, wrap);
        if (method & HTTP_POST)   srv_.Post(uri, wrap);
        if (method & HTTP_PUT)    srv_.Put(uri, wrap);
        if (method & HTTP_PATCH)  srv_.Patch(uri, wrap);
        if (method & HTTP_DELETE) srv_.Delete(uri, wrap);
        if (method & HTTP_OPTIONS) srv_.Options(uri, wrap);
        // HTTP_HEAD is intentionally not registered: httplib auto-handles HEAD
        // for any registered GET route, so no explicit registration is needed.
    }

    void set_not_found(ArRequestHandlerFunction handler) {
        srv_.set_error_handler([h = std::move(handler)](const httplib::Request& req, httplib::Response& res) {
            if (tls_route_handled) return;  // a matched route owns this response (even a 404)
            if (res.status != 404) return;
            AsyncReqState st; st.req = &req; st.res = &res;
            AsyncWebServerRequest r(&st);
            try { h(&r); } catch (...) {}
        });
    }

    void serve_static(const std::string& uri, fs::FS& fs, const std::string& path) {
        // Map a URI prefix to a single file (the common dashboard case:
        // serveStatic("/", SPIFFS, "/index.html")).
        // NOTE: `fs` is captured by reference for the server's lifetime. Callers must
        // ensure it outlives the server — true for the usual globals (SPIFFS/LittleFS).
        srv_.Get(uri, [fsp = &fs, path](const httplib::Request&, httplib::Response& res) {
            fs::File f = fsp->open(path.c_str(), "r");
            if (!f) { res.status = 404; tls_route_handled = true; return; }
            std::string body; size_t n = f.size(); body.resize(n);
            if (n) f.read((uint8_t*)&body[0], n);
            res.set_content(body, "text/html");
            tls_route_handled = true;
        });
    }

    httplib::Server& server() { return srv_; }

    void attach_events(std::shared_ptr<EventSourceImpl> es, const std::string& url) {
        es->attach(srv_, url);
        event_sources_.push_back(std::move(es));
    }

    void start() {
        if (running_.load()) return;
        // Reset the per-request route-handled flag before each request so that
        // the onNotFound error handler can distinguish "no route matched" from
        // "a matched route deliberately returned 404".
        srv_.set_pre_routing_handler([](const httplib::Request&, httplib::Response&) {
            tls_route_handled = false;
            return httplib::Server::HandlerResponse::Unhandled;  // continue normal routing
        });
        uint16_t port = resolve_async_port(port_);
        if (!srv_.bind_to_port("0.0.0.0", port)) {
            std::fprintf(stderr, "[boardghost] AsyncWebServer: bind port %u failed.\n", port);
            return;
        }
        running_.store(true);
        thread_ = std::thread([this]() { srv_.listen_after_bind(); running_.store(false); });
        std::fprintf(stderr, "[boardghost] AsyncWebServer: listening on http://localhost:%u\n", port);
    }

    void stop() {
        if (!running_.load() && !thread_.joinable()) return;
        for (auto& es : event_sources_) es->shutdown();
        srv_.stop();
        if (thread_.joinable()) thread_.join();
        running_.store(false);
    }

private:
    uint16_t port_;
    httplib::Server srv_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::vector<std::shared_ptr<EventSourceImpl>> event_sources_;
};

}  // namespace boardghost_internal

namespace bgi = boardghost_internal;

// ---- AsyncWebServerRequest ----
String AsyncWebServerRequest::url() const { return String(st_->req->path.c_str()); }
String AsyncWebServerRequest::host() const {
    auto it = st_->req->headers.find("Host");
    return it == st_->req->headers.end() ? String() : String(it->second.c_str());
}
WebRequestMethodComposite AsyncWebServerRequest::method() const {
    const std::string& m = st_->req->method;
    if (m == "GET") return HTTP_GET;     if (m == "POST") return HTTP_POST;
    if (m == "PUT") return HTTP_PUT;     if (m == "PATCH") return HTTP_PATCH;
    if (m == "DELETE") return HTTP_DELETE; if (m == "OPTIONS") return HTTP_OPTIONS;
    if (m == "HEAD") return HTTP_HEAD;   return HTTP_ANY;
}
int AsyncWebServerRequest::params() const { return (int)st_->params.size(); }
bool AsyncWebServerRequest::hasParam(const String& name, bool, bool) const {
    for (auto& p : st_->params) if (p.name() == name) return true;
    return false;
}
const AsyncWebParameter* AsyncWebServerRequest::getParam(const String& name, bool, bool) const {
    for (auto& p : st_->params) if (p.name() == name) return &p;
    return nullptr;
}
const AsyncWebParameter* AsyncWebServerRequest::getParam(size_t idx) const {
    return idx < st_->params.size() ? &st_->params[idx] : nullptr;
}
String AsyncWebServerRequest::arg(const String& name) const {
    auto* p = getParam(name); return p ? p->value() : String();
}
bool AsyncWebServerRequest::hasHeader(const String& name) const {
    return st_->req->headers.find(name.c_str()) != st_->req->headers.end();
}
String AsyncWebServerRequest::header(const String& name) const {
    auto it = st_->req->headers.find(name.c_str());
    return it == st_->req->headers.end() ? String() : String(it->second.c_str());
}
void AsyncWebServerRequest::send(int code, const String& contentType, const String& content) {
    st_->res->status = code;
    st_->res->set_content(content.c_str() ? content.c_str() : "",
                          contentType.length() ? contentType.c_str() : "text/plain");
    st_->responded = true;
}
void AsyncWebServerRequest::send_P(int code, const String& contentType, const char* content,
                                   AwsTemplateProcessor processor) {
    std::string body = bgi::apply_template(content ? content : "", processor);
    st_->res->status = code;
    st_->res->set_content(body, contentType.length() ? contentType.c_str() : "text/html");
    st_->responded = true;
}
void AsyncWebServerRequest::send(fs::FS& fs, const String& path, const String& contentType,
                                 bool /*download*/, AwsTemplateProcessor processor) {
    fs::File f = fs.open(path.c_str(), "r");
    if (!f) { st_->res->status = 404; st_->responded = true; return; }
    std::string body; size_t n = f.size(); body.resize(n);
    if (n) f.read((uint8_t*)&body[0], n);
    if (processor) body = bgi::apply_template(body, processor);
    st_->res->status = 200;
    st_->res->set_content(body, contentType.length() ? contentType.c_str() : "text/html");
    st_->responded = true;
}
void AsyncWebServerRequest::redirect(const String& url) {
    st_->res->status = 302;
    st_->res->set_header("Location", url.c_str());
    st_->responded = true;
}

// ---- AsyncWebServer ----
AsyncWebServer::AsyncWebServer(uint16_t port) : impl_(std::make_unique<bgi::AsyncServerImpl>(port)) {}
AsyncWebServer::~AsyncWebServer() = default;
void AsyncWebServer::on(const char* uri, ArRequestHandlerFunction handler) {
    impl_->add_route(uri ? uri : "/", HTTP_ANY, std::move(handler));
}
void AsyncWebServer::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction handler) {
    impl_->add_route(uri ? uri : "/", method, std::move(handler));
}
void AsyncWebServer::onNotFound(ArRequestHandlerFunction handler) { impl_->set_not_found(std::move(handler)); }
void AsyncWebServer::serveStatic(const char* uri, fs::FS& fs, const char* path, const char*) {
    impl_->serve_static(uri ? uri : "/", fs, path ? path : "/");
}
void AsyncWebServer::addHandler(AsyncEventSource* source) {
    if (source) impl_->attach_events(source->shared_impl(), source->url().c_str());
}
void AsyncWebServer::begin() { impl_->start(); }
void AsyncWebServer::end()   { impl_->stop(); }

// ---- AsyncEventSource ----
AsyncEventSource::AsyncEventSource(const String& url)
    : url_(url), impl_(std::make_shared<bgi::EventSourceImpl>()) {}
AsyncEventSource::~AsyncEventSource() = default;
void AsyncEventSource::onConnect(std::function<void()> /*cb*/) { /* connect callback not surfaced in sim */ }
void AsyncEventSource::send(const char* message, const char* event, uint32_t id, uint32_t reconnect) {
    impl_->send(message ? message : "", event ? event : "", id, reconnect);
}
size_t AsyncEventSource::count() const { return impl_->count(); }
