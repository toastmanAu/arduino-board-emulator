#pragma once
// ESPAsyncWebServer sketches conventionally #include <AsyncTCP.h> before
// <ESPAsyncWebServer.h>. The async TCP layer is internal to the cpp-httplib
// backing in the sim, so this header only needs to exist for the include to
// resolve. Intentionally empty.
