// PubSubClient backing — MQTT 3.1.1 wire protocol over WiFiClient.
//
// QoS 0 only for now. Implements the packet types ckb_pos-class sketches
// actually use: CONNECT/CONNACK, PUBLISH (in + out), SUBSCRIBE/SUBACK,
// UNSUBSCRIBE/UNSUBACK, PINGREQ/PINGRESP, DISCONNECT. No retained-message
// publish path on the broker side, no will, no QoS 1/2 retransmission.
//
// Threading mirrors PubSubClient: connect/publish/subscribe send + wait
// synchronously (with a configurable socket timeout); loop() polls for
// incoming bytes and dispatches PUBLISH to the user callback on the calling
// thread. Sketches that follow the "loop()-in-loop()" pattern get correct
// single-threaded semantics for free.
//
// The wire protocol notes that follow are stripped to what we actually use;
// see the OASIS MQTT 3.1.1 spec for full details
// (https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html).

#include "PubSubClient.h"
#include "Arduino.h"   // for millis()

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace boardghost_internal {

// MQTT control packet types (high nibble of fixed header byte 1).
enum : uint8_t {
    PKT_CONNECT     = 0x10,
    PKT_CONNACK     = 0x20,
    PKT_PUBLISH     = 0x30,
    PKT_PUBACK      = 0x40,
    PKT_SUBSCRIBE   = 0x82,   // bit-0 set: reserved=2 per spec
    PKT_SUBACK      = 0x90,
    PKT_UNSUBSCRIBE = 0xA2,
    PKT_UNSUBACK    = 0xB0,
    PKT_PINGREQ     = 0xC0,
    PKT_PINGRESP    = 0xD0,
    PKT_DISCONNECT  = 0xE0,
};

class MqttImpl {
public:
    MqttImpl() = default;

    void set_client(WiFiClient* c) { client_ = c; }
    void set_server(std::string host, uint16_t port) {
        host_ = std::move(host); port_ = port;
    }
    void set_callback(PubSubClient::Callback cb) { callback_ = std::move(cb); }
    void set_buffer_size(uint16_t sz) { (void)sz; /* dynamic buffer */ }
    void set_keepalive(uint16_t sec) { keepalive_sec_ = sec; }
    void set_socket_timeout(uint16_t sec) { socket_timeout_sec_ = sec; }

    bool connect(const std::string& id, const char* user, const char* pass) {
        if (!client_) { state_ = MQTT_CONNECT_FAILED; return false; }
        if (host_.empty() || port_ == 0) { state_ = MQTT_CONNECT_FAILED; return false; }

        if (!client_->connected()) {
            if (!client_->connect(host_.c_str(), port_)) {
                state_ = MQTT_CONNECT_FAILED;
                return false;
            }
        }
        client_->setTimeout(socket_timeout_sec_ * 1000);

        // CONNECT payload: protocol name "MQTT", level 4 (3.1.1), connect
        // flags (clean session + optional user/pass), keepalive, client id.
        std::vector<uint8_t> payload;
        push_string(payload, "MQTT");
        payload.push_back(4);  // protocol level: MQTT 3.1.1

        uint8_t flags = 0x02;  // clean session
        if (user) flags |= 0x80;
        if (pass) flags |= 0x40;
        payload.push_back(flags);

        push_be16(payload, keepalive_sec_);
        push_string(payload, id);
        if (user) push_string(payload, user);
        if (pass) push_string(payload, pass);

        if (!send_packet(PKT_CONNECT, payload)) {
            state_ = MQTT_CONNECT_FAILED;
            return false;
        }
        // Wait for CONNACK.
        std::vector<uint8_t> resp;
        uint8_t type = 0;
        if (!recv_packet(type, resp, socket_timeout_sec_ * 1000)) {
            state_ = MQTT_CONNECTION_TIMEOUT;
            return false;
        }
        if (type != PKT_CONNACK || resp.size() < 2) {
            state_ = MQTT_CONNECT_FAILED;
            return false;
        }
        // resp[0] = ack-flags, resp[1] = return code (0 = accepted).
        int rc = resp[1];
        if (rc != 0) {
            // Map to MQTT_CONNECT_* (rc values align 1..5).
            state_ = rc;
            client_->stop();
            return false;
        }
        state_         = MQTT_CONNECTED;
        last_activity_ = millis();
        return true;
    }

    void disconnect() {
        if (client_ && client_->connected()) {
            send_packet(PKT_DISCONNECT, {});
            client_->stop();
        }
        state_ = MQTT_DISCONNECTED;
    }

    bool publish(const std::string& topic, const uint8_t* payload,
                 size_t len, bool retained) {
        if (state_ != MQTT_CONNECTED) return false;
        std::vector<uint8_t> body;
        push_string(body, topic);
        if (payload && len) body.insert(body.end(), payload, payload + len);
        uint8_t header = PKT_PUBLISH | (retained ? 0x01 : 0x00);
        if (!send_packet(header, body)) {
            state_ = MQTT_CONNECTION_LOST;
            return false;
        }
        last_activity_ = millis();
        return true;
    }

    bool subscribe(const std::string& topic, uint8_t qos) {
        if (state_ != MQTT_CONNECTED) return false;
        std::vector<uint8_t> body;
        uint16_t pid = ++next_packet_id_;
        push_be16(body, pid);
        push_string(body, topic);
        body.push_back(qos);
        if (!send_packet(PKT_SUBSCRIBE, body)) {
            state_ = MQTT_CONNECTION_LOST;
            return false;
        }
        last_activity_ = millis();
        return true;
    }

    bool unsubscribe(const std::string& topic) {
        if (state_ != MQTT_CONNECTED) return false;
        std::vector<uint8_t> body;
        uint16_t pid = ++next_packet_id_;
        push_be16(body, pid);
        push_string(body, topic);
        if (!send_packet(PKT_UNSUBSCRIBE, body)) {
            state_ = MQTT_CONNECTION_LOST;
            return false;
        }
        last_activity_ = millis();
        return true;
    }

    bool loop() {
        if (state_ != MQTT_CONNECTED) return false;
        if (!client_ || !client_->connected()) {
            state_ = MQTT_CONNECTION_LOST;
            return false;
        }
        // Drain any complete inbound packets without blocking.
        while (client_->available() > 0) {
            uint8_t type = 0;
            std::vector<uint8_t> body;
            // Use a tiny timeout — bytes are already buffered, so this is
            // essentially non-blocking; we just have to give recv_packet a
            // budget to assemble the variable-length header.
            if (!recv_packet(type, body, 200)) break;
            dispatch_inbound(type, body);
        }
        // Keepalive: send PINGREQ if we're past 80% of the keepalive window.
        if (keepalive_sec_ > 0 &&
            (millis() - last_activity_) > (uint32_t)keepalive_sec_ * 800) {
            send_packet(PKT_PINGREQ, {});
            last_activity_ = millis();
        }
        return true;
    }

    bool connected() {
        if (state_ != MQTT_CONNECTED) return false;
        if (!client_ || !client_->connected()) {
            state_ = MQTT_CONNECTION_LOST;
            return false;
        }
        return true;
    }

    int state() const { return state_; }

private:
    // --- packet framing ---
    static void push_be16(std::vector<uint8_t>& v, uint16_t x) {
        v.push_back((uint8_t)(x >> 8));
        v.push_back((uint8_t)(x & 0xFF));
    }
    static void push_string(std::vector<uint8_t>& v, const std::string& s) {
        push_be16(v, (uint16_t)s.size());
        v.insert(v.end(), s.begin(), s.end());
    }

    bool send_packet(uint8_t type, const std::vector<uint8_t>& payload) {
        if (!client_) return false;
        std::vector<uint8_t> packet;
        packet.reserve(payload.size() + 5);
        packet.push_back(type);
        // Remaining length: variable-byte encoding per spec section 2.2.3.
        size_t remaining = payload.size();
        do {
            uint8_t byte = remaining & 0x7F;
            remaining >>= 7;
            if (remaining > 0) byte |= 0x80;
            packet.push_back(byte);
        } while (remaining > 0);
        packet.insert(packet.end(), payload.begin(), payload.end());
        size_t sent = client_->write(packet.data(), packet.size());
        return sent == packet.size();
    }

    bool recv_packet(uint8_t& type, std::vector<uint8_t>& out, uint32_t deadline_ms) {
        if (!client_) return false;
        uint32_t start = millis();
        // Read fixed header byte 1 (type + flags).
        uint8_t hdr = 0;
        if (!read_exact(&hdr, 1, deadline_ms - (millis() - start))) return false;
        type = hdr & 0xF0;
        if (type == PKT_SUBSCRIBE || type == PKT_UNSUBSCRIBE) {
            // For sub/unsub the low nibble is reserved=2; the high-nibble
            // mask above stripped it, so type compares cleanly above. Other
            // packets that carry flags in the low nibble (PUBLISH retain/dup)
            // need the raw byte preserved — keep the full byte for callers
            // that care via |0x01 etc.
        }
        // Read remaining-length (variable byte int, 1-4 bytes).
        size_t remaining = 0;
        size_t multiplier = 1;
        for (int i = 0; i < 4; ++i) {
            uint8_t b = 0;
            if (!read_exact(&b, 1, deadline_ms - (millis() - start))) return false;
            remaining += (b & 0x7F) * multiplier;
            if ((b & 0x80) == 0) break;
            multiplier *= 128;
            if (multiplier > 128 * 128 * 128) return false;  // malformed
        }
        out.assign(remaining, 0);
        if (remaining == 0) return true;
        return read_exact(out.data(), remaining, deadline_ms - (millis() - start));
    }

    bool read_exact(uint8_t* buf, size_t n, uint32_t deadline_ms) {
        if (!client_) return false;
        size_t got = 0;
        uint32_t start = millis();
        while (got < n) {
            if (millis() - start > deadline_ms) return false;
            int r = client_->read(buf + got, n - got);
            if (r <= 0) {
                // No data right now; brief yield so we don't busy-loop.
                if (client_->available() == 0) {
                    // small sleep via millis polling — we don't have a
                    // delay helper at this layer; OS-level read blocks too.
                    for (int i = 0; i < 10 && client_->available() == 0; ++i) {
                        if (millis() - start > deadline_ms) return false;
                    }
                    continue;
                }
                continue;
            }
            got += (size_t)r;
        }
        return true;
    }

    void dispatch_inbound(uint8_t type, std::vector<uint8_t>& body) {
        uint8_t top = type & 0xF0;
        if (top == PKT_PUBLISH) {
            // PUBLISH payload layout: 2-byte topic length + topic + (packet
            // id if QoS>0 — we only support QoS 0 inbound) + body.
            if (body.size() < 2) return;
            size_t tlen = ((size_t)body[0] << 8) | body[1];
            if (body.size() < 2 + tlen) return;
            std::string topic((char*)&body[2], tlen);
            size_t pl_off = 2 + tlen;
            size_t pl_len = body.size() - pl_off;
            if (callback_) {
                // PubSubClient passes a writable topic + payload; copy into
                // a temp buffer so user code can stomp on it without
                // corrupting our recv buffer.
                std::vector<char> tcopy(topic.begin(), topic.end());
                tcopy.push_back(0);
                callback_(tcopy.data(), pl_len ? &body[pl_off] : nullptr,
                          (unsigned int)pl_len);
            }
        } else if (top == PKT_PINGRESP) {
            // No-op; presence confirms broker is alive.
        } else if (top == PKT_SUBACK || top == PKT_UNSUBACK || top == PKT_PUBACK) {
            // Ignore — we don't wait for these in synchronous calls because
            // QoS 0 doesn't need them. The broker may still send them.
        }
    }

    WiFiClient*            client_              = nullptr;
    std::string            host_;
    uint16_t               port_                = 0;
    PubSubClient::Callback callback_;
    int                    state_               = MQTT_DISCONNECTED;
    uint16_t               keepalive_sec_       = 15;
    uint16_t               socket_timeout_sec_  = 15;
    uint16_t               next_packet_id_      = 0;
    uint32_t               last_activity_       = 0;
};

}  // namespace boardghost_internal

