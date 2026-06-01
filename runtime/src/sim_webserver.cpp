// Real backing for the WebServer shim, using vendored cpp-httplib.
//
// ESP32 semantics that drive the design:
//   - Sketch handlers run "single-threaded": each call to handleClient()
//     dispatches at most one request, on the main thread, synchronously.
//     Sketches assume their own globals aren't being mutated under them.
//   - server.arg() / .send() / .uri() / .method() are stateful — they read
//     and write the *currently-handled* request's slot.
//
// cpp-httplib by default dispatches each request on a worker thread. To match
// ESP32's serialised feel without invasively refactoring sketches, we wrap
// every request in a mutex and use member fields for per-request state.
// Concurrent requests queue behind the mutex (slower but matches the model
// sketches expect). Sketches can still scale because most ESP32 dashboards
// see a handful of clicks per minute, not parallel load.
//
// Port handling: server(80) is honoured unless BOARDGHOST_WEBSERVER_PORT
// overrides it (typical Linux dev: 80 needs root, so the env lets you map
// to 18080 etc.). On bind failure we log + skip — the sketch keeps running.

#include "WebServer.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace boardghost_internal {

struct ResponseHeader {
    std::string name;
    std::string value;
};

class WebServerImpl {
public:
    WebServerImpl() = default;
    ~WebServerImpl() {
        stop();
    }

    void start(uint16_t default_port);
    void stop();

    // Route registration — dispatches to the right httplib Server::* method.
    void register_route(const std::string& uri, int method,
                        std::function<void()> handler);
    void register_not_found(std::function<void()> handler) {
        not_found_handler_ = std::move(handler);
        srv_.set_error_handler([this](const httplib::Request& req, httplib::Response& res) {
            if (res.status != 404 || !not_found_handler_) return;
            std::lock_guard<std::mutex> lk(request_mutex_);
            populate_from(req);
            reset_response();
            try { not_found_handler_(); } catch (...) {}
            apply_to(res);
        });
    }

    // ----- response builders, called from the sketch handler -----
    void set_status(int code) { res_status_ = code; }
    void set_body(std::string body) { res_body_ = std::move(body); }
    void set_content_type(std::string ct) { res_content_type_ = std::move(ct); }
    void add_response_header(std::string name, std::string value, bool first) {
        if (first) res_headers_.insert(res_headers_.begin(), {std::move(name), std::move(value)});
        else       res_headers_.push_back({std::move(name), std::move(value)});
    }
    void append_body(const std::string& chunk) { res_body_ += chunk; }
    void enable_cors(bool on) { cors_enabled_ = on; }

    // ----- request introspection, called from the sketch handler -----
    std::string get_arg(const std::string& name) const {
        auto it = params_.find(name);
        return it == params_.end() ? std::string() : it->second;
    }
    bool has_arg(const std::string& name) const { return params_.count(name) > 0; }
    int  arg_count() const { return (int)param_order_.size(); }
    std::string arg_by_index(int i) const {
        if (i < 0 || (size_t)i >= param_order_.size()) return {};
        return params_.at(param_order_[i]);
    }
    std::string arg_name_by_index(int i) const {
        if (i < 0 || (size_t)i >= param_order_.size()) return {};
        return param_order_[i];
    }
    std::string get_header(const std::string& name) const {
        auto it = headers_.find(name);
        return it == headers_.end() ? std::string() : it->second;
    }
    bool has_header(const std::string& name) const { return headers_.count(name) > 0; }
    int  header_count() const { return (int)header_order_.size(); }
    std::string header_by_index(int i) const {
        if (i < 0 || (size_t)i >= header_order_.size()) return {};
        return headers_.at(header_order_[i]);
    }
    std::string header_name_by_index(int i) const {
        if (i < 0 || (size_t)i >= header_order_.size()) return {};
        return header_order_[i];
    }
    std::string get_uri() const { return uri_; }
    std::string get_host_header() const {
        auto it = headers_.find("Host");
        return it == headers_.end() ? std::string() : it->second;
    }
    int get_method() const { return method_; }

private:
    void populate_from(const httplib::Request& req) {
        uri_ = req.path;
        method_ = method_from_string(req.method);
        params_.clear();
        param_order_.clear();
        // cpp-httplib merges query string and form params into req.params.
        // We preserve insertion order via param_order_ so arg(int) matches.
        for (auto it = req.params.begin(); it != req.params.end(); ++it) {
            if (!params_.count(it->first)) param_order_.push_back(it->first);
            params_[it->first] = it->second;
        }
        headers_.clear();
        header_order_.clear();
        for (auto it = req.headers.begin(); it != req.headers.end(); ++it) {
            if (!headers_.count(it->first)) header_order_.push_back(it->first);
            headers_[it->first] = it->second;
        }
    }

