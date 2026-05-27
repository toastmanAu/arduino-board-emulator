use boardghost::preprocess::preprocess;
use tempfile::TempDir;

// This test is gated: it requires arduino-cli to be installed on PATH.
// We skip when not present rather than fail, so CI environments without
// arduino-cli can still build the crate.
fn arduino_cli_available() -> bool {
    std::process::Command::new("arduino-cli").arg("version").output()
        .map(|o| o.status.success()).unwrap_or(false)
}

#[test]
fn preprocesses_a_minimal_sketch() {
    if !arduino_cli_available() {
        eprintln!("skipping: arduino-cli not installed");
        return;
    }
    let dir = TempDir::new().unwrap();
    let sketch_dir = dir.path().join("minimal");
    std::fs::create_dir(&sketch_dir).unwrap();
    let sketch = sketch_dir.join("minimal.ino");
    std::fs::write(&sketch,
        "void setup() {}\nvoid loop() {}\n").unwrap();

    let out = preprocess(&sketch, "esp32:esp32:esp32s3").expect("preprocess");
    let text = std::fs::read_to_string(&out).unwrap();
    assert!(text.contains("void setup"));
    assert!(text.contains("void loop"));
}
