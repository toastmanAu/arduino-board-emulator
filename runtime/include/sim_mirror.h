#pragma once
#include <stddef.h>

// Mirror server — a SECOND HTTP server, distinct from the devtools server in
// sim_devtools.cpp. The devtools server binds 127.0.0.1 only because its
// /scanner/inject route mutates sketch I/O; this mirror server is the one that
// may be reached from the LAN (for the agentic control plane + the Android
// companion). Keeping it in its own module means /receipt and /scanner can
// never accidentally become LAN-reachable.
//
// Lifecycle: started from sim_runtime_init() so the observe/act surface exists
// regardless of sketch activity. Idempotent.
//
// Environment contract (Phase 1):
//   BOARDGHOST_MIRROR        off | lan   (default off → bind loopback)
//   BOARDGHOST_MIRROR_PORT   default 18082 (distinct from devtools 18081)
//   BOARDGHOST_MIRROR_TOKEN  required for a LAN bind; gates all /mirror/* routes.
//                            Use a random token of at least 128 bits (32 hex
//                            chars / a UUID). The server does not throttle 401s,
//                            so short or guessable tokens have no brute-force
//                            protection on a LAN bind.
//
// Invariant: bind 0.0.0.0 ONLY when BOARDGHOST_MIRROR=lan AND a token is set.
// Anything weaker stays on loopback and logs a warning (mirrors sim_ota.cpp).

#ifdef __cplusplus
extern "C" {
#endif

// Start the mirror HTTP server (idempotent — safe to call repeatedly).
void boardghost_mirror_start(void);

// Stop the mirror server and join its thread. Safe if never started.
void boardghost_mirror_stop(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <cstdint>
#include <string>
#include <vector>

namespace boardghost {
namespace mirror {

// Result of the bind-address decision: where to bind, and whether to warn the
// user that they asked for LAN reach but didn't provide a token (so we stayed
// on loopback).
struct BindDecision {
    bool bind_lan;       // true → bind 0.0.0.0; false → 127.0.0.1
    bool warn_no_token;  // true → log "LAN requested without a token" warning
};

// Decide where the mirror server should bind, from the raw BOARDGHOST_MIRROR
// value and whether a non-empty token was configured.
//   mode == nullptr / "" / "off"  → loopback, no warning
//   mode == "lan" && token_present → LAN bind
//   mode == "lan" && !token_present → loopback + warn
// (Any unrecognized mode is treated as "off".)
BindDecision decide_bind(const char* mode, bool token_present);

// Constant-time equality of two tokens. MUST NOT short-circuit on the first
// differing byte (that leaks token length/prefix via timing). Returns false on
// any length mismatch.
bool token_equal(const std::string& a, const std::string& b);

// Authorize a /mirror/* request. `configured` is BOARDGHOST_MIRROR_TOKEN
// (empty = no token configured → loopback-only dev convenience, allow).
// `header` is the X-BoardGhost-Mirror value. When a token is configured, the
// request is authorized iff `header` matches it in constant time.
//
// Header-only by design: a token in a query string leaks into access logs,
// shell history, the Referer header, and any LAN proxy. The browser <img>
// MJPEG case (which cannot set headers) is a Phase 2 concern and will get its
// own scoped, separately-reviewed mechanism — not a global ?token= channel.
bool authorized(const std::string& configured, const std::string& header);

// Cheap hash of a framebuffer, used by the /mirror/display MJPEG loop to skip
// re-encoding (and re-sending) frames that haven't changed. Equal pixels MUST
// produce equal hashes; a single changed pixel SHOULD change the hash.
uint64_t frame_hash(const std::vector<uint16_t>& fb);

// Build the /mirror/info JSON advertised to agents and the companion app:
// dimensions, the display stream cap, the fixed audio format, and the route
// list for feature detection.
std::string build_info_json(int w, int h, int fps);

}  // namespace mirror
}  // namespace boardghost
#endif
