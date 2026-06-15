// espota-protocol OTA receiver. See ArduinoOTA.h for the env contract.
#include "ArduinoOTA.h"
#include "Update.h"
#include "ESPmDNS.h"
#include "WiFi.h"
#include "sim_net.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/random.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#if defined(BOARDGHOST_WITH_OPENSSL)
#include <openssl/evp.h>
#endif

namespace boardghost_internal {

#if defined(BOARDGHOST_WITH_OPENSSL)
static std::string ota_md5(const std::string& in) {
    unsigned char d[EVP_MAX_MD_SIZE]; unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(ctx, in.data(), in.size());
    EVP_DigestFinal_ex(ctx, d, &len);
    EVP_MD_CTX_free(ctx);
    static const char* h = "0123456789abcdef";
    std::string o; o.reserve(len * 2);
    for (unsigned i = 0; i < len; i++) { o += h[d[i] >> 4]; o += h[d[i] & 0xf]; }
    return o;
}
#else
static std::string ota_md5(const std::string&) { return std::string(); }
#endif

static uint16_t resolve_port(uint16_t def) {
    if (const char* e = std::getenv("BOARDGHOST_OTA_PORT")) {
        int p = std::atoi(e);
        if (p > 0 && p < 65536) return (uint16_t)p;
    }
    return def;
}

class OtaImpl {
public:
    ~OtaImpl() { stop(); }

    void set_port(uint16_t p)          { port_ = p; }
    void set_hostname(const char* h)   { hostname_ = h ? h : ""; }
    std::string hostname() const       { return hostname_; }
    void set_password(const char* p)   { pass_md5_ = (p && *p) ? ota_md5(p) : ""; auth_ = !pass_md5_.empty(); }
    void set_password_hash(const char* h) { pass_md5_ = h ? h : ""; auth_ = !pass_md5_.empty(); }
    void set_mdns(bool e)              { mdns_ = e; }

    std::function<void()> on_start_, on_end_;
    std::function<void(unsigned, unsigned)> on_progress_;
    std::function<void(ota_error_t)> on_error_;
    int command_ = 0;

    void begin() {
        if (sim_net_mode() != BOARDGHOST_NET_REAL) {
            std::fprintf(stderr,
                "[boardghost] ArduinoOTA.begin(): inert (set BOARDGHOST_NET=real "
                "to accept OTA pushes on UDP:%u).\n", resolve_port(port_));
            return;
        }
        stop();
        port_ = resolve_port(port_);
        const char* bind_env = std::getenv("BOARDGHOST_OTA_BIND");
        std::string bind_addr = bind_env ? bind_env : "127.0.0.1";

        udp_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_fd_ < 0) return;
        int one = 1; setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port_);
        a.sin_addr.s_addr = inet_addr(bind_addr.c_str());
        if (::bind(udp_fd_, (sockaddr*)&a, sizeof(a)) != 0) {
            std::fprintf(stderr, "[boardghost] ArduinoOTA: bind %s:%u failed.\n",
                         bind_addr.c_str(), port_);
            ::close(udp_fd_); udp_fd_ = -1; return;
        }
        int fl = fcntl(udp_fd_, F_GETFL, 0); fcntl(udp_fd_, F_SETFL, fl | O_NONBLOCK);

