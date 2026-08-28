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

#include <cstdint>
#include "sim_mirror.h"
#include "sim_capture.h"
#include "sim_audio.h"
#include "sim_touch_inject.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// mDNS advertisement (sim_mirror_mdns.cpp) — only meaningful on a LAN bind.
extern "C" void boardghost_mirror_mdns_advertise(uint16_t port);
extern "C" void boardghost_mirror_mdns_stop(void);

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

int mirror_fps() {
    if (const char* env = std::getenv("BOARDGHOST_MIRROR_FPS"); env && *env) {
        int f = std::atoi(env);
        if (f >= 1 && f <= 60) return f;
    }
    return 15;
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

uint64_t frame_hash(const std::vector<uint16_t>& fb) {
    // FNV-1a over the pixels, seeded with the length so two buffers that differ
    // only in size never collide (a resized display still reads as "changed").
    uint64_t h = 1469598103934665603ULL ^ static_cast<uint64_t>(fb.size());
    for (uint16_t px : fb) {
        h = (h ^ px) * 1099511628211ULL;
    }
    return h;
}

std::string build_info_json(int w, int h, int fps) {
    // Hand-built (no JSON dep in the runtime). Fixed audio format matches the
    // sim_audio mix that Phase 3 will tap: 44.1 kHz mono S16.
    return "{\"w\":" + std::to_string(w) +
           ",\"h\":" + std::to_string(h) +
           ",\"fps\":" + std::to_string(fps) +
           ",\"audio\":{\"rate\":44100,\"channels\":1,\"bits\":16}" +
           ",\"endpoints\":[\"/mirror/info\",\"/mirror/screen.png\","
           "\"/mirror/display\",\"/mirror/audio\",\"/mirror/touch\"]}";
}

namespace {
// Pull an integer value for a JSON key `"<key>"` out of `b`. Tolerant of
// surrounding whitespace; handles a leading sign. Returns false if the key or
// a numeric value isn't found. (A hand parser, not a full JSON reader — the
// runtime has no JSON dep and the body shape is fixed.)
bool json_int(const std::string& b, const char* key, int& out) {
    std::string pat = std::string("\"") + key + "\"";
    size_t k = b.find(pat);
    if (k == std::string::npos) return false;
    size_t c = b.find(':', k + pat.size());
    if (c == std::string::npos) return false;
    size_t i = c + 1;
    while (i < b.size() && std::isspace((unsigned char)b[i])) ++i;
    bool neg = false;
    if (i < b.size() && (b[i] == '-' || b[i] == '+')) { neg = (b[i] == '-'); ++i; }
    if (i >= b.size() || !std::isdigit((unsigned char)b[i])) return false;
    // Clamp accumulation: a 64 KB body could otherwise feed enough digits to
    // overflow `long` (UB). No touch coordinate exceeds the panel dimensions,
    // so cap the magnitude at INT16_MAX — also keeps the int16_t narrowing in
    // Panel_sdl_bg exact rather than wrapping.
    long v = 0;
    while (i < b.size() && std::isdigit((unsigned char)b[i])) {
        if (v <= 32767L) v = v * 10 + (b[i] - '0');
        ++i;
    }
    if (v > 32767L) v = 32767L;
    out = (int)(neg ? -v : v);
    return true;
}
}  // namespace

bool parse_touch_body(const std::string& body, int& x, int& y, bool& screen_space) {
    if (!json_int(body, "x", x)) return false;
    if (!json_int(body, "y", y)) return false;
    // Default to screen space; only the explicit quoted "raw" value opts out.
    // Matching the quoted token (not a bare "raw" substring) avoids tripping on
    // unrelated fields like {"label":"drawback"} or a "rawmode" key.
    screen_space = body.find("\"raw\"") == std::string::npos;
    return true;
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

    // GET /mirror/info — feature detection: dims (0 until a display registers),
    // stream fps cap, fixed audio format, and the route list.
    g_srv->Get("/mirror/info",
        [](const httplib::Request&, httplib::Response& res) {
            std::vector<uint16_t> fb; int w = 0, h = 0;
            boardghost::capture_active_rgb565(fb, w, h);  // false → w/h stay 0
            res.set_content(boardghost::mirror::build_info_json(w, h, mirror_fps()),
                            "application/json");
        });

    // GET /mirror/screen.png — on-demand single frame, the agent-loop observe
    // primitive (no SIGUSR1, no delay race). ?stable=N captures until N
    // consecutive identical frames (a settled screen) or a ~2s timeout.
    g_srv->Get("/mirror/screen.png",
        [](const httplib::Request& req, httplib::Response& res) {
            std::vector<uint16_t> fb; int w = 0, h = 0;
            if (!boardghost::capture_active_rgb565(fb, w, h)) {
                res.status = 503;
                res.set_content("no active display", "text/plain");
                return;
            }
            if (req.has_param("stable")) {
                int need = std::atoi(req.get_param_value("stable").c_str());
                if (need < 2) need = 2;
                uint64_t last = boardghost::mirror::frame_hash(fb);
                int streak = 1;
                auto deadline = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(2000);
                while (streak < need &&
                       std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(33));
                    std::vector<uint16_t> next; int nw = 0, nh = 0;
                    if (!boardghost::capture_active_rgb565(next, nw, nh)) break;
                    uint64_t hh = boardghost::mirror::frame_hash(next);
                    if (hh == last) { ++streak; }
                    else { last = hh; streak = 1; fb.swap(next); w = nw; h = nh; }
                }
            }
            std::vector<uint8_t> png;
            if (!boardghost::encode_png(fb.data(), w, h, png)) {
                res.status = 500;
                res.set_content("encode failed", "text/plain");
                return;
            }
            res.set_content(reinterpret_cast<const char*>(png.data()), png.size(),
                            "image/png");
        });

    // GET /mirror/display — live MJPEG (multipart/x-mixed-replace). One JPEG
    // part per changed frame, paced to the fps cap; unchanged frames are
    // skipped (no encode, no send) so an idle board costs ~0 CPU/bandwidth.
    g_srv->Get("/mirror/display",
        [](const httplib::Request&, httplib::Response& res) {
            const int fps = mirror_fps();
            const auto period = std::chrono::milliseconds(1000 / fps);
            auto last = std::make_shared<uint64_t>(0);
            auto have = std::make_shared<bool>(false);
            res.set_chunked_content_provider(
                "multipart/x-mixed-replace; boundary=frame",
                [fps, period, last, have](size_t, httplib::DataSink& sink) -> bool {
                    std::this_thread::sleep_for(period);  // pace ≤ fps
                    std::vector<uint16_t> fb; int w = 0, h = 0;
                    if (!boardghost::capture_active_rgb565(fb, w, h)) {
                        return true;  // no display yet — keep the connection alive
                    }
                    uint64_t hh = boardghost::mirror::frame_hash(fb);
                    if (*have && hh == *last) return true;  // unchanged — skip
                    std::vector<uint8_t> jpg;
                    if (!boardghost::encode_jpeg(fb.data(), w, h, 70, jpg)) return true;
                    *last = hh; *have = true;
                    std::string head = "--frame\r\nContent-Type: image/jpeg\r\n"
                                       "Content-Length: " + std::to_string(jpg.size()) +
                                       "\r\n\r\n";
                    if (!sink.write(head.data(), head.size())) return false;
                    if (!sink.write(reinterpret_cast<const char*>(jpg.data()),
                                    jpg.size())) return false;
                    if (!sink.write("\r\n", 2)) return false;  // client gone
                    return true;
                });
        });

    // GET /mirror/audio — chunked raw PCM (S16LE mono, 44.1 kHz). Forces the
    // audio device open so a steady cadence flows even before the first tone;
    // each ~20ms tick drains the tap ring and pads with silence so the client
    // (Android AudioTrack) never underruns. BOARDGHOST_SOUND=off → all silence.
    g_srv->Get("/mirror/audio",
        [](const httplib::Request&, httplib::Response& res) {
            boardghost_audio_ensure_started();
            res.set_chunked_content_provider(
                "application/octet-stream",
                [](size_t, httplib::DataSink& sink) -> bool {
                    constexpr size_t kChunk = 882;  // ~20ms @ 44.1kHz mono
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    int16_t buf[kChunk];
                    size_t got = boardghost_audio_drain(buf, kChunk);
                    for (size_t i = got; i < kChunk; ++i) buf[i] = 0;  // pad
                    return sink.write(reinterpret_cast<const char*>(buf),
                                      kChunk * sizeof(int16_t));  // false → gone
                });
        });

    // POST /mirror/touch — the act half of the agentic loop (and the Android
    // companion's touch-back). Body: {"x":N,"y":N,"space":"screen"|"raw"}.
    // Token-gated by the /mirror/* pre-routing handler; body capped at 64 KB by
    // set_payload_max_length above.
    g_srv->Post("/mirror/touch",
        [](const httplib::Request& req, httplib::Response& res) {
            int x = 0, y = 0;
            bool screen = true;
            if (!boardghost::mirror::parse_touch_body(req.body, x, y, screen)) {
                res.status = 400;
                res.set_content(
                    "{\"error\":\"expected {\\\"x\\\":N,\\\"y\\\":N,"
                    "\\\"space\\\":\\\"screen|raw\\\"}\"}",
                    "application/json");
                return;
            }
            boardghost_inject_touch(x, y, screen ? 1 : 0);
            res.set_content("{\"ok\":true}", "application/json");
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

    // Advertise for discovery only when actually reachable from the LAN.
    if (decision.bind_lan) boardghost_mirror_mdns_advertise(port);
}

extern "C" void boardghost_mirror_stop(void) {
    boardghost_mirror_mdns_stop();
    if (g_srv) g_srv->stop();
    if (g_thread.joinable()) g_thread.join();
    g_srv.reset();
    g_running.store(false);
}
