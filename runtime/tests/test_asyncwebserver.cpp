// AsyncWebServer core: drive a live instance with the vendored cpp-httplib
// client and assert request/response, params, onNotFound fidelity (matched
// route 404 must NOT invoke onNotFound), serveStatic file serving, and
// template-processor substitution.

#include <gtest/gtest.h>
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <chrono>

namespace {
uint16_t pick_port() {
    std::random_device rd; std::mt19937 rng(rd());
    return (uint16_t)(40000 + std::uniform_int_distribution<int>(0, 20000)(rng));
}

// Wait until the server is accepting connections (replaces a fixed sleep).
void wait_ready(uint16_t port) {
    httplib::Client probe("127.0.0.1", port);
    probe.set_connection_timeout(0, 100000);  // 100ms
    for (int i = 0; i < 40; ++i) {
        if (probe.Get("/__ready_probe__")) return;   // any response (even 404) = listening
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
}  // namespace

TEST(AsyncWebServerTest, SendAndParamsRoundtrip) {
    uint16_t port = pick_port();
    AsyncWebServer server(port);
    server.on("/hello", HTTP_GET, [](AsyncWebServerRequest* req) {
        String who = req->hasParam("name") ? req->getParam("name")->value() : String("world");
        req->send(200, "text/plain", String("hi ") + who);
    });
    server.begin();
    wait_ready(port);

    httplib::Client cli("127.0.0.1", port);
    auto res = cli.Get("/hello?name=ghost");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "hi ghost");

    auto res2 = cli.Get("/hello");
    ASSERT_TRUE(res2 != nullptr);
    EXPECT_EQ(res2->body, "hi world");

    server.end();
}

TEST(AsyncWebServerTest, OnNotFoundFires) {
    uint16_t port = pick_port();
    AsyncWebServer server(port);
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "nope");
    });
    server.begin();
    wait_ready(port);

    httplib::Client cli("127.0.0.1", port);
    auto res = cli.Get("/does-not-exist");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
    EXPECT_EQ(res->body, "nope");
    server.end();
}

TEST(AsyncWebServerTest, TemplateProcessorSubstitutes) {
    uint16_t port = pick_port();
    AsyncWebServer server(port);
    server.on("/page", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", "<b>%TITLE%</b>", [](const String& var) -> String {
            if (var == "TITLE") return String("GhostBoard");
            return String();
        });
    });
    server.begin();
    wait_ready(port);

    httplib::Client cli("127.0.0.1", port);
    auto res = cli.Get("/page");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->body, "<b>GhostBoard</b>");
    server.end();
}

TEST(AsyncWebServerTest, MatchedRoute404DoesNotTriggerNotFound) {
    uint16_t port = pick_port();
    AsyncWebServer server(port);
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "global-not-found");
    });
    server.on("/api/item", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "no-such-item");   // matched route, deliberate 404
    });
    server.begin();
    wait_ready(port);

    httplib::Client cli("127.0.0.1", port);
    auto matched = cli.Get("/api/item");
    ASSERT_TRUE(matched != nullptr);
    EXPECT_EQ(matched->status, 404);
    EXPECT_EQ(matched->body, "no-such-item");      // route's body, NOT the onNotFound body

    auto missing = cli.Get("/totally-unknown");
    ASSERT_TRUE(missing != nullptr);
    EXPECT_EQ(missing->body, "global-not-found");  // real 404 still hits onNotFound
    server.end();
}

TEST(AsyncWebServerTest, ServeStaticServesFileFromFS) {
    // Set up SPIFFS with a temp dir (mirrors test_fs.cpp pattern).
    namespace std_fs = std::filesystem;
    std_fs::path assets = std_fs::temp_directory_path() / "bg-asyncweb-test";
    std_fs::remove_all(assets);
    std_fs::create_directories(assets);
    setenv("BOARDGHOST_ASSETS_DIR", assets.c_str(), 1);

    ASSERT_TRUE(SPIFFS.begin(true));

    // Write a test HTML file via the FS shim.
    {
        auto f = SPIFFS.open("/index.html", FILE_WRITE);
        ASSERT_TRUE(f);
        const char* html = "<html><body>hello</body></html>";
        f.write(reinterpret_cast<const uint8_t*>(html), strlen(html));
    }

    uint16_t port = pick_port();
    AsyncWebServer server(port);
    server.serveStatic("/index.html", SPIFFS, "/index.html");
    server.begin();
    wait_ready(port);

    httplib::Client cli("127.0.0.1", port);
    auto res = cli.Get("/index.html");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("<html>"), std::string::npos);

    server.end();
    std_fs::remove_all(assets);
}

TEST(AsyncWebServerTest, EventSourceDeliversFrame) {
    uint16_t port = pick_port();
    AsyncWebServer server(port);
    AsyncEventSource events("/events");
    server.addHandler(&events);
    server.begin();
    wait_ready(port);

    // Receive the stream on a background thread; stop after the first frame.
    std::string received;
    std::thread reader([&]{
        httplib::Client cli("127.0.0.1", port);
        cli.set_read_timeout(3, 0);
        cli.Get("/events", [&](const char* data, size_t len) {
            received.append(data, len);
            return received.find("\n\n") == std::string::npos;  // stop after one frame
        });
    });

    // Spin until client registers, then push an event.
    for (int i = 0; i < 50 && events.count() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_GE(events.count(), (size_t)1);
    events.send("hello-sse", "tick");

    reader.join();
    server.end();

    EXPECT_NE(received.find("event: tick"), std::string::npos);
    EXPECT_NE(received.find("data: hello-sse"), std::string::npos);
}

TEST(AsyncWebServerTest, EventSourceImplOutlivesWrapper) {
    // Regression: AsyncServerImpl must co-own the EventSourceImpl via shared_ptr,
    // so httplib lambdas (and stop()->shutdown()) don't touch freed memory if the
    // AsyncEventSource wrapper is destroyed before the server.
    uint16_t port = pick_port();
    AsyncWebServer server(port);
    {
        AsyncEventSource events("/events");
        server.addHandler(&events);
        server.begin();
        wait_ready(port);
    }   // `events` (the wrapper) is destroyed here, before server.end()
    // Hitting the endpoint + tearing down must not use-after-free.
    httplib::Client cli("127.0.0.1", port);
    cli.set_read_timeout(1, 0);
    cli.Get("/events", [](const char*, size_t) { return false; });  // connect then drop
    server.end();   // stop()->shutdown() on the co-owned impl must be safe
    SUCCEED();
}
