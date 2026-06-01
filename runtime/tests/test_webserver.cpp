// End-to-end smoke for the WebServer shim: register a handful of routes the
// way a sketch does, hit the bound port with a real HTTP client (we re-use
// cpp-httplib::Client since it's already in the build), assert the handlers
// fired and the response body / status / headers came back as set.
//
// Uses an ephemeral port via BOARDGHOST_WEBSERVER_PORT so concurrent test
// runs don't collide. picks 18000 + random(0..999) — fully reliable would
// need bind-then-getsockname plumbed through, but the chance of two CTest
// processes hitting the same number in the same second is negligible here.

#include <gtest/gtest.h>
#include "WebServer.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>

namespace {

uint16_t pick_ephemeral_port() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> d(18000, 18999);
    return (uint16_t)d(rng);
}

void wait_for_server(uint16_t port) {
    // Poll for up to 2s waiting for the server thread to actually accept.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        httplib::Client probe("127.0.0.1", port);
        probe.set_connection_timeout(0, 100 * 1000);  // 100ms
        if (probe.Get("/__probe")) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

}  // namespace

TEST(WebServerReal, GetRouteFiresAndResponseFlowsBack) {
    uint16_t port = pick_ephemeral_port();
    setenv("BOARDGHOST_WEBSERVER_PORT", std::to_string(port).c_str(), 1);

    WebServer server(80);  // port is overridden by env
    bool handler_ran = false;
    server.on("/hello", HTTP_GET, [&]() {
        handler_ran = true;
        server.sendHeader("X-From", "sim");
        server.send(200, "text/plain", "hi from sketch");
    });
    server.begin();
    wait_for_server(port);

    httplib::Client client("127.0.0.1", port);
    auto res = client.Get("/hello");
    ASSERT_TRUE((bool)res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "hi from sketch");
    EXPECT_EQ(res->get_header_value("X-From"), "sim");
    EXPECT_TRUE(handler_ran);

    server.stop();
    unsetenv("BOARDGHOST_WEBSERVER_PORT");
}

TEST(WebServerReal, PostHandlerSeesArgsAndHeaders) {
    uint16_t port = pick_ephemeral_port();
    setenv("BOARDGHOST_WEBSERVER_PORT", std::to_string(port).c_str(), 1);

    WebServer server(80);
    String observed_arg, observed_header;
    int    observed_method = -1;
    server.on("/tryLogin", HTTP_POST, [&]() {
        observed_arg    = server.arg("user");
        observed_header = server.header("Authorization");
        observed_method = server.method();
        server.send(200, "application/json", String("{\"ok\":true}"));
    });
    server.begin();
    wait_for_server(port);

    httplib::Client client("127.0.0.1", port);
    httplib::Headers hdrs = {{"Authorization", "Bearer test123"}};
    auto res = client.Post("/tryLogin", hdrs, "user=phill&pass=hunter2",
                            "application/x-www-form-urlencoded");
    ASSERT_TRUE((bool)res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "{\"ok\":true}");
    EXPECT_EQ(observed_arg,    String("phill"));
    EXPECT_EQ(observed_header, String("Bearer test123"));
    EXPECT_EQ(observed_method, (int)HTTP_POST);

    server.stop();
    unsetenv("BOARDGHOST_WEBSERVER_PORT");
}

TEST(WebServerReal, NotFoundFiresOnUnknownRoute) {
    uint16_t port = pick_ephemeral_port();
    setenv("BOARDGHOST_WEBSERVER_PORT", std::to_string(port).c_str(), 1);

    WebServer server(80);
    bool nf_ran = false;
    server.onNotFound([&]() {
        nf_ran = true;
        server.send(404, "text/plain", "nope");
    });
    server.begin();
    wait_for_server(port);

    httplib::Client client("127.0.0.1", port);
    auto res = client.Get("/no-such-route");
    ASSERT_TRUE((bool)res);
    EXPECT_EQ(res->status, 404);
    EXPECT_EQ(res->body, "nope");
    EXPECT_TRUE(nf_ran);

    server.stop();
    unsetenv("BOARDGHOST_WEBSERVER_PORT");
}

TEST(WebServerReal, EnableCORSAddsAllowOriginHeader) {
    uint16_t port = pick_ephemeral_port();
    setenv("BOARDGHOST_WEBSERVER_PORT", std::to_string(port).c_str(), 1);

    WebServer server(80);
    server.enableCORS(true);
    server.on("/", HTTP_GET, [&]() {
        server.send(200, "text/plain", "ok");
    });
    server.begin();
    wait_for_server(port);

    httplib::Client client("127.0.0.1", port);
    auto res = client.Get("/");
    ASSERT_TRUE((bool)res);
    EXPECT_EQ(res->get_header_value("Access-Control-Allow-Origin"), "*");

    server.stop();
    unsetenv("BOARDGHOST_WEBSERVER_PORT");
}
