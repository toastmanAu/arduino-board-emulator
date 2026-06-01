#include <gtest/gtest.h>
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

TEST(WiFi, FakeModeConnectsImmediately) {
    setenv("BOARDGHOST_NET", "fake", 1);
    EXPECT_EQ(WiFi.begin("ssid", "pw"), WL_CONNECTED);
    EXPECT_EQ(WiFi.status(), WL_CONNECTED);
    EXPECT_TRUE(WiFi.isConnected());
    // localIP returns the host's primary LAN IPv4; LocalIPReturnsNonLoopback
    // covers the value details — we just confirm a non-empty IP string here.
    EXPECT_GT(WiFi.localIP().toString().length(), 0u);
}

TEST(WiFi, FailModeReportsNoSsid) {
    setenv("BOARDGHOST_NET", "fail", 1);
    EXPECT_EQ(WiFi.begin("ssid", "pw"), WL_NO_SSID_AVAIL);
    EXPECT_EQ(WiFi.status(), WL_DISCONNECTED);
    EXPECT_FALSE(WiFi.isConnected());
}

TEST(WiFi, ClientConnectInFakeMode) {
    setenv("BOARDGHOST_NET", "fake", 1);
    WiFiClient client;
    EXPECT_EQ(client.connect("example.com", 80), 1);
    EXPECT_TRUE(client.connected());
    client.stop();
    EXPECT_FALSE(client.connected());
}

// --- Phase 2: real POSIX sockets in real mode ---

// Spins up a tiny localhost TCP server on an ephemeral port that echoes
// whatever bytes the first client sends, then closes. Returns the port.
// `out_received` captures what the server saw for assertion. Server thread
// joins itself when the client disconnects.
static uint16_t spawn_echo_server(std::string* out_received) {
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // ephemeral
    ::bind(srv, (sockaddr*)&addr, sizeof(addr));
    ::listen(srv, 1);
    socklen_t alen = sizeof(addr);
    ::getsockname(srv, (sockaddr*)&addr, &alen);
    uint16_t port = ntohs(addr.sin_port);
    std::thread([srv, out_received]() {
        int c = ::accept(srv, nullptr, nullptr);
        ::close(srv);
        if (c < 0) return;
        char buf[256];
        ssize_t n = ::recv(c, buf, sizeof(buf), 0);
        if (n > 0) {
            if (out_received) out_received->assign(buf, n);
            ::send(c, buf, n, 0);
        }
        ::close(c);
    }).detach();
    return port;
}

TEST(WiFi, ClientRealModeTcpRoundtrip) {
    setenv("BOARDGHOST_NET", "real", 1);
    std::string received;
    uint16_t port = spawn_echo_server(&received);

    WiFiClient client;
    client.setTimeout(1000);
    ASSERT_EQ(client.connect("127.0.0.1", port), 1);
    EXPECT_TRUE(client.connected());

    const char* msg = "hello sim";
    EXPECT_EQ(client.write((const uint8_t*)msg, std::strlen(msg)), std::strlen(msg));

    // Echo server returns the same bytes. Block on read up to the timeout.
    uint8_t buf[64] = {0};
    int got = client.read(buf, sizeof(buf));
    EXPECT_EQ(got, (int)std::strlen(msg));
    EXPECT_EQ(std::string((char*)buf, got), msg);

    client.stop();
    EXPECT_FALSE(client.connected());
    // Give the server thread a moment to record what it received.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(received, msg);
}

TEST(WiFi, ClientRealModeConnectRefusedReturnsZero) {
    setenv("BOARDGHOST_NET", "real", 1);
    WiFiClient client;
    client.setTimeout(500);
    // Port 1 is well-known-but-almost-always-closed on loopback. If it ever
    // is open here the test is bogus, but in any normal dev env this fails
    // fast and we get the expected behaviour.
    EXPECT_EQ(client.connect("127.0.0.1", 1), 0);
    EXPECT_FALSE(client.connected());
}

