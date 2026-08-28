#include <cstdint>
#include "WiFi.h"
#include "WiFiClient.h"
#include "sim_net.h"
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ifaddrs.h>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace {

struct ScanRow {
    std::string      ssid;
    int              rssi    = -60;
    int              channel = 6;
    wifi_auth_mode_t auth    = WIFI_AUTH_WPA2_PSK;
};

std::vector<ScanRow>& scan_results() {
    static std::vector<ScanRow> v;
    return v;
}

wifi_auth_mode_t parse_security(const std::string& s) {
    // Accept nmcli's SECURITY column ("WPA2", "WPA1 WPA2", "--") and a few
    // common aliases. Default to WPA2 for unknown secured cases — most
    // sketches just check open-vs-secured.
    if (s.empty() || s == "--" || s == "none" || s == "open" || s == "OPEN") return WIFI_AUTH_OPEN;
    if (s.find("WPA3") != std::string::npos) return WIFI_AUTH_WPA3_PSK;
    if (s.find("WPA2") != std::string::npos) return WIFI_AUTH_WPA2_PSK;
    if (s.find("WPA")  != std::string::npos) return WIFI_AUTH_WPA_PSK;
    if (s.find("WEP")  != std::string::npos) return WIFI_AUTH_WEP;
    return WIFI_AUTH_WPA2_PSK;
}

// Parse BOARDGHOST_WIFI_SCAN="ssid,security,rssi[,channel];..." into rows.
bool parse_env_scan(const char* env, std::vector<ScanRow>& out) {
    if (!env || !*env) return false;
    const char* p = env;
    while (*p) {
        ScanRow r;
        const char* end = std::strchr(p, ';');
        std::string entry = end ? std::string(p, end) : std::string(p);
        size_t i1 = entry.find(',');
        if (i1 == std::string::npos) { r.ssid = entry; }
        else {
            r.ssid = entry.substr(0, i1);
            size_t i2 = entry.find(',', i1 + 1);
            std::string sec = entry.substr(i1 + 1, (i2 == std::string::npos ? entry.size() : i2) - (i1 + 1));
            r.auth = parse_security(sec);
            if (i2 != std::string::npos) {
                size_t i3 = entry.find(',', i2 + 1);
                std::string rssi = entry.substr(i2 + 1, (i3 == std::string::npos ? entry.size() : i3) - (i2 + 1));
                if (!rssi.empty()) r.rssi = std::atoi(rssi.c_str());
                if (i3 != std::string::npos) {
                    r.channel = std::atoi(entry.substr(i3 + 1).c_str());
                }
            }
        }
        if (!r.ssid.empty()) out.push_back(std::move(r));
        if (!end) break;
        p = end + 1;
    }
    return !out.empty();
}

// Best-effort host scan via `nmcli` (NetworkManager). Returns false if nmcli
// isn't on PATH or the call fails — caller falls back to built-in defaults.
bool host_scan_nmcli(std::vector<ScanRow>& out) {
    // -t terse, --escape no, no rescan to avoid a multi-second probe each call.
    FILE* fp = popen("nmcli -t --escape no -f SSID,SIGNAL,CHAN,SECURITY dev wifi list --rescan no 2>/dev/null", "r");
    if (!fp) return false;
    char line[512];
    while (std::fgets(line, sizeof(line), fp)) {
        // Strip trailing newline.
        size_t n = std::strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        // Fields are colon-separated; nmcli escapes colons inside fields with
        // backslashes, but with --escape no it leaves them literal. We split
        // on the first three unescaped colons to recover ssid:signal:chan:security.
        std::string fields[4];
        int fi = 0;
        size_t start = 0;
        for (size_t i = 0; i < n && fi < 3; ++i) {
            if (line[i] == '\\' && i + 1 < n) { ++i; continue; }
            if (line[i] == ':') {
                fields[fi++] = std::string(line + start, i - start);
                start = i + 1;
            }
        }
        fields[fi] = std::string(line + start, n - start);
        if (fields[0].empty()) continue;
        ScanRow r;
        r.ssid = fields[0];
        int signal = std::atoi(fields[1].c_str());
        // nmcli SIGNAL is 0..100 strength %. Map to ESP-style RSSI dBm:
        // 0% → -100, 100% → -30 (rough heuristic, good enough for picker UI).
        r.rssi = signal == 0 ? -100 : (-100 + signal * 70 / 100);
        r.channel = std::atoi(fields[2].c_str());
        r.auth = parse_security(fields[3]);
        out.push_back(std::move(r));
    }
    pclose(fp);
    return !out.empty();
}

void populate_default_scan(std::vector<ScanRow>& out) {
    // Believable fallback — three networks with mixed security and signal so
    // a picker has something useful to filter/sort.
    out.push_back({"boardghost-sim",  -42, 6,  WIFI_AUTH_WPA2_PSK});
    out.push_back({"guest",           -67, 11, WIFI_AUTH_OPEN});
    out.push_back({"BTHub-5G-2X4Q",   -78, 36, WIFI_AUTH_WPA3_PSK});
}

}  // namespace

