#include <cstdint>
#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/select.h>

SerialClass    Serial;
HardwareSerial Serial1(1);  // Secondary UART — no peripheral connected in sim
HardwareSerial Serial2(2);  // Tertiary UART — no peripheral connected in sim

// Force line buffering on stdout so Serial.println output appears immediately
// when stdout is a pipe (e.g. when boardghost run captures output). Without
// this, libc defaults to fully-buffered mode on non-TTY stdout and prints
// only emerge on 4KB blocks or program exit — making hangs invisible to
// callers tailing the log.
namespace {
struct StdoutLineBuffer {
    StdoutLineBuffer() { std::setvbuf(stdout, nullptr, _IOLBF, 0); }
} s_stdout_line_buffer;
}  // namespace

size_t SerialClass::print(const char* s)        { return std::fputs(s, stdout) >= 0 ? std::strlen(s) : 0; }
size_t SerialClass::print(const String& s)      { return print(s.c_str()); }
size_t SerialClass::print(int v)                { return std::printf("%d", v);  }
size_t SerialClass::print(unsigned int v)       { return std::printf("%u", v);  }
size_t SerialClass::print(long v)               { return std::printf("%ld", v); }
size_t SerialClass::print(unsigned long v)      { return std::printf("%lu", v); }
size_t SerialClass::print(double v, int d)      { return std::printf("%.*f", d, v); }
size_t SerialClass::print(char c)               { std::putchar(c); return 1; }

namespace {
// Render `v` in the given base into `out` (buffer must be ≥ 33 bytes for
// a 32-bit value in base 2 plus terminator). Matches Arduino's print(v, BASE)
// semantics: bases 2/8/10/16 give expected glyphs, anything else falls back
// to decimal. Two's-complement negatives match Arduino (only base 10 prints
// a sign; other bases render the bit pattern, like the AVR core does).
size_t fmt_signed_radix(long v, int base, char* out, size_t cap) {
    if (base == 10) return (size_t)std::snprintf(out, cap, "%ld", v);
    unsigned long u = (unsigned long)v;
    switch (base) {
        case 16: return (size_t)std::snprintf(out, cap, "%lx", u);
        case  8: return (size_t)std::snprintf(out, cap, "%lo", u);
        case  2: {
            // No libc spec for binary; render manually.
            char tmp[65]; int i = 0;
            if (u == 0) { tmp[i++] = '0'; }
            while (u) { tmp[i++] = (char)('0' + (u & 1)); u >>= 1; }
            size_t n = i; if (n >= cap) n = cap - 1;
            for (size_t k = 0; k < n; ++k) out[k] = tmp[n - 1 - k];
            out[n] = '\0'; return n;
        }
        default: return (size_t)std::snprintf(out, cap, "%ld", v);
    }
}
size_t fmt_unsigned_radix(unsigned long v, int base, char* out, size_t cap) {
    switch (base) {
        case 10: return (size_t)std::snprintf(out, cap, "%lu", v);
        case 16: return (size_t)std::snprintf(out, cap, "%lx", v);
        case  8: return (size_t)std::snprintf(out, cap, "%lo", v);
        case  2: {
            char tmp[65]; int i = 0;
            if (v == 0) { tmp[i++] = '0'; }
            while (v) { tmp[i++] = (char)('0' + (v & 1)); v >>= 1; }
            size_t n = i; if (n >= cap) n = cap - 1;
            for (size_t k = 0; k < n; ++k) out[k] = tmp[n - 1 - k];
            out[n] = '\0'; return n;
        }
        default: return (size_t)std::snprintf(out, cap, "%lu", v);
    }
}
}  // namespace

size_t SerialClass::print(int v, int base)            { char b[40]; size_t n = fmt_signed_radix(v, base, b, sizeof(b));   std::fputs(b, stdout); return n; }
size_t SerialClass::print(unsigned int v, int base)   { char b[40]; size_t n = fmt_unsigned_radix(v, base, b, sizeof(b)); std::fputs(b, stdout); return n; }
size_t SerialClass::print(long v, int base)           { char b[40]; size_t n = fmt_signed_radix(v, base, b, sizeof(b));   std::fputs(b, stdout); return n; }
size_t SerialClass::print(unsigned long v, int base)  { char b[40]; size_t n = fmt_unsigned_radix(v, base, b, sizeof(b)); std::fputs(b, stdout); return n; }

size_t SerialClass::println()                          { std::putchar('\n'); return 1; }
size_t SerialClass::println(const char* s)             { size_t n = print(s); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(const String& s)           { return println(s.c_str()); }
size_t SerialClass::println(int v)                     { size_t n = print(v); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(unsigned int v)            { size_t n = print(v); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(long v)                    { size_t n = print(v); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(unsigned long v)           { size_t n = print(v); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(double v, int d)           { size_t n = print(v, d); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(int v, int base)           { size_t n = print(v, base); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(unsigned int v, int base)  { size_t n = print(v, base); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(long v, int base)          { size_t n = print(v, base); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(unsigned long v, int base) { size_t n = print(v, base); std::putchar('\n'); return n + 1; }

size_t SerialClass::printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = std::vprintf(fmt, ap);
    va_end(ap);
    return n < 0 ? 0 : static_cast<size_t>(n);
}

size_t SerialClass::write(uint8_t b)                          { std::putchar(b); return 1; }
size_t SerialClass::write(const uint8_t* buf, size_t n)       { return std::fwrite(buf, 1, n, stdout); }

int  SerialClass::available() {
    // Non-blocking stdin check; simple poll.
    fd_set s; FD_ZERO(&s); FD_SET(STDIN_FILENO, &s);
    timeval t{0, 0};
    return select(STDIN_FILENO + 1, &s, nullptr, nullptr, &t) > 0 ? 1 : 0;
}

int  SerialClass::read() {
    int c = std::getchar();
    return c == EOF ? -1 : c;
}

void SerialClass::flush() { std::fflush(stdout); }
