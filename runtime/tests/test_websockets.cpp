// Phase 5 — real WebSocket client.
//
// Spins up a localhost ws:// echo server using cpp-httplib's WebSocket
// handler, then drives WebsocketsClient through a connect → send → poll →
// receive cycle. Verifies the Arduino-style callback semantics: the message
// callback fires from the sketch thread (poll()'s caller), not the network
// reader thread.

#include <gtest/gtest.h>
#include "ArduinoWebsockets.h"
#include "../third_party/cpp-httplib/httplib.h"

#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <thread>

namespace {

uint16_t pick_port() {
    std::random_device rd;
    std::mt19937 rng(rd());
    return (uint16_t)std::uniform_int_distribution<int>(20000, 20999)(rng);
}

struct EchoServer {
    httplib::Server srv;
    std::thread th;

    explicit EchoServer(uint16_t port) {
        srv.WebSocket("/echo", [](const httplib::Request&, httplib::ws::WebSocket& ws) {
            std::string msg;
            while (ws.is_open()) {
                auto r = ws.read(msg);
                if (r == httplib::ws::ReadResult::Fail) {
                    if (!ws.is_open()) return;
                    continue;
                }
                ws.send(msg);
                msg.clear();
            }
        });
        srv.bind_to_port("127.0.0.1", port);
        th = std::thread([this]() { srv.listen_after_bind(); });
        // Poll for liveness — just a GET probe; if we get any response the
        // listener is up.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            httplib::Client probe("127.0.0.1", port);
            probe.set_connection_timeout(0, 100 * 1000);
            if (probe.Get("/__probe")) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    ~EchoServer() {
        srv.stop();
        if (th.joinable()) th.join();
    }
};

}  // namespace

TEST(WebSocketsReal, EchoRoundtripDispatchesCallbackOnPollThread) {
    setenv("BOARDGHOST_NET", "real", 1);
    uint16_t port = pick_port();
    EchoServer server(port);

    WebsocketsClient client;
    std::string received;
    std::thread::id callback_thread_id;
    bool opened = false;

    client.onMessage([&](WebsocketsMessage m) {
        received          = m.data();
        callback_thread_id = std::this_thread::get_id();
    });
    client.onEvent([&](WebsocketsEvent ev, std::string) {
        if (ev == WebsocketsEvent::ConnectionOpened) opened = true;
    });

    ASSERT_TRUE(client.connect("ws://127.0.0.1:" + std::to_string(port) + "/echo"));

    // Drain the "opened" event from poll() on this thread.
    auto sketch_thread = std::this_thread::get_id();
    for (int i = 0; i < 50 && !opened; ++i) {
        client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(opened);

    EXPECT_TRUE(client.send("phase5"));

    // Wait for the echo + drain callback via poll().
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (received.empty() && std::chrono::steady_clock::now() < deadline) {
        client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_EQ(received, "phase5");
    // Critical: the callback ran on the sketch (test) thread, not the reader.
    EXPECT_EQ(callback_thread_id, sketch_thread);

    client.close();
}

TEST(WebSocketsReal, ConnectFailsAgainstClosedPort) {
    setenv("BOARDGHOST_NET", "real", 1);
    WebsocketsClient client;
    // Port 1 is essentially always closed on loopback for normal dev envs.
    EXPECT_FALSE(client.connect("ws://127.0.0.1:1/nope"));
    EXPECT_FALSE(client.available());
}

TEST(WebSockets, FakeModeReportsOpenedAndAvailable) {
    setenv("BOARDGHOST_NET", "fake", 1);
    WebsocketsClient client;
    bool opened = false;
    client.onEvent([&](WebsocketsEvent ev, std::string) {
        if (ev == WebsocketsEvent::ConnectionOpened) opened = true;
    });
    EXPECT_TRUE(client.connect("ws://example.invalid/whatever"));
    EXPECT_TRUE(client.available());
    client.poll();   // drain opened event
    EXPECT_TRUE(opened);
    client.close();
    EXPECT_FALSE(client.available());
}
