// M2.C smoke test: exercises auto-discovery of ArduinoJson + the
// ArduinoWebsockets shim in fake-net mode. The sketch is intentionally
// minimal — its job is to compile and run cleanly, not to be useful.
//
// Used as a CI gate for the library auto-discovery pipeline.
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

static WebsocketsClient wsClient;
static int loop_count = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("cryptoticker_smoke: setup");

    WiFi.begin("simulated", "simulated");
    while (WiFi.status() != WL_CONNECTED) {
        delay(50);
    }
    Serial.println("WiFi connected (simulated)");

    JsonDocument doc;
    doc["symbol"] = "BTC";
    doc["price"] = 100000.0;
    String body;
    serializeJson(doc, body);
    Serial.print("JSON: ");
    Serial.println(body);

    wsClient.onMessage([](WebsocketsMessage msg) {
        Serial.print("WS msg: ");
        Serial.println(msg.data().c_str());
    });
    wsClient.connect("ws://example.com/socket");
}

void loop() {
    if (wsClient.available()) {
        wsClient.poll();
    }
    if (++loop_count > 200) {
        Serial.println("cryptoticker_smoke: done");
        exit(0);
    }
    delay(10);
}
