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

#include <cstdint>
#include "Arduino.h"
#include "sim_devtools.h"

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

// Per-port "virtual peripheral" for protocols where the sketch expects a
// specific ACK after writing a command frame — without it, the sketch
// retries / times out and never enters its run-loop. Currently only the
// GROW GM861S barcode scanner is implemented (ckb_pos's UART2 device); the
// hook lets other peripherals be added without rewriting Port plumbing.
//
// Selected via BOARDGHOST_UART_<N>_PERIPHERAL=gm861s (default off → silent
// drops, original behaviour).
enum class Peripheral { None, Gm861s };

struct Port {
    int                 fifo_fd = -1;
    int                 out_fd  = -1;
    std::deque<uint8_t> rx_buf;
    std::mutex          mtx;
    bool                opened = false;
    Peripheral          peripheral = Peripheral::None;
    std::deque<uint8_t> write_history;  // sliding window of recent outbound bytes
    // Bytes staged for delivery AFTER the sketch drains the current rx_buf.
    // Used by the GM861S emulator: the trigger ACK lands in rx_buf so the
    // sketch's triggerScanner response-comparison loop sees a clean 7-byte
    // match, then once rx_buf is empty (sketch finished reading ACK) we
    // release pre-queued scan bytes into rx_buf so the main barcode loop
    // picks them up in a SEPARATE read-loop iteration.
    //
    // Two-step release: when pump() first sees rx_buf empty + delayed_rx
    // non-empty, it sets `delayed_rx_armed` and returns size 0. That makes
    // the sketch's `while (available() > 0)` loop exit cleanly. The NEXT
    // pump() call (i.e. the next sketch poll) sees the armed flag and
    // releases delayed_rx into rx_buf as a fresh read. Without the cooldown
    // the release would fire mid-loop and the sketch would consume ACK +
    // scan as one stream — exactly the "Byte Mismatch" bug this comment
    // was born from.
    std::deque<uint8_t> delayed_rx;
    bool                delayed_rx_armed = false;
};

std::array<Port, kMaxPorts> g_ports;

// The GM861S replies to both 0x7E…02 01 (start trigger) and 0x7E…02 00
// (stop trigger) with the same 7-byte success ACK. Other commands the
// sketch sends (setBaud, setCommandTriggerMode) get no reply in this
// model — the real ckb_pos sketch doesn't read for one either.
constexpr uint8_t kGm861sTriggerAck[] = {0x02, 0x00, 0x00, 0x01, 0x00, 0x33, 0x31};

// Match result: number of bytes consumed from the history and whether
// the matched frame was a start-trigger (which should fire any staged
// queue scan). Stop-trigger is acknowledged but doesn't fire the queue.
struct Gm861sMatch {
    size_t consumed         = 0;
    bool   fire_queue       = false;
};

Gm861sMatch maybe_match_gm861s(std::deque<uint8_t>& hist, std::deque<uint8_t>& rx_out) {
    // GM861S commands are 9 bytes: 7E 00 LL CC ... AB CD.
    if (hist.size() < 9) return {};
    for (size_t start = 0; start + 9 <= hist.size(); ++start) {
        if (hist[start] != 0x7E) continue;
        if (hist[start + 7] != 0xAB || hist[start + 8] != 0xCD) continue;
        // Byte 3 = command type (0x01=trigger), 5 = subcmd (0x02=software),
        // 6 = action (0x01=start, 0x00=stop). Other frame shapes (setBaud
        // etc.) pass through silently — the real sketch doesn't read an
        // ACK for those either.
        bool is_trigger_frame =
            hist[start + 3] == 0x01 && hist[start + 5] == 0x02 &&
            (hist[start + 6] == 0x00 || hist[start + 6] == 0x01);
        if (is_trigger_frame) {
            for (auto b : kGm861sTriggerAck) rx_out.push_back(b);
        }
        bool is_start_trigger = is_trigger_frame && hist[start + 6] == 0x01;
        return {start + 9, is_start_trigger};
    }
    // No header found yet — if the history is overflowing without a match
    // (32+ bytes and still no 0x7E in a valid position), drop a byte so
    // we don't accumulate forever.
    if (hist.size() > 32) return {1, false};
    return {};
}