    void reset_response() {
        res_status_ = 200;
        res_body_.clear();
        res_content_type_ = "text/plain";
        res_headers_.clear();
    }

    void apply_to(httplib::Response& res) {
        res.status = res_status_;
        // ESP32's WebServer::send treats Content-Type as part of the response
        // call; cpp-httplib has set_content(body, type) — same effect.
        res.set_content(res_body_, res_content_type_);
        for (auto& h : res_headers_) {
            res.set_header(h.name, h.value);
        }
        if (cors_enabled_ && !has_response_header("Access-Control-Allow-Origin")) {
            res.set_header("Access-Control-Allow-Origin", "*");
        }
    }

    bool has_response_header(const std::string& name) const {
        for (auto& h : res_headers_) if (h.name == name) return true;
        return false;
    }

    static int method_from_string(const std::string& m) {
        if (m == "GET")     return 1;
        if (m == "POST")    return 2;
        if (m == "PUT")     return 3;
        if (m == "PATCH")   return 4;
        if (m == "DELETE")  return 5;
        if (m == "OPTIONS") return 6;
        return 0;
    }

public:
    // Public so the WebServer wrapper can take the lock per-call from the
    // user handler. Re-entry from the sketch's send/arg/etc. is fine — they
    // run on the same thread that holds the lock.
    std::mutex request_mutex_;

private:
    httplib::Server srv_;
    std::thread     srv_thread_;
    std::atomic<bool> running_{false};

    // Per-request state — protected by request_mutex_ for the lifetime of
    // a single request dispatch.
    std::string uri_;
    int         method_ = 0;
    std::map<std::string, std::string> params_;
    std::vector<std::string>           param_order_;
    std::map<std::string, std::string> headers_;
    std::vector<std::string>           header_order_;

    int                          res_status_ = 200;
    std::string                  res_body_;
    std::string                  res_content_type_ = "text/plain";
    std::vector<ResponseHeader>  res_headers_;
    bool                         cors_enabled_ = false;

    std::function<void()>        not_found_handler_;
};

void WebServerImpl::start(uint16_t default_port) {
    if (running_.load()) return;
    uint16_t port = default_port;
    if (const char* env = std::getenv("BOARDGHOST_WEBSERVER_PORT")) {
        int p = std::atoi(env);
        if (p > 0 && p < 65536) port = (uint16_t)p;
    }
    // 0.0.0.0 so the host browser can reach it from other interfaces too.
    bool bound = srv_.bind_to_port("0.0.0.0", port);
    if (!bound) {
        std::fprintf(stderr,
            "[boardghost] WebServer: bind to port %u failed; sketch's web UI "
            "will not be reachable. Try BOARDGHOST_WEBSERVER_PORT=18080.\n",
            port);
        return;
    }
    running_.store(true);
    srv_thread_ = std::thread([this]() {
        srv_.listen_after_bind();
        running_.store(false);
    });
    std::fprintf(stderr,
        "[boardghost] WebServer: listening on http://localhost:%u\n", port);
}

void WebServerImpl::stop() {
    if (!running_.load() && !srv_thread_.joinable()) return;
    srv_.stop();
    if (srv_thread_.joinable()) srv_thread_.join();
    running_.store(false);
}

void WebServerImpl::register_route(const std::string& uri, int method,
                                   std::function<void()> handler) {
    // Wrap the sketch's handler so we set per-request state under the mutex,
    // invoke the closure, then transfer the response back to cpp-httplib.
    auto wrap = [this, h = std::move(handler)](const httplib::Request& req,
                                                httplib::Response& res) {
        std::lock_guard<std::mutex> lk(request_mutex_);
        populate_from(req);
        reset_response();
        try { h(); } catch (...) {
            res_status_ = 500;
            res_body_   = "sim handler threw";
        }
        apply_to(res);
    };
    // method == 0 (HTTP_ANY) means register for any of the common verbs.
    switch (method) {
        case 1:  srv_.Get(uri, wrap); break;
        case 2:  srv_.Post(uri, wrap); break;
        case 3:  srv_.Put(uri, wrap); break;
        case 4:  srv_.Patch(uri, wrap); break;
        case 5:  srv_.Delete(uri, wrap); break;
        case 6:  srv_.Options(uri, wrap); break;
        default:
            srv_.Get(uri, wrap);
            srv_.Post(uri, wrap);
            srv_.Put(uri, wrap);
            srv_.Patch(uri, wrap);
            srv_.Delete(uri, wrap);
            srv_.Options(uri, wrap);
            break;
    }
}

}  // namespace boardghost_internal

