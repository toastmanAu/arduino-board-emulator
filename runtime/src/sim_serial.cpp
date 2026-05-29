#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/select.h>

SerialClass Serial;
SerialClass Serial1;  // Stub for modem/secondary UART (no-op in sim)
SerialClass Serial2;  // Stub for tertiary UART (no-op in sim)

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

size_t SerialClass::println()                          { std::putchar('\n'); return 1; }
size_t SerialClass::println(const char* s)             { size_t n = print(s); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(const String& s)           { return println(s.c_str()); }
size_t SerialClass::println(int v)                     { size_t n = print(v); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(unsigned int v)            { size_t n = print(v); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(long v)                    { size_t n = print(v); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(unsigned long v)           { size_t n = print(v); std::putchar('\n'); return n + 1; }
size_t SerialClass::println(double v, int d)           { size_t n = print(v, d); std::putchar('\n'); return n + 1; }

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
