use crate::error::BoardGhostError;

pub const ALLOWLIST: &[&str] = &[
    // M1 — graphics
    "LovyanGFX",
    "lvgl",
    "Adafruit_GFX",
    // M2.B — IoT stubs (these names match arduino-cli's library reports)
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

pub fn filter_allowed(libs: &[String]) -> Result<Vec<String>, BoardGhostError> {
    for lib in libs {
        if !is_allowed(lib) {
            return Err(BoardGhostError::UnsupportedLibrary { name: lib.clone() });
        }
    }
    Ok(libs.to_vec())
}

pub fn is_allowed(lib: &str) -> bool {
    ALLOWLIST.iter().any(|a| a.eq_ignore_ascii_case(lib))
}
