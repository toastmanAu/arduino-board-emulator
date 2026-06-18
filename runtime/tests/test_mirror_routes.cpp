// Live integration test for the Phase 2 mirror display routes: start the real
// server with a registered display and round-trip /mirror/info,
// /mirror/screen.png and the /mirror/display MJPEG stream over HTTP. The pure
// helpers are unit-tested in test_mirror_auth.cpp; this covers the glue
// (capture → encode → HTTP) and the multipart framing.
#include <gtest/gtest.h>
#include <Arduino.h>
#include "sim_runtime.h"
#include "sim_mirror.h"
#include "sim_touch_inject.h"
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
    // Deterministic audio stream regardless of CI audio support: with sound
    // off the tap ring stays empty and /mirror/audio emits padded silence at a
    // steady cadence — exactly the underrun-proofing we want to assert.
    setenv("BOARDGHOST_SOUND", "off", 1);
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

    // /mirror/audio — chunked PCM at a steady cadence. Read at least one full
    // 20ms S16 mono chunk (882 samples * 2 bytes = 1764).
    std::string audio;
    cli.Get("/mirror/audio", [&](const char* d, size_t n) {
        audio.append(d, n);
        return audio.size() < 1764;
    });
    EXPECT_GE(audio.size(), 1764u);
    EXPECT_EQ(audio.size() % 2, 0u) << "S16 stream must be 2-byte aligned";

    // POST /mirror/touch — the HTTP → parse → inject chain reaches the queue
    // (Panel_sdl_bg consumption is covered in test_touch_inject).
    auto tr = cli.Post("/mirror/touch", "{\"x\":70,\"y\":40,\"space\":\"raw\"}",
                       "application/json");
    ASSERT_TRUE((bool)tr);
    EXPECT_EQ(tr->status, 200);
    int ix = 0, iy = 0; bool iscreen = true;
    ASSERT_TRUE(boardghost_pending_touch(ix, iy, iscreen));
    EXPECT_EQ(ix, 70);
    EXPECT_EQ(iy, 40);
    EXPECT_FALSE(iscreen) << "space:raw must disable screen-space";
    // Malformed body → 400, not a silent no-op.
    auto bad = cli.Post("/mirror/touch", "nope", "application/json");
    ASSERT_TRUE((bool)bad);
    EXPECT_EQ(bad->status, 400);

    sim_set_active_display(nullptr);
    boardghost_mirror_stop();
    sim_runtime_shutdown();
}

// The input-injection route must honour the token gate: a LAN-bind operator's
// token is the only thing standing between the network and synthetic input.
TEST(MirrorTouchAuth, GatedRouteRejectsWithoutTokenAcceptsWithIt) {
    const char* kTok = "s3cret-token-0123456789abcdef0123";
    unsetenv("BOARDGHOST_DEVTOOLS");
    unsetenv("BOARDGHOST_MIRROR");
    setenv("BOARDGHOST_MIRROR_TOKEN", kTok, 1);
    uint16_t port = pick_ephemeral_port();
    setenv("BOARDGHOST_MIRROR_PORT", std::to_string(port).c_str(), 1);
    boardghost_mirror_start();
    wait_for_server(port);

    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(2, 0);

    auto no = cli.Post("/mirror/touch", "{\"x\":1,\"y\":2}", "application/json");
    ASSERT_TRUE((bool)no);
    EXPECT_EQ(no->status, 401) << "POST /mirror/touch must require the token";

    httplib::Headers h{{"X-BoardGhost-Mirror", kTok}};
    auto ok = cli.Post("/mirror/touch", h, "{\"x\":1,\"y\":2}", "application/json");
    ASSERT_TRUE((bool)ok);
    EXPECT_EQ(ok->status, 200);

    boardghost_mirror_stop();
    unsetenv("BOARDGHOST_MIRROR_TOKEN");
}
