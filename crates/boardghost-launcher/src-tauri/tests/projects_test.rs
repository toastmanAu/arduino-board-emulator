use boardghost_launcher::projects::{ProjectStore, ProjectEntry};
use std::path::PathBuf;
use tempfile::TempDir;

#[test]
fn empty_store_has_no_projects() {
    let dir = TempDir::new().unwrap();
    let path = dir.path().join("projects.json");
    let store = ProjectStore::load(&path).expect("load missing file → empty");
    assert!(store.recent().is_empty());
}

#[test]
fn add_and_persist_roundtrip() {
    let dir = TempDir::new().unwrap();
    let path = dir.path().join("projects.json");
    let mut store = ProjectStore::load(&path).unwrap();

    store.add_or_update(ProjectEntry {
        path: PathBuf::from("/home/u/code/demo"),
        board: "ili9488_esp32s3_sim".to_string(),
        last_used: 1716840000,
    });
    store.save(&path).unwrap();

    let reloaded = ProjectStore::load(&path).unwrap();
    assert_eq!(reloaded.recent().len(), 1);
    assert_eq!(reloaded.recent()[0].path, PathBuf::from("/home/u/code/demo"));
    assert_eq!(reloaded.recent()[0].board, "ili9488_esp32s3_sim");
}

#[test]
fn add_or_update_overwrites_same_path() {
    let dir = TempDir::new().unwrap();
    let path = dir.path().join("projects.json");
    let mut store = ProjectStore::load(&path).unwrap();

    store.add_or_update(ProjectEntry {
        path: PathBuf::from("/p"),
        board: "a".into(),
        last_used: 1,
    });
    store.add_or_update(ProjectEntry {
        path: PathBuf::from("/p"),
        board: "b".into(),
        last_used: 2,
    });

    assert_eq!(store.recent().len(), 1);
    assert_eq!(store.recent()[0].board, "b");
    assert_eq!(store.recent()[0].last_used, 2);
}

#[test]
fn recent_sorted_by_last_used_desc() {
    let dir = TempDir::new().unwrap();
    let path = dir.path().join("projects.json");
    let mut store = ProjectStore::load(&path).unwrap();

    store.add_or_update(ProjectEntry { path: "/a".into(), board: "x".into(), last_used: 100 });
    store.add_or_update(ProjectEntry { path: "/b".into(), board: "x".into(), last_used: 300 });
    store.add_or_update(ProjectEntry { path: "/c".into(), board: "x".into(), last_used: 200 });

    let r = store.recent();
    assert_eq!(r[0].path.to_str().unwrap(), "/b");
    assert_eq!(r[1].path.to_str().unwrap(), "/c");
    assert_eq!(r[2].path.to_str().unwrap(), "/a");
}