namespace bgi = boardghost_internal;

PubSubClient::PubSubClient() : impl_(std::make_unique<bgi::MqttImpl>()) {}
PubSubClient::PubSubClient(WiFiClient& c) : PubSubClient() { impl_->set_client(&c); }
PubSubClient::PubSubClient(const char* d, uint16_t p, WiFiClient& c) : PubSubClient(c) {
    impl_->set_server(d ? d : "", p);
}
PubSubClient::PubSubClient(const char* d, uint16_t p, Callback cb, WiFiClient& c) : PubSubClient(c) {
    impl_->set_server(d ? d : "", p);
    impl_->set_callback(std::move(cb));
}
PubSubClient::~PubSubClient() = default;

PubSubClient& PubSubClient::setServer(const char* d, uint16_t p) {
    impl_->set_server(d ? d : "", p);
    return *this;
}
PubSubClient& PubSubClient::setCallback(Callback cb) {
    impl_->set_callback(std::move(cb));
    return *this;
}
PubSubClient& PubSubClient::setClient(WiFiClient& c) {
    impl_->set_client(&c);
    return *this;
}
PubSubClient& PubSubClient::setBufferSize(uint16_t s) {
    impl_->set_buffer_size(s); return *this;
}
PubSubClient& PubSubClient::setKeepAlive(uint16_t s) {
    impl_->set_keepalive(s); return *this;
}
PubSubClient& PubSubClient::setSocketTimeout(uint16_t s) {
    impl_->set_socket_timeout(s); return *this;
}