        // Advertise only when reachable from the LAN (non-loopback bind).
        if (mdns_ && bind_addr != "127.0.0.1") {
            MDNS.begin(hostname_.empty() ? "esp32" : hostname_.c_str());
            MDNS.enableArduino(port_, auth_);
        }
        std::fprintf(stderr, "[boardghost] ArduinoOTA: listening on %s:%u%s\n",
                     bind_addr.c_str(), port_, auth_ ? " (auth)" : "");
    }

    void stop() {
        if (udp_fd_ >= 0) { ::close(udp_fd_); udp_fd_ = -1; }
    }

    void handle() {
        if (udp_fd_ < 0) return;
        char buf[512]; sockaddr_in from{}; socklen_t fl = sizeof(from);
        ssize_t n = ::recvfrom(udp_fd_, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 0) return;   // no invite pending
        buf[n] = 0;

        // Invite: "<cmd> <host_tcp_port> <size> <md5>"
        std::istringstream iss(buf);
        int cmd = 0; uint16_t host_port = 0; size_t size = 0; std::string md5;
        if (!(iss >> cmd >> host_port >> size >> md5)) return;
        command_ = cmd;

        // Save invite sender's address before auth block may overwrite `from`.
        sockaddr_in invite_from = from;

        if (auth_) {
            unsigned char rnd[16];
            ssize_t gr = getrandom(rnd, sizeof(rnd), 0);
            std::string nonce = ota_md5(std::string(reinterpret_cast<char*>(rnd), gr > 0 ? (size_t)gr : 0) + std::to_string(getpid()));
            std::string authreq = "AUTH " + nonce + "\n";
            ::sendto(udp_fd_, authreq.data(), authreq.size(), 0, (sockaddr*)&from, fl);
            // Await the host's response (blocking with a short timeout).
            timeval tv{3, 0}; setsockopt(udp_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            int curfl = fcntl(udp_fd_, F_GETFL, 0); fcntl(udp_fd_, F_SETFL, curfl & ~O_NONBLOCK);
            char rb[256]; ssize_t rn = ::recvfrom(udp_fd_, rb, sizeof(rb) - 1, 0, (sockaddr*)&from, &fl);
            fcntl(udp_fd_, F_SETFL, curfl);   // restore non-blocking
            bool ok = false;
            if (rn > 0) {
                rb[rn] = 0; std::istringstream as(rb);
                std::string cnonce, response; as >> cnonce >> response;
                ok = (response == ota_md5(pass_md5_ + ":" + nonce + ":" + cnonce));
            }
            if (!ok) {
                const char* deny = "Authentication Failed\n";
                ::sendto(udp_fd_, deny, std::strlen(deny), 0, (sockaddr*)&from, fl);
                fire_error(OTA_AUTH_ERROR);
                return;
            }
        }
        const char* okmsg = "OK\n";
        ::sendto(udp_fd_, okmsg, std::strlen(okmsg), 0, (sockaddr*)&from, fl);

        // Connect back to the host and pull the firmware.
        int tcp = ::socket(AF_INET, SOCK_STREAM, 0);
        if (tcp < 0) { fire_error(OTA_CONNECT_ERROR); return; }
        sockaddr_in h{}; h.sin_family = AF_INET; h.sin_port = htons(host_port);
        h.sin_addr = invite_from.sin_addr;   // FIX 1: use saved invite sender IP

        // FIX 4: non-blocking connect with 5-second timeout.
        int cfl = fcntl(tcp, F_GETFL, 0);
        fcntl(tcp, F_SETFL, cfl | O_NONBLOCK);
        int cr = ::connect(tcp, (sockaddr*)&h, sizeof(h));
        if (cr != 0) {
            if (errno != EINPROGRESS) {
                ::close(tcp); fire_error(OTA_CONNECT_ERROR); return;
            }
            fd_set wfds; FD_ZERO(&wfds); FD_SET(tcp, &wfds);
            timeval tv_conn{5, 0};
            int sel = ::select(tcp + 1, nullptr, &wfds, nullptr, &tv_conn);
            if (sel <= 0) {
                ::close(tcp); fire_error(OTA_CONNECT_ERROR); return;
            }
            int so_err = 0; socklen_t so_len = sizeof(so_err);
            getsockopt(tcp, SOL_SOCKET, SO_ERROR, &so_err, &so_len);
            if (so_err != 0) {
                ::close(tcp); fire_error(OTA_CONNECT_ERROR); return;
            }
        }
        // Restore blocking mode for recv loop + SO_RCVTIMEO to work normally.
        fcntl(tcp, F_SETFL, cfl);

        if (!Update.begin(size, cmd == U_SPIFFS ? U_SPIFFS : U_FLASH)) {
            ::close(tcp); fire_error(OTA_BEGIN_ERROR); return;
        }
        if (!md5.empty()) Update.setMD5(md5.c_str());
        if (on_start_) on_start_();

        // FIX 3: bound recv loop so a stalled host can't hang sketch loop.
        timeval tv_recv{30, 0};
        setsockopt(tcp, SOL_SOCKET, SO_RCVTIMEO, &tv_recv, sizeof(tv_recv));

        size_t got = 0; char chunk[4096]; bool recv_ok = true;
        while (got < size) {
            ssize_t r = ::recv(tcp, chunk, sizeof(chunk), 0);
            if (r <= 0) { recv_ok = false; break; }
            // FIX 5: capture write count and check for Update error.
            size_t written = Update.write((uint8_t*)chunk, (size_t)r);
            got += written;
            if (Update.hasError()) { recv_ok = false; break; }
            if (on_progress_) on_progress_((unsigned)got, (unsigned)size);
        }
        if (!recv_ok) { Update.abort(); ::close(tcp); fire_error(OTA_RECEIVE_ERROR); return; }

        if (!Update.end(true)) {
            ::close(tcp); fire_error(OTA_END_ERROR); return;
        }
        const char* fin = "OK\n";
        ::send(tcp, fin, std::strlen(fin), 0);
        ::close(tcp);
        if (on_end_) on_end_();
    }

private:
    void fire_error(ota_error_t e) {
        std::fprintf(stderr, "[boardghost] ArduinoOTA: error %d\n", (int)e);
        if (on_error_) on_error_(e);
    }

    int         udp_fd_   = -1;
    uint16_t    port_     = 3232;
    std::string hostname_ = "esp32";
    std::string pass_md5_;
    bool        auth_     = false;
    bool        mdns_     = true;
};

}  // namespace boardghost_internal

