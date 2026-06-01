// Real UART backing for HardwareSerial(N) where N > 0.
//
// Read side  — opens a FIFO at <project>/.boardghost/uart-N-in.fifo (creates
// it if needed). Any byte written to that FIFO from outside is delivered to
// the sketch via the next available()/read() call. Use case: barcode/QR
// scanners on an external UART (GROW GM861S, etc.). Inject scans with
//   boardghost uart inject --port 2 "ABC123"
// or directly:
//   printf "ABC123\r\n" > <proj>/.boardghost/uart-2-in.fifo
//
// Write side — every byte the sketch writes to HardwareSerial(N) is also
// appended to <project>/.boardghost/uart-N-out.bin. Use case: thermal
// printers / GSM modems where you want to see the protocol bytes. tail -f
// the file to follow ESC/POS commands live.
//
// Port 0 stays on stdin/stdout (the global `Serial` instance), unchanged —
// only HardwareSerial(N>0) routes through this backend.

#include "Arduino.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

constexpr int kMaxPorts = 8;

struct Port {
    int               fifo_fd = -1;
    int               out_fd  = -1;
    std::deque<uint8_t> rx_buf;
    std::mutex          mtx;
    bool                opened = false;
};

std::array<Port, kMaxPorts> g_ports;

fs::path project_state_dir() {
    // Same anchor logic as sim_eeprom / sim_fs: when the sketch binary runs
    // from <project>/.boardghost/<board>/build, step up two levels.
    if (const char* env = std::getenv("BOARDGHOST_UART_DIR"); env && *env) {
        return fs::path(env);
    }
    auto cwd = fs::current_path();
    return cwd.parent_path().parent_path();
}

void ensure_open(int port_nr) {
    if (port_nr < 1 || port_nr >= kMaxPorts) return;  // port 0 = stdin path
    Port& p = g_ports[port_nr];
    std::lock_guard<std::mutex> lk(p.mtx);
    if (p.opened) return;
    p.opened = true;

    auto base = project_state_dir();
    std::error_code ec;
    fs::create_directories(base, ec);

    // Inbound FIFO: created if missing, opened O_RDONLY | O_NONBLOCK so
    // available()/read() never block. Multiple writers can append.
    std::string fifo = (base / ("uart-" + std::to_string(port_nr) + "-in.fifo")).string();
    if (!fs::exists(fifo)) {
        if (mkfifo(fifo.c_str(), 0666) != 0 && errno != EEXIST) {
            std::fprintf(stderr,
                "[boardghost] uart-%d: mkfifo %s failed: %s\n",
                port_nr, fifo.c_str(), std::strerror(errno));
        }
    }
    p.fifo_fd = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
    if (p.fifo_fd < 0) {
        std::fprintf(stderr,
            "[boardghost] uart-%d: open(%s) failed: %s\n",
            port_nr, fifo.c_str(), std::strerror(errno));
    } else {
        std::fprintf(stderr,
            "[boardghost] uart-%d: listening for injects at %s\n",
            port_nr, fifo.c_str());
    }

    // Outbound capture: append-only file.
    std::string out = (base / ("uart-" + std::to_string(port_nr) + "-out.bin")).string();
    p.out_fd = ::open(out.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (p.out_fd < 0) {
        std::fprintf(stderr,
            "[boardghost] uart-%d: open(%s, APPEND) failed: %s\n",
            port_nr, out.c_str(), std::strerror(errno));
    }
}

// Pump bytes from the FIFO into the rx_buf. Non-blocking — returns the new
// rx_buf size. Called from available()/read() so latency is "next sketch
// poll" rather than requiring a background thread.
size_t pump(int port_nr) {
    Port& p = g_ports[port_nr];
    if (p.fifo_fd < 0) return p.rx_buf.size();
    uint8_t tmp[1024];
    while (true) {
        ssize_t r = ::read(p.fifo_fd, tmp, sizeof(tmp));
        if (r <= 0) break;
        for (ssize_t i = 0; i < r; ++i) p.rx_buf.push_back(tmp[i]);
    }
    return p.rx_buf.size();
}

}  // namespace

// ---- HardwareSerial method bodies ----

int HardwareSerial::available() {
    int n = port();
    if (n < 1 || n >= kMaxPorts) return 0;
    ensure_open(n);
    Port& p = g_ports[n];
    std::lock_guard<std::mutex> lk(p.mtx);
    return (int)pump(n);
}

int HardwareSerial::read() {
    int n = port();
    if (n < 1 || n >= kMaxPorts) return -1;
    ensure_open(n);
    Port& p = g_ports[n];
    std::lock_guard<std::mutex> lk(p.mtx);
    pump(n);
    if (p.rx_buf.empty()) return -1;
    uint8_t b = p.rx_buf.front();
    p.rx_buf.pop_front();
    return (int)b;
}

int HardwareSerial::peek() {
    int n = port();
    if (n < 1 || n >= kMaxPorts) return -1;
    ensure_open(n);
    Port& p = g_ports[n];
    std::lock_guard<std::mutex> lk(p.mtx);
    pump(n);
    return p.rx_buf.empty() ? -1 : (int)p.rx_buf.front();
}

int HardwareSerial::read(uint8_t* buf, size_t cap) {
    int n = port();
    if (n < 1 || n >= kMaxPorts || !buf || cap == 0) return 0;
    ensure_open(n);
    Port& p = g_ports[n];
    std::lock_guard<std::mutex> lk(p.mtx);
    pump(n);
    size_t take = std::min(cap, p.rx_buf.size());
    for (size_t i = 0; i < take; ++i) {
        buf[i] = p.rx_buf.front();
        p.rx_buf.pop_front();
    }
    return (int)take;
}

size_t HardwareSerial::write(uint8_t b) {
    return write(&b, 1);
}

size_t HardwareSerial::write(const uint8_t* buf, size_t len) {
    int n = port();
    if (n < 1 || n >= kMaxPorts) return SerialClass::write(buf, len);
    ensure_open(n);
    Port& p = g_ports[n];
    if (p.out_fd >= 0 && buf && len > 0) {
        ssize_t w = ::write(p.out_fd, buf, len);
        if (w > 0) return (size_t)w;
    }
    return len;  // pretend success so sketches don't loop on send errors
}
