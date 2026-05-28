#include <gtest/gtest.h>
#include "WiFi.h"
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
