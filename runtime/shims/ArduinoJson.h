#pragma once
// ArduinoJson stub for BoardGhost simulator.
// Provides just enough surface to compile sketches that #include <ArduinoJson.h>
// but don't call any ArduinoJson APIs. Full ArduinoJson semantics are out of
// M2.B scope — add implementations as needed for sketches that use JSON parsing.

#include <stddef.h>
#include <stdint.h>
#include "WString.h"

// Minimal JsonVariant
class JsonVariant {
public:
    bool isNull() const { return true; }
    template<typename T> T as() const { return T{}; }
    template<typename T> operator T() const { return T{}; }
    bool operator==(const char*) const { return false; }
    explicit operator bool() const { return false; }
};

// Minimal JsonObject
class JsonObject {
public:
    bool isNull() const { return true; }
    JsonVariant operator[](const char*) const { return JsonVariant{}; }
    JsonVariant operator[](const String&) const { return JsonVariant{}; }
};

// Minimal JsonArray
class JsonArray {
public:
    bool isNull() const { return true; }
    size_t size() const { return 0; }
    JsonVariant operator[](size_t) const { return JsonVariant{}; }
};

// Minimal JsonDocument (covers both StaticJsonDocument and DynamicJsonDocument usage patterns)
template <size_t N = 0>
class JsonDocument {
public:
    JsonDocument() {}
    JsonObject as()           { return JsonObject{}; }
    JsonArray  asArray()      { return JsonArray{}; }
    JsonVariant operator[](const char*) { return JsonVariant{}; }
    JsonVariant operator[](const String&) { return JsonVariant{}; }
    bool isNull() const { return true; }
    void clear() {}
};

template <size_t N>
using StaticJsonDocument = JsonDocument<N>;

class DynamicJsonDocument : public JsonDocument<0> {
public:
    DynamicJsonDocument(size_t /*capacity*/) {}
};

// deserializeJson — returns an error object that reports "not supported"
struct DeserializationError {
    explicit operator bool() const { return true; }  // truthy = error (there is an error)
    const char* c_str() const { return "ArduinoJson stub — not implemented"; }
    // Allow use in if(err) patterns
    bool code() const { return true; }
};

template<typename T, typename S>
DeserializationError deserializeJson(T& /*doc*/, const S& /*input*/) {
    return DeserializationError{};
}

template<typename T>
DeserializationError deserializeJson(T& /*doc*/, const char* /*input*/) {
    return DeserializationError{};
}

// serializeJson — writes "{}" as placeholder
template<typename T, typename S>
size_t serializeJson(const T& /*doc*/, S& /*output*/) { return 2; }