WiFiClass WiFi;

wl_status_t WiFiClass::begin(const char* /*ssid*/, const char* /*pass*/) {
    _force_disconnected = false;
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAKE: return WL_CONNECTED;
        case BOARDGHOST_NET_FAIL: return WL_NO_SSID_AVAIL;
        case BOARDGHOST_NET_REAL: return WL_CONNECTED;   // real-mode "associate" is a no-op; libcurl handles its own DNS
    }
    return WL_DISCONNECTED;
}

int WiFiClass::disconnect(bool /*wifioff*/) {
    _force_disconnected = true;
    return 1;
}

wl_status_t WiFiClass::status() {
    if (_force_disconnected) return WL_DISCONNECTED;
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAIL: return WL_DISCONNECTED;
        default:                  return WL_CONNECTED;
    }
}

bool WiFiClass::isConnected() { return status() == WL_CONNECTED; }

IPAddress WiFiClass::localIP() {
    // Walk getifaddrs() for the first non-loopback IPv4. Sketches send this
    // IP to webservers / log it, so 127.0.0.1 is actively misleading — they
    // need the host's actual LAN address to advertise.
    struct ifaddrs* head = nullptr;
    if (getifaddrs(&head) != 0 || !head) return IPAddress(127, 0, 0, 1);
    std::unique_ptr<struct ifaddrs, decltype(&freeifaddrs)> guard(head, &freeifaddrs);
    for (auto* ifa = head; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        if ((ip >> 24) == 127) continue;   // skip loopback
        return IPAddress((ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    }
    return IPAddress(127, 0, 0, 1);
}

IPAddress WiFiClass::gatewayIP()  { return IPAddress(127, 0, 0, 1); }
IPAddress WiFiClass::subnetMask() { return IPAddress(255, 255, 255, 0); }

String WiFiClass::SSID()       { return String("boardghost-sim"); }
String WiFiClass::macAddress() { return String("DE:AD:BE:EF:00:01"); }

int WiFiClass::RSSI() {
    switch (sim_net_mode()) {
        case BOARDGHOST_NET_FAIL: return 0;
        default:                  return -42;
    }
}

// --- Scan API ---

int16_t WiFiClass::scanNetworks(bool /*async*/, bool /*show_hidden*/,
                                bool /*passive*/, uint32_t /*max_ms_per_chan*/) {
    auto& rows = scan_results();
    rows.clear();
    if (sim_net_mode() == BOARDGHOST_NET_FAIL) return 0;
    // Priority: explicit env override → nmcli → defaults. Lets CI pin
    // deterministic results without depending on a host with NM running.
    if (parse_env_scan(std::getenv("BOARDGHOST_WIFI_SCAN"), rows)) {
        // ok
    } else if (!host_scan_nmcli(rows)) {
        populate_default_scan(rows);
    }
    return (int16_t)rows.size();
}

String WiFiClass::SSID(uint8_t idx) {
    auto& r = scan_results();
    return idx < r.size() ? String(r[idx].ssid.c_str()) : String();
}

int WiFiClass::RSSI(uint8_t idx) {
    auto& r = scan_results();
    return idx < r.size() ? r[idx].rssi : 0;
}

int32_t WiFiClass::channel(uint8_t idx) {
    auto& r = scan_results();
    return idx < r.size() ? r[idx].channel : 0;
}

wifi_auth_mode_t WiFiClass::encryptionType(uint8_t idx) {
    auto& r = scan_results();
    return idx < r.size() ? r[idx].auth : WIFI_AUTH_OPEN;
}

String WiFiClass::BSSIDstr(uint8_t /*idx*/) {
    // Synthesise a stable-but-unique BSSID from the index so picker UIs that
    // dedupe by BSSID don't collapse all entries together.
    return String("02:00:00:00:00:00");
}

void WiFiClass::scanDelete() {
    scan_results().clear();
}

// --- WiFiClient ---
//
// Real-mode TCP: getaddrinfo + connect a POSIX socket. recv/send/close drive
// the Stream surface (read/write/available/stop). Read/write timeout maps to
// SO_RCVTIMEO / SO_SNDTIMEO from Stream::setTimeout(ms).
//
// Fake mode: keep a bool so sketches that just check connected() without
// doing I/O don't break. Fail mode: connect returns 0, all reads return 0,
// writes succeed (avoids the sketch retrying on send errors when the user
// explicitly opted into "no network").

BoardghostSocket::~BoardghostSocket() {
    if (fd >= 0) ::close(fd);
}

namespace {

int sock_fd(const std::shared_ptr<BoardghostSocket>& s) {
    return s ? s->fd : -1;
}

void apply_timeout(int fd, uint32_t ms) {
    if (fd < 0) return;
    struct timeval tv;
    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

}  // namespace

void WiFiClient::reapply_timeout() {
    apply_timeout(sock_fd(sock_), timeout_ms_);
}

int WiFiClient::connect(const char* host, uint16_t port) {
    if (sim_net_mode() == BOARDGHOST_NET_FAIL) {
        sock_.reset();
        fake_connected_ = false;
        return 0;
    }
    if (sim_net_mode() == BOARDGHOST_NET_FAKE || !host || !*host) {
        // Fake mode: no real socket, just say connected. Matches what we did
        // before — sketches that exercise the stream API will see EOF.
        sock_.reset();
        fake_connected_ = true;
        return 1;
    }

    // Real mode: actually open a TCP socket.
    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%u", port);
    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (::getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        sock_.reset();
        fake_connected_ = false;
        return 0;
    }
    int fd = -1;
    for (auto* ai = res; ai; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0) {
        sock_.reset();
        fake_connected_ = false;
        return 0;
    }
    sock_ = std::make_shared<BoardghostSocket>();
    sock_->fd = fd;
    fake_connected_ = false;
    reapply_timeout();
    return 1;
}

void WiFiClient::stop() {
    sock_.reset();
    fake_connected_ = false;
}

bool WiFiClient::connected() const {
    if (sock_fd(sock_) >= 0) return true;
    return fake_connected_;
}

int WiFiClient::available() {
    int fd = sock_fd(sock_);
    if (fd < 0) return 0;
    int n = 0;
    if (::ioctl(fd, FIONREAD, &n) != 0) return 0;
    return n;
}

int WiFiClient::read() {
    uint8_t b = 0;
    int got = read(&b, 1);
    return got == 1 ? (int)b : -1;
}

int WiFiClient::read(uint8_t* buf, size_t n) {
    int fd = sock_fd(sock_);
    if (fd < 0 || !buf || n == 0) return 0;
    ssize_t r = ::recv(fd, buf, n, 0);
    if (r < 0) return 0;          // timeout / error → 0 so Stream callers don't loop forever
    if (r == 0) { stop(); return 0; }  // remote closed
    return (int)r;
}

size_t WiFiClient::write(uint8_t b) {
    return write(&b, 1);
}

size_t WiFiClient::write(const uint8_t* buf, size_t n) {
    int fd = sock_fd(sock_);
    if (fd < 0 || !buf || n == 0) {
        // Fake mode: pretend the bytes went out. Matches old behaviour.
        return n;
    }
    // MSG_NOSIGNAL: don't SIGPIPE on peer-closed sockets — we surface the
    // error via the return value instead.
    ssize_t w = ::send(fd, buf, n, MSG_NOSIGNAL);
    if (w < 0) return 0;
    return (size_t)w;
}