// ---------------- Public WebServer surface ----------------

namespace bgi = boardghost_internal;

WebServer::WebServer(uint16_t port)
    : port_(port), impl_(std::make_unique<bgi::WebServerImpl>()) {}

WebServer::~WebServer() = default;

void WebServer::begin()              { impl_->start(port_); }
void WebServer::begin(uint16_t port) { port_ = port; impl_->start(port_); }
void WebServer::stop()               { impl_->stop(); }
void WebServer::close()              { impl_->stop(); }

void WebServer::on(const char* uri, THandlerFunction handler) {
    impl_->register_route(uri ? uri : "/", 0 /*HTTP_ANY*/, std::move(handler));
}
void WebServer::on(const char* uri, HTTPMethod method, THandlerFunction handler) {
    impl_->register_route(uri ? uri : "/", (int)method, std::move(handler));
}
void WebServer::on(const char* uri, HTTPMethod method, THandlerFunction handler,
                   THandlerFunctionUpload /*upload*/) {
    // Upload phase is not driven in the sim — multipart parsing happens but
    // the sketch's upload handler isn't invoked per-chunk. Files arrive as
    // the request body; sketches that need streamed upload should be flagged.
    impl_->register_route(uri ? uri : "/", (int)method, std::move(handler));
}
void WebServer::onNotFound(THandlerFunction handler)         { impl_->register_not_found(std::move(handler)); }
void WebServer::onFileUpload(THandlerFunctionUpload /*h*/)   {}

void WebServer::send(int code) {
    impl_->set_status(code);
    impl_->set_body({});
}
void WebServer::send(int code, const char* content_type, const String& content) {
    impl_->set_status(code);
    if (content_type) impl_->set_content_type(content_type);
    impl_->set_body(std::string(content.c_str() ? content.c_str() : ""));
}
void WebServer::send(int code, const String& content_type, const String& content) {
    send(code, content_type.c_str(), content);
}
void WebServer::send(int code, const char* content_type, const char* content) {
    impl_->set_status(code);
    if (content_type) impl_->set_content_type(content_type);
    impl_->set_body(content ? std::string(content) : std::string());
}
void WebServer::send_P(int code, const char* content_type, const char* content) {
    send(code, content_type, content);
}
void WebServer::sendHeader(const String& name, const String& value, bool first) {
    impl_->add_response_header(name.c_str() ? name.c_str() : "",
                               value.c_str() ? value.c_str() : "",
                               first);
}
void WebServer::sendContent(const String& content) {
    impl_->append_body(content.c_str() ? content.c_str() : "");
}

String WebServer::arg(const char* name) const {
    return name ? String(impl_->get_arg(name).c_str()) : String();
}
String WebServer::arg(int i) const               { return String(impl_->arg_by_index(i).c_str()); }
String WebServer::argName(int i) const           { return String(impl_->arg_name_by_index(i).c_str()); }
int    WebServer::args() const                   { return impl_->arg_count(); }
bool   WebServer::hasArg(const char* name) const { return name && impl_->has_arg(name); }

String WebServer::header(const char* name) const {
    return name ? String(impl_->get_header(name).c_str()) : String();
}
String WebServer::header(int i) const               { return String(impl_->header_by_index(i).c_str()); }
String WebServer::headerName(int i) const           { return String(impl_->header_name_by_index(i).c_str()); }
int    WebServer::headers() const                   { return impl_->header_count(); }
bool   WebServer::hasHeader(const char* name) const { return name && impl_->has_header(name); }
String WebServer::hostHeader() const                { return String(impl_->get_host_header().c_str()); }
String WebServer::uri() const                       { return String(impl_->get_uri().c_str()); }
HTTPMethod WebServer::method() const                { return (HTTPMethod)impl_->get_method(); }

void WebServer::collectHeaders(const char* /*headerKeys*/[], const size_t /*headerKeysCount*/) {
    // cpp-httplib exposes all request headers; we don't need to opt-in.
}

void WebServer::enableCORS(bool enable) { impl_->enable_cors(enable); }
