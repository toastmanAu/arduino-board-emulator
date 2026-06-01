#pragma once
#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <memory>
#include <string>

#include "WiFiClient.h"

// PubSubClient shim — Nick O'Leary's MQTT 3.1.1 client API.
//
// Wire protocol implemented directly in sim_mqtt.cpp on top of WiFiClient
// (or WiFiClientSecure for mqtts://). QoS 0 only — the vast majority of
// hobby/IoT sketches publish at QoS 0; adding 1/2 would require packet-ID
// tracking + retransmission state that I'll layer in if needed.
//
// Threading: matches the real PubSubClient. connect/publish/subscribe block
// briefly while the packet round-trips; loop() drains incoming PUBLISH +
// keepalive. Sketches that follow the canonical "call loop() in loop()"
// idiom work as-is.

// State constants — mirror PubSubClient's enum so sketches checking
// client.state() against MQTT_CONNECTED etc. see the same values.
#define MQTT_CONNECTION_TIMEOUT     -4
#define MQTT_CONNECTION_LOST        -3
#define MQTT_CONNECT_FAILED         -2
#define MQTT_DISCONNECTED           -1
#define MQTT_CONNECTED               0
#define MQTT_CONNECT_BAD_PROTOCOL    1
#define MQTT_CONNECT_BAD_CLIENT_ID   2
#define MQTT_CONNECT_UNAVAILABLE     3
#define MQTT_CONNECT_BAD_CREDENTIALS 4
#define MQTT_CONNECT_UNAUTHORIZED    5

namespace boardghost_internal { class MqttImpl; }

class PubSubClient {
public:
    // Callback signature matches PubSubClient: (topic, payload, length).
    using Callback = std::function<void(char* topic, uint8_t* payload, unsigned int length)>;

    PubSubClient();
    explicit PubSubClient(WiFiClient& client);
    PubSubClient(const char* domain, uint16_t port, WiFiClient& client);
    PubSubClient(const char* domain, uint16_t port, Callback cb, WiFiClient& client);
    ~PubSubClient();

    PubSubClient(const PubSubClient&) = delete;
    PubSubClient& operator=(const PubSubClient&) = delete;

    PubSubClient& setServer(const char* domain, uint16_t port);
    PubSubClient& setCallback(Callback cb);
    PubSubClient& setClient(WiFiClient& client);
    PubSubClient& setBufferSize(uint16_t size);
    PubSubClient& setKeepAlive(uint16_t seconds);
    PubSubClient& setSocketTimeout(uint16_t seconds);

    bool connect(const char* id);
    bool connect(const char* id, const char* user, const char* pass);
    void disconnect();

    bool publish(const char* topic, const char* payload);
    bool publish(const char* topic, const char* payload, bool retained);
    bool publish(const char* topic, const uint8_t* payload, unsigned int plength);
    bool publish(const char* topic, const uint8_t* payload, unsigned int plength, bool retained);

    bool subscribe(const char* topic);
    bool subscribe(const char* topic, uint8_t qos);
    bool unsubscribe(const char* topic);

    bool loop();
    bool connected();
    int  state();

private:
    std::unique_ptr<boardghost_internal::MqttImpl> impl_;
};