bool PubSubClient::connect(const char* id) {
    return impl_->connect(id ? id : "", nullptr, nullptr);
}
bool PubSubClient::connect(const char* id, const char* u, const char* p) {
    return impl_->connect(id ? id : "", u, p);
}
void PubSubClient::disconnect() { impl_->disconnect(); }

bool PubSubClient::publish(const char* t, const char* p) {
    return publish(t, (const uint8_t*)p, p ? std::strlen(p) : 0, false);
}
bool PubSubClient::publish(const char* t, const char* p, bool r) {
    return publish(t, (const uint8_t*)p, p ? std::strlen(p) : 0, r);
}
bool PubSubClient::publish(const char* t, const uint8_t* p, unsigned int n) {
    return publish(t, p, n, false);
}
bool PubSubClient::publish(const char* t, const uint8_t* p, unsigned int n, bool r) {
    return impl_->publish(t ? t : "", p, n, r);
}

bool PubSubClient::subscribe(const char* t)              { return impl_->subscribe(t ? t : "", 0); }
bool PubSubClient::subscribe(const char* t, uint8_t qos) { return impl_->subscribe(t ? t : "", qos); }
bool PubSubClient::unsubscribe(const char* t)            { return impl_->unsubscribe(t ? t : ""); }

bool PubSubClient::loop()      { return impl_->loop(); }
bool PubSubClient::connected() { return impl_->connected(); }
int  PubSubClient::state()     { return impl_->state(); }
