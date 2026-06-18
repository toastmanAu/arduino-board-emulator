// Mirror server — the LAN-capable observe/act surface for the agentic control
// plane and the Android companion. See sim_mirror.h for the env contract.
//
// This is deliberately a SEPARATE httplib::Server from sim_devtools.cpp: the
// devtools server stays pinned to 127.0.0.1 (its /scanner/inject mutates sketch
// I/O), while this server is the only one that may bind 0.0.0.0 — and then only
// with a token. All /mirror/* routes are gated by a single pre-routing handler
// so any route added in later phases is authorized automatically.
//
// Phase 1 scope: lifecycle (start from runtime init), bind decision, token gate.
// Content routes (/screen.png, /mirror/display, /mirror/audio, /mirror/touch)
// arrive in Phases 2–4.

#include "sim_mirror.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

namespace {

// Cap any request body a future mirror route might read. Phase 1 has no POST
// routes yet, but setting this now means Phase 4's /mirror/touch inherits a
// sane limit instead of accepting an unbounded body.
constexpr size_t kMirrorMaxBody = 64 * 1024;  // 64 KB

std::atomic<bool> g_running{false};
std::unique_ptr<httplib::Server> g_srv;
std::thread g_thread;
std::string g_token;  // captured at start; read by the pre-routing gate

// True when the mirror surface is switched off entirely (CI, paranoia). Note
// this is distinct from the bind decision: an unset BOARDGHOST_MIRROR still
// runs the server on loopback so the localhost agent loop works without a
// token; only an explicit "off"/"0" (or devtools off) suppresses it.
bool mirror_disabled() {
    auto off = [](const char* v) {
        return v && (std::strcmp(v, "off") == 0 || std::strcmp(v, "0") == 0);
    };
    if (off(std::getenv("BOARDGHOST_MIRROR"))) return true;
    if (off(std::getenv("BOARDGHOST_DEVTOOLS"))) {
        // Coupled by design (BOARDGHOST_DEVTOOLS=off is the global "no HTTP
        // surfaces" switch), but say so — otherwise a CI profile that sets it
        // turns a BOARDGHOST_MIRROR=lan request into a silent no-op.
        std::fprintf(stderr,
            "[boardghost] mirror: suppressed because BOARDGHOST_DEVTOOLS=off\n");
        return true;
    }
    return false;
}

uint16_t mirror_port() {
    if (const char* env = std::getenv("BOARDGHOST_MIRROR_PORT"); env && *env) {
        int p = std::atoi(env);
        if (p > 0 && p < 65536) return (uint16_t)p;
    }
    return 18082;  // distinct from devtools 18081 so both coexist
}

}  // namespace

namespace boardghost {
namespace mirror {

BindDecision decide_bind(const char* mode, bool token_present) {
    // Only the literal "lan" requests network reach. nullptr/""/"off"/unknown
    // all fall through to loopback — fail closed on anything we don't recognise.
    const bool lan_requested = (mode != nullptr) && std::strcmp(mode, "lan") == 0;
    if (lan_requested) {
        // SECURITY: the LAN socket opens ONLY with a token. Without one we stay
        // on loopback and flag a warning so the caller can tell the user why.
        return BindDecision{/*bind_lan=*/token_present,
                            /*warn_no_token=*/!token_present};
    }
    return BindDecision{/*bind_lan=*/false, /*warn_no_token=*/false};
}

bool token_equal(const std::string& a, const std::string& b) {
    // A length check is fine to short-circuit — token length isn't the secret.
    if (a.size() != b.size()) return false;
    // Constant-time over the common length: OR every byte difference into one
    // accumulator so the loop runs the same number of iterations and does the
    // same work regardless of where (or whether) the bytes diverge. Never
    // short-circuit here — an early return would leak the matching-prefix
    // length through timing.
    // volatile so the accumulating loop isn't dead-store-eliminated into an
    // early-out by an aggressive optimizer, which would reintroduce a timing
    // side channel.
    volatile unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

bool authorized(const std::string& configured, const std::string& header) {
    // No token configured → loopback-only dev convenience: allow. (A LAN bind
    // is impossible without a token, enforced by decide_bind, so "no token"
    // implies we're on loopback.)
    if (configured.empty()) return true;
    return token_equal(configured, header);
}

}  // namespace mirror
}  // namespace boardghost

extern "C" void boardghost_mirror_start(void) {
    if (mirror_disabled()) return;
    if (g_running.load()) return;

    const char* mode = std::getenv("BOARDGHOST_MIRROR");
    if (const char* t = std::getenv("BOARDGHOST_MIRROR_TOKEN"); t) g_token = t;

    auto decision = boardghost::mirror::decide_bind(mode, !g_token.empty());
    if (decision.bind_lan && g_token.size() < 16) {
        // Soft warning only — don't override the operator's explicit choice,
        // but a short token on a LAN bind is brute-forceable (no 401 throttle).
        std::fprintf(stderr,
            "[boardghost] WARNING: BOARDGHOST_MIRROR_TOKEN is short (%zu chars) "
            "for a LAN bind — use >=32 random chars.\n", g_token.size());
    }
    if (decision.warn_no_token) {
        std::fprintf(stderr,
            "[boardghost] WARNING: BOARDGHOST_MIRROR=lan requested but "
            "BOARDGHOST_MIRROR_TOKEN is unset — staying on loopback. Set a token "
            "to expose the mirror on the LAN.\n");
    }
    const char* bind_addr = decision.bind_lan ? "0.0.0.0" : "127.0.0.1";
    uint16_t port = mirror_port();

    g_srv = std::make_unique<httplib::Server>();
    g_srv->set_payload_max_length(kMirrorMaxBody);

    // Single gate for every /mirror/* route, present and future. A route added
    // in a later phase is authorized automatically; nothing under /mirror/ can
    // be reached without the token (when one is configured).
    g_srv->set_pre_routing_handler(
        [](const httplib::Request& req, httplib::Response& res) {
            if (req.path.rfind("/mirror/", 0) == 0) {
                if (!boardghost::mirror::authorized(
                        g_token,
                        req.get_header_value("X-BoardGhost-Mirror"))) {
                    res.status = 401;
                    res.set_content("unauthorized", "text/plain");
                    return httplib::Server::HandlerResponse::Handled;
                }
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

    // Minimal liveness/feature-detection route. Phase 2 expands this with
    // {w,h,fps,audio,endpoints}; for now it just proves the gate + bind work.
    g_srv->Get("/mirror/info",
        [](const httplib::Request&, httplib::Response& res) {
            res.set_content("{\"name\":\"boardghost-mirror\",\"phase\":1}",
                            "application/json");
        });

    if (!g_srv->bind_to_port(bind_addr, port)) {
        std::fprintf(stderr,
            "[boardghost] mirror: bind to %s:%u failed — mirror unavailable\n",
            bind_addr, port);
        g_srv.reset();
        return;
    }
    g_running.store(true);
    g_thread = std::thread([]() {
        g_srv->listen_after_bind();
        g_running.store(false);
    });
    std::fprintf(stderr, "[boardghost] mirror: http://%s:%u/mirror/info%s\n",
                 bind_addr, port, decision.bind_lan ? " (LAN, token required)" : "");
}

extern "C" void boardghost_mirror_stop(void) {
    if (g_srv) g_srv->stop();
    if (g_thread.joinable()) g_thread.join();
    g_srv.reset();
    g_running.store(false);
}
