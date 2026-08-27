#pragma once
// AVR/ESP <pgmspace.h> shim.
//
// Sketches that store bitmaps in flash (e.g. blackbox-pos bblogo.h) include
// <pgmspace.h> DIRECTLY rather than relying on Arduino.h pulling it in, so the
// shim layer needs a file by that name or the sketch fails with
// "fatal error: pgmspace.h: No such file or directory".
//
// On a host there is no separate program-memory address space: PROGMEM is a
// no-op and every pgm_read_* is a plain dereference. These are the same
// definitions Arduino.h carries, and every one is #ifndef-guarded, so
// including both headers in either order is safe.
#include <stdint.h>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char*
#endif
#ifndef PSTR
#define PSTR(s) (s)
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr)  (*reinterpret_cast<const uint8_t*>(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr)  (*reinterpret_cast<const uint16_t*>(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*reinterpret_cast<const uint32_t*>(addr))
#endif
#ifndef pgm_read_float
#define pgm_read_float(addr) (*reinterpret_cast<const float*>(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr)   (*reinterpret_cast<void* const*>(addr))
#endif