namespace bgi = boardghost_internal;

ArduinoOTAClass::ArduinoOTAClass() : impl_(std::make_unique<bgi::OtaImpl>()) {}
ArduinoOTAClass::~ArduinoOTAClass() = default;

ArduinoOTAClass& ArduinoOTAClass::setPort(uint16_t port)          { impl_->set_port(port); return *this; }
ArduinoOTAClass& ArduinoOTAClass::setHostname(const char* h)      { impl_->set_hostname(h); return *this; }
String ArduinoOTAClass::getHostname()                             { return String(impl_->hostname().c_str()); }
ArduinoOTAClass& ArduinoOTAClass::setPassword(const char* p)      { impl_->set_password(p); return *this; }
ArduinoOTAClass& ArduinoOTAClass::setPasswordHash(const char* h)  { impl_->set_password_hash(h); return *this; }
ArduinoOTAClass& ArduinoOTAClass::setRebootOnSuccess(bool)        { return *this; }
ArduinoOTAClass& ArduinoOTAClass::setMdnsEnabled(bool e)          { impl_->set_mdns(e); return *this; }

void ArduinoOTAClass::onStart(THandlerFunction fn)               { impl_->on_start_ = std::move(fn); }
void ArduinoOTAClass::onEnd(THandlerFunction fn)                 { impl_->on_end_ = std::move(fn); }
void ArduinoOTAClass::onProgress(THandlerFunctionProgress fn)    { impl_->on_progress_ = std::move(fn); }
void ArduinoOTAClass::onError(THandlerFunctionError fn)          { impl_->on_error_ = std::move(fn); }

void ArduinoOTAClass::begin()  { impl_->begin(); }
void ArduinoOTAClass::end()    { impl_->stop(); }
void ArduinoOTAClass::handle() { impl_->handle(); }
int  ArduinoOTAClass::getCommand() { return impl_->command_; }

ArduinoOTAClass ArduinoOTA;
