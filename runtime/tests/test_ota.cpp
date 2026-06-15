// Drives the ArduinoOTA espota receiver end-to-end over loopback. The test
// plays the role of the espota host: it opens a TCP listener, sends the UDP
// invite, then serves the firmware bytes; ArduinoOTA.handle() recvs the
// invite, connects back, and streams into the Update shim.

#include <gtest/gtest.h>
#include "ArduinoOTA.h"
#include "Update.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/evp.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

std::string md5_hex(const std::string& in);  // defined below via OpenSSL

class OtaTest : public ::testing::Test {
protected:
    fs::path ota_path;
    uint16_t ota_port;

    void SetUp() override {
        std::random_device rd; std::mt19937 rng(rd());
        int n = std::uniform_int_distribution<int>(20000, 40000)(rng);
        ota_path = fs::temp_directory_path() / ("bgh-ota-" + std::to_string(n) + ".bin");
        ota_port = (uint16_t)(40000 + (n % 20000));
        setenv("BOARDGHOST_OTA_PATH", ota_path.c_str(), 1);
        setenv("BOARDGHOST_OTA_PORT", std::to_string(ota_port).c_str(), 1);
        setenv("BOARDGHOST_OTA_BIND", "127.0.0.1", 1);
        setenv("BOARDGHOST_NET", "real", 1);
        setenv("BOARDGHOST_MDNS", "off", 1);
    }
    void TearDown() override {
        std::error_code ec; fs::remove(ota_path, ec);
        unsetenv("BOARDGHOST_OTA_PATH"); unsetenv("BOARDGHOST_OTA_PORT");
        unsetenv("BOARDGHOST_OTA_BIND"); unsetenv("BOARDGHOST_NET");
        unsetenv("BOARDGHOST_MDNS");
    }
    std::string read_file() {
        std::ifstream f(ota_path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
};

// Minimal espota host. Returns true if the device sent the closing "OK".
// `password` empty => no-auth invite. Drives one upload.
bool espota_push(uint16_t device_port, const std::string& firmware,
                 const std::string& password = "") {
    // 1. TCP listener on an ephemeral port.
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in la{}; la.sin_family = AF_INET; la.sin_addr.s_addr = htonl(INADDR_LOOPBACK); la.sin_port = 0;
    if (::bind(srv, (sockaddr*)&la, sizeof(la)) != 0) { ::close(srv); return false; }
    socklen_t ll = sizeof(la); getsockname(srv, (sockaddr*)&la, &ll);
    uint16_t host_port = ntohs(la.sin_port);
    ::listen(srv, 1);

    // 2. UDP invite to the device.
    int u = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in da{}; da.sin_family = AF_INET; da.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    da.sin_port = htons(device_port);
    std::string md5 = md5_hex(firmware);
    std::string invite = "0 " + std::to_string(host_port) + " " +
                         std::to_string(firmware.size()) + " " + md5 + "\n";
    ::sendto(u, invite.data(), invite.size(), 0, (sockaddr*)&da, sizeof(da));

    // 3. Read the device's UDP reply ("OK" or "AUTH <nonce>").
    char rb[256]; sockaddr_in from{}; socklen_t fl = sizeof(from);
    timeval tv{2, 0}; setsockopt(u, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ssize_t rn = ::recvfrom(u, rb, sizeof(rb) - 1, 0, (sockaddr*)&from, &fl);
    if (rn <= 0) { ::close(u); ::close(srv); return false; }
    rb[rn] = 0;
    std::string reply(rb);
    if (reply.rfind("AUTH", 0) == 0) {
        std::string nonce = reply.substr(5);
        while (!nonce.empty() && (nonce.back()=='\n'||nonce.back()=='\r'||nonce.back()==' ')) nonce.pop_back();
        std::string passmd5 = md5_hex(password);
        std::string cnonce  = md5_hex(firmware + std::to_string(host_port));
        std::string result  = md5_hex(passmd5 + ":" + nonce + ":" + cnonce);
        std::string authmsg = cnonce + " " + result + "\n";
        ::sendto(u, authmsg.data(), authmsg.size(), 0, (sockaddr*)&da, sizeof(da));
        rn = ::recvfrom(u, rb, sizeof(rb) - 1, 0, (sockaddr*)&from, &fl);
        if (rn <= 0) { ::close(u); ::close(srv); return false; }
        rb[rn] = 0; reply = rb;
        if (reply.rfind("OK", 0) != 0) { ::close(u); ::close(srv); return false; }
    }

    // 4. Accept the device's TCP connect-back and stream the firmware.
    int c = ::accept(srv, nullptr, nullptr);
    ::close(srv);
    if (c < 0) { ::close(u); return false; }
    size_t sent = 0;
    while (sent < firmware.size()) {
        ssize_t w = ::send(c, firmware.data() + sent, firmware.size() - sent, 0);
        if (w <= 0) break;
        sent += (size_t)w;
    }
    // 5. Read the device's closing status.
    char fin[64]; ssize_t fn = ::recv(c, fin, sizeof(fin) - 1, 0);
    ::close(c); ::close(u);
    if (fn <= 0) return false;
    fin[fn] = 0;
    return std::string(fin).rfind("OK", 0) == 0;
}

// OpenSSL MD5 (mirrors sim_update.cpp). Test-local helper.
std::string md5_hex(const std::string& in) {
    unsigned char d[EVP_MAX_MD_SIZE]; unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(ctx, in.data(), in.size());
    EVP_DigestFinal_ex(ctx, d, &len);
    EVP_MD_CTX_free(ctx);
    static const char* h = "0123456789abcdef";
    std::string o; o.reserve(len * 2);
    for (unsigned i = 0; i < len; i++) { o += h[d[i] >> 4]; o += h[d[i] & 0xf]; }
    return o;
}

}  // namespace

TEST_F(OtaTest, ReceivesFirmwareAndFiresCallbacks) {
    ArduinoOTA.setPort(ota_port);
    std::atomic<bool> done{false}, started{false};
    std::atomic<unsigned> last_total{0};
    ArduinoOTA.onStart([&]{ started = true; });
    ArduinoOTA.onProgress([&](unsigned, unsigned total){ last_total = total; });
    ArduinoOTA.onEnd([&]{ done = true; });
    ArduinoOTA.begin();

    std::string fw = "OTA-FIRMWARE-" + std::string(4096, 'Z');
    std::thread host([&]{ EXPECT_TRUE(espota_push(ota_port, fw)); });

    // Pump handle() until the transfer completes (or time out).
    for (int i = 0; i < 500 && !done.load(); ++i) {
        ArduinoOTA.handle();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    host.join();
    ArduinoOTA.end();

    EXPECT_TRUE(started.load());
    EXPECT_TRUE(done.load());
    EXPECT_EQ(last_total.load(), fw.size());
    EXPECT_EQ(read_file(), fw);
}

TEST_F(OtaTest, WrongPasswordTriggersAuthError) {
    ArduinoOTA.setPort(ota_port);
    ArduinoOTA.setPassword("correct-horse");
    std::atomic<bool> err{false}; std::atomic<int> code{-1};
    ArduinoOTA.onError([&](ota_error_t e){ err = true; code = (int)e; });
    ArduinoOTA.begin();

    std::string fw = "should-not-land";
    std::thread host([&]{ espota_push(ota_port, fw, "wrong-password"); });
    for (int i = 0; i < 400 && !err.load(); ++i) {
        ArduinoOTA.handle();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    host.join();
    ArduinoOTA.end();

    EXPECT_TRUE(err.load());
    EXPECT_EQ(code.load(), (int)OTA_AUTH_ERROR);
    EXPECT_FALSE(fs::exists(ota_path));   // rejected before any write
}
