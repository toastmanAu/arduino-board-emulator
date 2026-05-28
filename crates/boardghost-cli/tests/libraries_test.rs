use boardghost::libraries::{filter_allowed, ALLOWLIST};

#[test]
fn allows_known_libraries() {
    let input = vec![
        "LovyanGFX".to_string(),
        "lvgl".to_string(),
        "Adafruit_GFX".to_string(),
    ];
    let r = filter_allowed(&input).expect("ok");
    assert_eq!(r, input);
}

#[test]
fn rejects_unknown_library() {
    let input = vec!["LovyanGFX".to_string(), "UnknownLib".to_string()];
    let err = filter_allowed(&input).unwrap_err();
    let msg = format!("{err:#}");
    assert!(msg.contains("UnknownLib"), "msg: {msg}");
    assert!(msg.contains("BOARDGHOST_SIM"), "msg: {msg}");
}

#[test]
fn allowlist_is_case_insensitive() {
    let input = vec!["lovyangfx".to_string(), "LVGL".to_string()];
    filter_allowed(&input).expect("ok");
}

#[test]
fn allowlist_contains_expected_entries() {
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("LovyanGFX")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("lvgl")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("Adafruit_GFX")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("WiFi")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("HTTPClient")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("EEPROM")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("SPIFFS")));
    assert!(ALLOWLIST.iter().any(|s| s.eq_ignore_ascii_case("TinyGSM")));
}
