#include "sim_net.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" boardghost_net_mode_t sim_net_mode(void) {
    static boardghost_net_mode_t cached = (boardghost_net_mode_t)-1;
    if (cached != (boardghost_net_mode_t)-1) return cached;

    const char* v = std::getenv("BOARDGHOST_NET");
    if (!v || !*v) {
        cached = BOARDGHOST_NET_FAKE;
        return cached;
    }
    if (std::strcmp(v, "fake") == 0) cached = BOARDGHOST_NET_FAKE;
    else if (std::strcmp(v, "fail") == 0) cached = BOARDGHOST_NET_FAIL;
    else if (std::strcmp(v, "real") == 0) {
        if (sim_net_real_supported()) {
            cached = BOARDGHOST_NET_REAL;
        } else {
            std::fprintf(stderr,
                "[boardghost] BOARDGHOST_NET=real requested but this build "
                "has no libcurl; falling back to fake\n");
            cached = BOARDGHOST_NET_FAKE;
        }
    } else {
        std::fprintf(stderr,
            "[boardghost] BOARDGHOST_NET=%s unrecognised; using fake\n", v);
        cached = BOARDGHOST_NET_FAKE;
    }
    return cached;
}

extern "C" int sim_net_real_supported(void) {
#ifdef BOARDGHOST_WITH_CURL
    return 1;
#else
    return 0;
#endif
}