Peripheral parse_peripheral_env(int port_nr) {
    char key[40];
    std::snprintf(key, sizeof(key), "BOARDGHOST_UART_%d_PERIPHERAL", port_nr);
    const char* v = std::getenv(key);
    if (!v || !*v) return Peripheral::None;
    if (std::strcmp(v, "gm861s") == 0) return Peripheral::Gm861s;
    std::fprintf(stderr,
        "[boardghost] uart-%d: unknown peripheral '%s'; treating as none\n",
        port_nr, v);
    return Peripheral::None;
}

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
    p.peripheral = parse_peripheral_env(port_nr);
    if (p.peripheral != Peripheral::None) {
        std::fprintf(stderr,
            "[boardghost] uart-%d: virtual peripheral=gm861s "
            "(auto-acks trigger commands)\n", port_nr);
    }

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
//
// Two-phase delivery: when delayed_rx has staged bytes (post-trigger scan
// data), they're only released into rx_buf once rx_buf is empty. This keeps
// the trigger ACK and the scan payload in separate reads on the sketch
// side — see the comment on Port::delayed_rx for why that matters.
size_t pump(int port_nr) {
    Port& p = g_ports[port_nr];
    if (p.fifo_fd >= 0) {
        uint8_t tmp[1024];
        while (true) {
            ssize_t r = ::read(p.fifo_fd, tmp, sizeof(tmp));
            if (r <= 0) break;
            for (ssize_t i = 0; i < r; ++i) p.rx_buf.push_back(tmp[i]);
        }
    }
    if (p.rx_buf.empty() && !p.delayed_rx.empty()) {
        if (!p.delayed_rx_armed) {
            // First time we see rx_buf empty after staging — arm the
            // release but don't fire yet. Returning 0 here makes the
            // sketch's `while (available() > 0)` loop exit, so when we DO
            // release on the next call the bytes appear as a fresh read.
            p.delayed_rx_armed = true;
            return 0;
        }
        // Second call — sketch has finished its previous read loop and
        // is polling again. Now release the whole staged scan in one
        // batch so SCAN_RESULT[] captures contiguous bytes.
        for (auto b : p.delayed_rx) p.rx_buf.push_back(b);
        p.delayed_rx.clear();
        p.delayed_rx_armed = false;
    }
    return p.rx_buf.size();
}

// Look for a staged scan in <project>/.boardghost/uart-N-queue.bin and, if
// non-empty, atomically move its contents into delayed_rx. The file is
// truncated after — each click of the scanner-trigger fires exactly one
// staged scan. Called whenever the GM861S emulator ACKs a trigger.
void drain_queue_file(int port_nr, Port& p) {
    auto base = project_state_dir();
    std::string path = (base / ("uart-" + std::to_string(port_nr) + "-queue.bin")).string();
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) return;  // no queued scan, no problem
    uint8_t tmp[4096];
    while (true) {
        ssize_t r = ::read(fd, tmp, sizeof(tmp));
        if (r <= 0) break;
        for (ssize_t i = 0; i < r; ++i) p.delayed_rx.push_back(tmp[i]);
    }
    // Truncate so the next trigger doesn't replay the same scan.
    ::ftruncate(fd, 0);
    ::close(fd);
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
    // Capture the bytes to disk first so the user can tail -f the
    // outbound stream regardless of whether a peripheral emulator
    // consumes them.
    if (p.out_fd >= 0 && buf && len > 0) {
        ::write(p.out_fd, buf, len);
    }
    // Mirror to the devtools printer ring so the /receipt page can render
    // a live view. Filters to port 1 inside the hook (only UART1 surfaces
    // as a printer in v1).
    if (buf && len > 0) {
        boardghost_devtools_record_uart_out(n, buf, len);
    }
    // Virtual peripheral hook: feed outbound bytes into the per-port
    // history buffer and emit any synthesised response into the rx queue.
    if (p.peripheral != Peripheral::None && buf && len > 0) {
        std::lock_guard<std::mutex> lk(p.mtx);
        for (size_t i = 0; i < len; ++i) p.write_history.push_back(buf[i]);
        while (true) {
            Gm861sMatch m;
            if (p.peripheral == Peripheral::Gm861s) {
                m = maybe_match_gm861s(p.write_history, p.rx_buf);
            }
            if (m.consumed == 0) break;
            for (size_t i = 0; i < m.consumed; ++i) p.write_history.pop_front();
            // Start-trigger fires any staged scan from the queue file.
            // Scan bytes go into delayed_rx so they're delivered AFTER the
            // sketch consumes the trigger ACK that's already in rx_buf.
            if (m.fire_queue) drain_queue_file(n, p);
        }
    }
    return len;  // pretend success so sketches don't loop on send errors
}
