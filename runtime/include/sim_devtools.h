#pragma once
#include <stddef.h>
#include <stdint.h>

// Devtools — a small HTTP server that exposes peripheral UIs to the host
// browser. The sketch's UART1 (printer) bytes accumulate in a shared buffer
// that the /receipt page renders as styled HTML; the /scanner page lets the
// user type or camera-scan a QR code and POSTs the result into the UART2
// queue file (same effect as `boardghost uart inject --queue`).
//
// Hooked from the UART backend so adding a new "tool" page later — say a
// GPS NMEA stream viewer — is a route registration plus a buffer hook.

#ifdef __cplusplus
extern "C" {
#endif

// Start the devtools HTTP server (idempotent). Called from runtime init so the
// receipt/scanner UIs are up regardless of printer activity. The lazy
// UART1-triggered start remains as a fallback. Always binds 127.0.0.1 — the
// LAN-reachable surface lives in sim_mirror.cpp.
void boardghost_devtools_start(void);

// Stop the devtools server and JOIN its thread. Must be called before the
// process returns from main(): g_thread is a namespace-scope std::thread, and
// ~thread() on a still-joinable thread calls std::terminate(). That aborts at
// static-destruction time — after the sketch has printed everything and looks
// like it succeeded — so it surfaces as "terminate called without an active
// exception" and a non-zero exit from an otherwise clean run.
// Mirrors boardghost_mirror_stop(). Idempotent.
void boardghost_devtools_stop(void);

// Append bytes to the in-memory printer ring buffer that /receipt renders.
// Called from sim_uart.cpp whenever the sketch writes to UART1.
void boardghost_devtools_record_uart_out(int port_nr, const uint8_t* buf, size_t n);

#ifdef __cplusplus
}
#endif