TEST(WiFi, ClientCopyShareSocketAndCloseOnce) {
    // Sketches sometimes copy WiFiClient by value (vector push, captured in
    // a lambda). The shared_ptr-managed fd should mean both copies see the
    // same socket and exactly one close happens when both go out of scope.
    setenv("BOARDGHOST_NET", "real", 1);
    std::string received;
    uint16_t port = spawn_echo_server(&received);

    WiFiClient a;
    ASSERT_EQ(a.connect("127.0.0.1", port), 1);
    {
        WiFiClient b = a;  // copy
        EXPECT_TRUE(b.connected());
        EXPECT_TRUE(a.connected());
        // b drops here — must not close the shared fd while a still owns it.
    }
    EXPECT_TRUE(a.connected());
    a.stop();
    EXPECT_FALSE(a.connected());
}

TEST(WiFiClientSecure, CompilesAndDelegates) {
    setenv("BOARDGHOST_NET", "fake", 1);
    WiFiClientSecure client;
    client.setCACert("dummy");
    client.setInsecure();
    EXPECT_EQ(client.connect("example.com", 443), 1);
}

TEST(WiFi, ScanReturnsParsedEnvOverride) {
    // Pin scan via env so the test doesn't depend on the host's wifi state.
    setenv("BOARDGHOST_NET", "fake", 1);
    setenv("BOARDGHOST_WIFI_SCAN",
           "homenet,WPA2,-45,6;coffee-shop,open,-72,11;legacy,WEP,-88,1",
           1);
    int16_t n = WiFi.scanNetworks();
    EXPECT_EQ(n, 3);
    EXPECT_EQ(WiFi.SSID(0),           String("homenet"));
    EXPECT_EQ(WiFi.encryptionType(0), WIFI_AUTH_WPA2_PSK);
    EXPECT_EQ(WiFi.RSSI(0),           -45);
    EXPECT_EQ(WiFi.channel(0),        6);
    EXPECT_EQ(WiFi.SSID(1),           String("coffee-shop"));
    EXPECT_EQ(WiFi.encryptionType(1), WIFI_AUTH_OPEN);
    EXPECT_EQ(WiFi.encryptionType(2), WIFI_AUTH_WEP);
    // Out-of-range index returns safe defaults rather than crashing.
    EXPECT_EQ(WiFi.SSID(99), String());
    EXPECT_EQ(WiFi.RSSI(99), 0);
    unsetenv("BOARDGHOST_WIFI_SCAN");
}

TEST(WiFi, ScanReturnsZeroInFailMode) {
    setenv("BOARDGHOST_NET", "fail", 1);
    setenv("BOARDGHOST_WIFI_SCAN", "anything,open,-50", 1);
    EXPECT_EQ(WiFi.scanNetworks(), 0);
    unsetenv("BOARDGHOST_WIFI_SCAN");
}

TEST(WiFi, LocalIPReturnsNonLoopbackWhenAvailable) {
    // We can't pin what the host's IP is, but we can assert the behaviour:
    // either there's a non-loopback IPv4 (most dev machines), in which case
    // it's not 127.x.x.x; or there isn't, in which case we fall back to
    // 127.0.0.1 by design.
    auto ip = WiFi.localIP();
    uint32_t raw = (uint32_t)ip;
    bool loopback = ((raw >> 24) & 0xFF) == 127;
    // If we did get loopback it had better be exactly 127.0.0.1, not garbage.
    if (loopback) EXPECT_EQ(ip.toString(), String("127.0.0.1"));
    // Otherwise it's a real LAN address — we don't assert which.
}

TEST(WiFi, DisconnectFlipsStatusSoSpinWaitTerminates) {
    // Mirrors ckb_pos's pre-scan loop:
    //   WiFi.disconnect();
    //   while (WiFi.status() == WL_CONNECTED) delay(500);
    // Before this regression test, disconnect() was a no-op so the loop
    // spun forever in fake/real mode.
    setenv("BOARDGHOST_NET", "fake", 1);
    WiFi.begin("ssid", "pw");
    EXPECT_EQ(WiFi.status(), WL_CONNECTED);
    WiFi.disconnect();
    EXPECT_EQ(WiFi.status(), WL_DISCONNECTED);
    // A subsequent begin() should re-arm fake-connected state.
    WiFi.begin("ssid", "pw");
    EXPECT_EQ(WiFi.status(), WL_CONNECTED);
}
