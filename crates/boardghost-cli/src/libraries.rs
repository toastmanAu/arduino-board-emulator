/// Libraries that BoardGhost ships header-only shims for (under runtime/shims/).
/// When a sketch's `#include` resolves to one of these, we use our shim instead
/// of the arduino-cli-resolved library — `runtime/shims/` comes first in the
/// preprocess include path.
///
/// Match is case-insensitive against the library name (the stem of the header,
/// e.g. "WiFi.h" → "WiFi").
pub const ALLOWLIST: &[&str] = &[
    // M1 — graphics
    "LovyanGFX",
    "lvgl",
    "Adafruit_GFX",
    // M2.B — IoT stubs
    "WiFi",
    "WiFiClient",
    "WiFiClientSecure",
    "HTTPClient",
    "EEPROM",
    "FS",
    "SPIFFS",
    "LittleFS",
    "SD",
    "TinyGSM",
    "StreamDebugger",
];

pub fn is_shimmed(lib: &str) -> bool {
    ALLOWLIST.iter().any(|a| a.eq_ignore_ascii_case(lib))
}
