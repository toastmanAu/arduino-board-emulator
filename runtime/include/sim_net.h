#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARDGHOST_NET_FAKE = 0,
    BOARDGHOST_NET_FAIL = 1,
    BOARDGHOST_NET_REAL = 2,
} boardghost_net_mode_t;

// Returns the current network mode. Reads BOARDGHOST_NET env var on first
// call and caches. Defaults to FAKE. Unknown values fall back to FAKE
// with a stderr warning.
boardghost_net_mode_t sim_net_mode(void);

// Returns 1 iff this build supports BOARDGHOST_NET=real (i.e. libcurl
// was found at configure time). Always 0 if BOARDGHOST_WITH_CURL=OFF.
int sim_net_real_supported(void);

#ifdef __cplusplus
}
#endif
