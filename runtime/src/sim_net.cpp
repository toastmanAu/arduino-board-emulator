#include "sim_net.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" boardghost_net_mode_t sim_net_mode(void) {
    // Re-read env every call. The cost is negligible (called only from a
    // handful of shim methods, never on a hot path) and it lets integration
    // tests flip BOARDGHOST_NET between cases. The "unrecognised value"
    // warning is rate-limited so a wrong value doesn't spam stderr.
    const char* v = std::getenv("BOARDGHOST_NET");
    if (!v || !*v) return BOARDGHOST_NET_FAKE;
    if (std::strcmp(v, "fake") == 0) return BOARDGHOST_NET_FAKE;
    if (std::strcmp(v, "fail") == 0) return BOARDGHOST_NET_FAIL;
    if (std::strcmp(v, "real") == 0) {
        if (sim_net_real_supported()) return BOARDGHOST_NET_REAL;
        static bool warned_no_curl = false;
        if (!warned_no_curl) {
            std::fprintf(stderr,
                "[boardghost] BOARDGHOST_NET=real requested but this build "
                "has no libcurl; falling back to fake\n");
            warned_no_curl = true;
        }
        return BOARDGHOST_NET_FAKE;
    }
    static bool warned_unknown = false;
    if (!warned_unknown) {
        std::fprintf(stderr,
            "[boardghost] BOARDGHOST_NET=%s unrecognised; using fake\n", v);
        warned_unknown = true;
    }
    return BOARDGHOST_NET_FAKE;
}

extern "C" int sim_net_real_supported(void) {
#ifdef BOARDGHOST_WITH_CURL
    return 1;
#else
    return 0;
#endif
}
