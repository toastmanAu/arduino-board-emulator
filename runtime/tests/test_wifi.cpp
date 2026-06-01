#include <gtest/gtest.h>
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include <cstdlib>

TEST(WiFi, FakeModeConnectsImmediately) {
    setenv("BOARDGHOST_NET", "fake", 1);
    EXPECT_EQ(WiFi.begin("ssid", "pw"), WL_CONNECTED);
    EXPECT_EQ(WiFi.status(), WL_CONNECTED);
    EXPECT_TRUE(WiFi.isConnected());
    EXPECT_EQ(WiFi.localIP().toString(), String("127.0.0.1"));
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

TEST(WiFiClientSecure, CompilesAndDelegates) {
    setenv("BOARDGHOST_NET", "fake", 1);
    WiFiClientSecure client;
    client.setCACert("dummy");
    client.setInsecure();
    EXPECT_EQ(client.connect("example.com", 443), 1);
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
