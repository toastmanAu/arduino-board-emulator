// Verifies that RunOptions round-trips through serde_json the way the
// Tauri command bridge will deliver it from the JS side, and that defaults
// match what the UI initialises (no env vars accidentally set).

use boardghost_launcher::runner::RunOptions;

#[test]
fn default_options_are_all_empty_or_off() {
    let opts = RunOptions::default();
    assert!(opts.net_mode.is_empty());
    assert!(!opts.auto_touch_cal);
    assert!(opts.sim_touches_screen.is_empty());
    assert_eq!(opts.screenshot_delay_ms, 0);
    assert!(
        !opts.mirror,
        "mirror must default off — LAN exposure is opt-in"
    );
}

#[test]
fn deserialises_from_ui_payload() {
    let json = r#"{
        "net_mode": "real",
        "auto_touch_cal": true,
        "sim_touches_screen": "3000:160,125",
        "screenshot_delay_ms": 8000,
        "mirror": true
    }"#;
    let opts: RunOptions = serde_json::from_str(json).expect("parse");
    assert_eq!(opts.net_mode, "real");
    assert!(opts.auto_touch_cal);
    assert_eq!(opts.sim_touches_screen, "3000:160,125");
    assert_eq!(opts.screenshot_delay_ms, 8000);
    assert!(opts.mirror);
}

#[test]
fn missing_fields_fall_back_to_defaults() {
    // The UI may omit fields when their values are at default — serde
    // should still construct a RunOptions without erroring.
    let json = r#"{ "net_mode": "fake" }"#;
    let opts: RunOptions = serde_json::from_str(json).expect("parse");
    assert_eq!(opts.net_mode, "fake");
    assert!(!opts.auto_touch_cal);
    assert!(opts.sim_touches_screen.is_empty());
    assert_eq!(opts.screenshot_delay_ms, 0);
}
