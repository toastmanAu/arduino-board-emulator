use std::process::Command;
use std::path::PathBuf;

fn bin() -> PathBuf {
    PathBuf::from(env!("CARGO_BIN_EXE_boardghost"))
}

#[test]
fn list_boards_shows_both_profiles() {
    let repo = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let out = Command::new(bin())
        .arg("list-boards")
        .env("BOARDGHOST_BOARDS", repo.join("runtime/boards"))
        .output()
        .unwrap();
    assert!(out.status.success(), "{:?}", out);
    let s = String::from_utf8_lossy(&out.stdout);
    assert!(s.contains("ili9488_esp32s3_sim"), "got: {s}");
    assert!(s.contains("ssd1306_uno_sim"),    "got: {s}");
}
