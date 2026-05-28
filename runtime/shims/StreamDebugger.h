#pragma once
#include <stdint.h>
#include <stddef.h>

// StreamDebugger wraps a Stream and a debug-output Stream. In sim we just
// satisfy the constructor signature; reads/writes are no-ops.
template <typename A, typename B>
class StreamDebuggerT {
public:
    StreamDebuggerT(A& /*upstream*/, B& /*debug*/) {}
    int  available()                  { return 0; }
    int  read()                       { return -1; }
    size_t write(uint8_t /*b*/)       { return 1; }
    size_t write(const uint8_t* /*b*/, size_t n) { return n; }
    void flush()                      {}
};

// Real lib uses StreamDebugger as a non-template. Provide a forwarding alias.
class HardwareSerial;  // forward-declare; ESP32 core defines this.
using StreamDebugger = StreamDebuggerT<HardwareSerial, HardwareSerial>;
