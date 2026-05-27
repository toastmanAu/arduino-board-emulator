use boardghost::discover::discover;
use std::path::PathBuf;
use tempfile::TempDir;

#[test]
fn finds_sketch_ino() {
    let dir = TempDir::new().unwrap();
    let sketch_dir = dir.path().join("sketch");
    std::fs::create_dir(&sketch_dir).unwrap();
    let sketch = sketch_dir.join("sketch.ino");
    std::fs::write(&sketch, "void setup(){} void loop(){}").unwrap();

    let d = discover(dir.path()).expect("discover");
    assert_eq!(d.entry, sketch);
}

#[test]
fn finds_top_level_ino() {
    let dir = TempDir::new().unwrap();
    let sketch = dir.path().join("foo.ino");
    std::fs::write(&sketch, "void setup(){} void loop(){}").unwrap();
    let d = discover(dir.path()).expect("discover");
    assert_eq!(d.entry, sketch);
}

#[test]
fn errors_when_no_sketch() {
    let dir = TempDir::new().unwrap();
    let err = discover(dir.path()).unwrap_err();
    assert!(format!("{err:#}").contains("No sketch found"));
}

#[test]
fn errors_when_dir_missing() {
    let err = discover(&PathBuf::from("/no/such/dir")).unwrap_err();
    assert!(format!("{err:#}").contains("does not exist"));
}
