// PubSubClient end-to-end test. Spins up a minimal MQTT 3.1.1 broker
// in-process (~200 lines of POSIX-socket code) and drives a sketch-style
// connect → subscribe → publish → loop cycle against it. Doesn't depend on
// mosquitto or any other system MQTT broker.
//
// The broker is deliberately minimal:
//   - Accepts CONNECT, replies CONNACK accepted
//   - Tracks a single subscriber's topic
//   - On PUBLISH to that topic, fans out PUBLISH back to subscribers (loopback)
//   - Replies SUBACK / UNSUBACK / PINGRESP as needed
//   - QoS 0 only (matches the client)
//
// Bugs in the broker would show up as bugs in this test, but the wire
// format follows the spec section-by-section so the surface area is small.

#include <gtest/gtest.h>
#include "PubSubClient.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

uint16_t pick_port() {
    std::random_device rd;
    std::mt19937 rng(rd());
    return (uint16_t)std::uniform_int_distribution<int>(21000, 21999)(rng);
}

// Minimal MQTT broker — accepts one client, parses + responds to the packets
// PubSubClient sends. Subscriber state is in-memory; PUBLISH fans out to
// every connection that subscribed to the matching topic.
struct Broker {
    int               srv_fd = -1;
    std::atomic<bool> stop{false};
    std::thread       th;

    explicit Broker(uint16_t port) {
        srv_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        ::setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        ::bind(srv_fd, (sockaddr*)&addr, sizeof(addr));
        ::listen(srv_fd, 4);
        th = std::thread([this]() { run(); });
    }
    ~Broker() {
        stop.store(true);
        if (srv_fd >= 0) ::shutdown(srv_fd, SHUT_RDWR);
        if (srv_fd >= 0) ::close(srv_fd);
        if (th.joinable()) th.join();
    }

    static bool read_n(int fd, uint8_t* buf, size_t n) {
        size_t got = 0;
        while (got < n) {
            ssize_t r = ::recv(fd, buf + got, n - got, 0);
            if (r <= 0) return false;
            got += (size_t)r;
        }
        return true;
    }
    static bool read_varint(int fd, size_t& out) {
        out = 0; size_t mult = 1;
        for (int i = 0; i < 4; ++i) {
            uint8_t b; if (!read_n(fd, &b, 1)) return false;
            out += (b & 0x7F) * mult;
            if (!(b & 0x80)) return true;
            mult *= 128;
        }
        return false;
    }
    static void write_varint(std::vector<uint8_t>& v, size_t n) {
        do {
            uint8_t b = n & 0x7F; n >>= 7;
            if (n > 0) b |= 0x80;
            v.push_back(b);
        } while (n > 0);
    }
    static void write_n(int fd, const std::vector<uint8_t>& v) {
        size_t off = 0;
        while (off < v.size()) {
            ssize_t w = ::send(fd, v.data() + off, v.size() - off, MSG_NOSIGNAL);
            if (w <= 0) return;
            off += (size_t)w;
        }
    }
    static std::vector<uint8_t> pkt(uint8_t type, const std::vector<uint8_t>& body) {
        std::vector<uint8_t> v;
        v.push_back(type);
        write_varint(v, body.size());
        v.insert(v.end(), body.begin(), body.end());
        return v;
    }

    void run() {
        int cli = ::accept(srv_fd, nullptr, nullptr);
        if (cli < 0) return;
        std::string subscribed_topic;
        while (!stop.load()) {
            uint8_t hdr; if (!read_n(cli, &hdr, 1)) break;
            size_t remaining; if (!read_varint(cli, remaining)) break;
            std::vector<uint8_t> body(remaining);
            if (remaining > 0 && !read_n(cli, body.data(), remaining)) break;
            uint8_t type = hdr & 0xF0;
            if (type == 0x10) {           // CONNECT → CONNACK accepted
                write_n(cli, pkt(0x20, {0x00, 0x00}));
            } else if (type == 0x30) {    // PUBLISH inbound
                size_t tlen = (body[0] << 8) | body[1];
                std::string topic((char*)&body[2], tlen);
                if (topic == subscribed_topic) {
                    // Fan back to the same client (loopback test).
                    write_n(cli, pkt(0x30, body));
                }
            } else if (type == 0x80) {    // SUBSCRIBE → SUBACK granted-QoS-0
                // body: packet id (2) + filters [len(2) + topic + qos(1)]+
                uint16_t pid = (body[0] << 8) | body[1];
                size_t tlen = (body[2] << 8) | body[3];
                subscribed_topic.assign((char*)&body[4], tlen);
                write_n(cli, pkt(0x90, {(uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF), 0x00}));
            } else if (type == 0xA0) {    // UNSUBSCRIBE → UNSUBACK
                uint16_t pid = (body[0] << 8) | body[1];
                subscribed_topic.clear();
                write_n(cli, pkt(0xB0, {(uint8_t)(pid >> 8), (uint8_t)(pid & 0xFF)}));
            } else if (type == 0xC0) {    // PINGREQ → PINGRESP
                write_n(cli, pkt(0xD0, {}));
            } else if (type == 0xE0) {    // DISCONNECT
                break;
            }
        }
        ::close(cli);
    }
};

}  // namespace

TEST(PubSubClientReal, ConnectSubscribePublishLoopback) {
    setenv("BOARDGHOST_NET", "real", 1);
    uint16_t port = pick_port();
    Broker broker(port);

    WiFiClient   wc;
    PubSubClient mqtt(wc);
    mqtt.setServer("127.0.0.1", port);
    mqtt.setSocketTimeout(2);

    std::string  rx_topic;
    std::string  rx_payload;
    mqtt.setCallback([&](char* topic, uint8_t* payload, unsigned int len) {
        rx_topic   = topic;
        rx_payload = std::string((char*)payload, len);
    });

    ASSERT_TRUE(mqtt.connect("sim-client-1"));
    EXPECT_EQ(mqtt.state(), MQTT_CONNECTED);
    EXPECT_TRUE(mqtt.connected());

    EXPECT_TRUE(mqtt.subscribe("phase6/echo"));
    // Brief settle so the broker's SUBSCRIBE handler can record state.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_TRUE(mqtt.publish("phase6/echo", "hello-mqtt"));

    // Spin loop() to drain the looped-back PUBLISH that the broker sends.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (rx_payload.empty() && std::chrono::steady_clock::now() < deadline) {
        mqtt.loop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(rx_topic,   "phase6/echo");
    EXPECT_EQ(rx_payload, "hello-mqtt");

    mqtt.disconnect();
    EXPECT_FALSE(mqtt.connected());
}

TEST(PubSubClient, ConnectFailsWithoutServerSet) {
    WiFiClient wc;
    PubSubClient mqtt(wc);
    // No setServer() called — connect must refuse rather than crash.
    EXPECT_FALSE(mqtt.connect("anything"));
    EXPECT_EQ(mqtt.state(), MQTT_CONNECT_FAILED);
}

TEST(PubSubClientReal, ConnectFailsAgainstClosedPort) {
    setenv("BOARDGHOST_NET", "real", 1);
    WiFiClient wc;
    PubSubClient mqtt(wc);
    mqtt.setServer("127.0.0.1", 1);  // almost always closed
    mqtt.setSocketTimeout(1);
    EXPECT_FALSE(mqtt.connect("noone-home"));
    EXPECT_EQ(mqtt.state(), MQTT_CONNECT_FAILED);
}
