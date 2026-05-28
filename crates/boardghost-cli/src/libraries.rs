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
    // M2.C — shim wins via header-only override
    "ArduinoWebsockets",
];

pub fn is_shimmed(lib: &str) -> bool {
    ALLOWLIST.iter().any(|a| a.eq_ignore_ascii_case(lib))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_libs_are_shimmed() {
        assert!(is_shimmed("WiFi"));
        assert!(is_shimmed("wifi"));        // case-insensitive
        assert!(is_shimmed("HTTPClient"));
        assert!(is_shimmed("LovyanGFX"));
    }

    #[test]
    fn unknown_libs_are_not_shimmed() {
        assert!(!is_shimmed("ArduinoJson"));
        assert!(!is_shimmed("TimeLib"));
    }
}
