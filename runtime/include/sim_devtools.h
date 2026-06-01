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

// Append bytes to the in-memory printer ring buffer that /receipt renders.
// Called from sim_uart.cpp whenever the sketch writes to UART1.
void boardghost_devtools_record_uart_out(int port_nr, const uint8_t* buf, size_t n);

#ifdef __cplusplus
}
#endif
