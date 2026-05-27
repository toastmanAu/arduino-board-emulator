use boardghost::codegen::{generate_cmake, CodegenInput};
use boardghost::BoardProfile;
use std::path::PathBuf;
use tempfile::TempDir;

#[test]
fn generates_cmakelists_with_substitutions() {
    let repo = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let board = BoardProfile::load(
        &repo.join("runtime/boards/ili9488_esp32s3_sim.toml")).unwrap();

    let dir = TempDir::new().unwrap();
    let sketch = dir.path().join("preprocessed.cpp");
    std::fs::write(&sketch, "void setup(){} void loop(){}").unwrap();

    let out_dir = dir.path().join("build_root");
    std::fs::create_dir(&out_dir).unwrap();

    let input = CodegenInput {
        sketch_cpp:  sketch.clone(),
        board:       &board,
        runtime_dir: repo.join("runtime"),
        release:     false,
        out_dir:     out_dir.clone(),
    };
    generate_cmake(&input).expect("codegen");

    let cml = std::fs::read_to_string(out_dir.join("CMakeLists.txt")).unwrap();
    assert!(cml.contains("ili9488_esp32s3_sim"));
    assert!(cml.contains("BOARD_DISPLAY_WIDTH=480"));
    assert!(cml.contains("BOARD_DISPLAY_HEIGHT=320"));
    assert!(cml.contains("preprocessed.cpp"));
    assert!(cml.contains("-O0 -g"));
}
