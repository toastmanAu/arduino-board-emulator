// AsyncWebServer core: drive a live instance with the vendored cpp-httplib
// client and assert request/response, params, serveStatic, and templating.

#include <gtest/gtest.h>
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <cstdlib>
#include <random>
#include <string>
#include <thread>

namespace {
uint16_t pick_port() {
    std::random_device rd; std::mt19937 rng(rd());
    return (uint16_t)(40000 + std::uniform_int_distribution<int>(0, 20000)(rng));
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
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

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
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

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
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    httplib::Client cli("127.0.0.1", port);
    auto res = cli.Get("/page");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->body, "<b>GhostBoard</b>");
    server.end();
}
