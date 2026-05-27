use boardghost::BoardProfile;
use std::path::PathBuf;

#[test]
fn loads_ili9488_profile() {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../runtime/boards/ili9488_esp32s3_sim.toml");
    let profile = BoardProfile::load(&path).expect("load");
    assert_eq!(profile.name, "ili9488_esp32s3_sim");
    assert_eq!(profile.display.controller, "ILI9488");
    assert_eq!(profile.display.width, 480);
    assert_eq!(profile.display.height, 320);
    assert_eq!(profile.display.color_depth, 16);
    assert_eq!(profile.touch.as_ref().map(|t| t.controller.as_str()), Some("xpt2046"));
}

#[test]
fn loads_ssd1306_profile() {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../runtime/boards/ssd1306_uno_sim.toml");
    let profile = BoardProfile::load(&path).expect("load");
    assert_eq!(profile.display.controller, "SSD1306");
    assert_eq!(profile.display.width, 128);
    assert_eq!(profile.display.height, 64);
    assert_eq!(profile.display.color_depth, 1);
    assert!(profile.touch.is_none());
}

#[test]
fn rejects_malformed_profile() {
    let dir = tempfile::tempdir().unwrap();
    let bad = dir.path().join("bad.toml");
    std::fs::write(&bad, "not toml [ at all").unwrap();
    let err = BoardProfile::load(&bad).unwrap_err();
    let msg = format!("{err:#}");
    assert!(msg.contains("malformed"), "got: {msg}");
}
