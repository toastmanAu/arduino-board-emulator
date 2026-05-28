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

// Real lib uses StreamDebugger as a non-template alias.
// In the sim, SerialClass is used for all serial ports (Serial, Serial1, etc.).
// We need StreamDebugger to accept SerialClass& arguments — use SerialClass here.
// This must match the type used in Serial/Serial1 declarations in Arduino.h.
class SerialClass;  // forward-declare (defined in Arduino.h / sim_serial.cpp)
using StreamDebugger = StreamDebuggerT<SerialClass, SerialClass>;
