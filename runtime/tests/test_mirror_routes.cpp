// Live integration test for the Phase 2 mirror display routes: start the real
// server with a registered display and round-trip /mirror/info,
// /mirror/screen.png and the /mirror/display MJPEG stream over HTTP. The pure
// helpers are unit-tested in test_mirror_auth.cpp; this covers the glue
// (capture → encode → HTTP) and the multipart framing.
#include <gtest/gtest.h>
#include <Arduino.h>
#include "sim_runtime.h"
#include "sim_mirror.h"
#include "LGFX_ILI9488_SDL.hpp"
#include "../third_party/cpp-httplib/httplib.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>

namespace {

uint16_t pick_ephemeral_port() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> d(18100, 18999);
    return (uint16_t)d(rng);
}

void wait_for_server(uint16_t port) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        httplib::Client probe("127.0.0.1", port);
        probe.set_connection_timeout(0, 100 * 1000);
        if (probe.Get("/mirror/info")) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

}  // namespace

TEST(MirrorRoutes, InfoScreenPngAndDisplayStream) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    // Keep the eager servers off during init (the suite already sets
    // BOARDGHOST_DEVTOOLS=off); we start ONLY the mirror, by hand, below.
    sim_runtime_init(0, nullptr);

    LGFX_ILI9488_SDL tft;
    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 60, 60, TFT_RED);
    sim_set_active_display(&tft);

    uint16_t port = pick_ephemeral_port();
    unsetenv("BOARDGHOST_DEVTOOLS");      // re-enable the mirror...
    unsetenv("BOARDGHOST_MIRROR");        // ...on loopback (no token)
    unsetenv("BOARDGHOST_MIRROR_TOKEN");
    setenv("BOARDGHOST_MIRROR_PORT", std::to_string(port).c_str(), 1);
    boardghost_mirror_start();
    wait_for_server(port);

    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(3, 0);

    // /mirror/info — dims reflect the registered display.
    auto info = cli.Get("/mirror/info");
    ASSERT_TRUE((bool)info);
    EXPECT_EQ(info->status, 200);
    EXPECT_NE(info->body.find("\"w\":" + std::to_string(tft.width())),
              std::string::npos);
    EXPECT_NE(info->body.find("\"h\":" + std::to_string(tft.height())),
              std::string::npos);

    // /mirror/screen.png — a real PNG with the right content type.
    auto png = cli.Get("/mirror/screen.png");
    ASSERT_TRUE((bool)png);
    EXPECT_EQ(png->status, 200);
    EXPECT_EQ(png->get_header_value("Content-Type"), "image/png");
    ASSERT_GE(png->body.size(), 8u);
    EXPECT_EQ((uint8_t)png->body[0], 0x89);
    EXPECT_EQ((uint8_t)png->body[1], 0x50);  // 'P'

    // /mirror/display — read until we have at least one full multipart frame,
    // then abort by returning false from the receiver.
    std::string acc;
    cli.Get("/mirror/display", [&](const char* d, size_t n) {
        acc.append(d, n);
        return acc.size() < 8192;  // stop once we've clearly got a frame
    });
    EXPECT_NE(acc.find("--frame"), std::string::npos);
    EXPECT_NE(acc.find("Content-Type: image/jpeg"), std::string::npos);
    auto soi = acc.find("\r\n\r\n");
    ASSERT_NE(soi, std::string::npos);
    EXPECT_EQ((uint8_t)acc[soi + 4], 0xFF);
    EXPECT_EQ((uint8_t)acc[soi + 5], 0xD8);  // JPEG Start-Of-Image

    sim_set_active_display(nullptr);
    boardghost_mirror_stop();
    sim_runtime_shutdown();
}
