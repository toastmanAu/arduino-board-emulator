use anyhow::{anyhow, Result};
use std::path::PathBuf;
use tera::{Context, Tera};

use crate::BoardProfile;
use crate::lib_resolve::ResolvedLibrary;

const TEMPLATE: &str = include_str!("../templates/CMakeLists.txt.tera");

pub struct CodegenInput<'a> {
    pub sketch_cpp:  PathBuf,
    pub board:       &'a BoardProfile,
    pub runtime_dir: PathBuf,
    pub release:     bool,
    pub out_dir:     PathBuf,
    pub libraries:   Vec<ResolvedLibrary>,
    /// Extra .c/.cpp files living in the sketch directory (e.g. ckb_pos's
    /// qrcode.c). Listed explicitly so the CMake build compiles them; the
    /// arduino-cli `--preprocess` step only emits the merged .ino translation
    /// unit and ignores siblings.
    pub extra_sketch_sources: Vec<PathBuf>,
}

#[derive(serde::Serialize)]
struct LibraryCtx {
    name: String,
    cmake_var_name: String,
    source_dir: String,
    glob_patterns: Vec<String>,
    exclude_dirs: Vec<String>,
    exclude_files: Vec<String>,
    add_include_dirs: Vec<String>,
    shim_only: bool,
}

pub fn generate_cmake(input: &CodegenInput) -> Result<PathBuf> {
    let mut tera = Tera::default();
    tera.add_raw_template("cml", TEMPLATE)?;

    let runtime_dir_str = input.runtime_dir.to_string_lossy().to_string();
    let libs_ctx: Vec<LibraryCtx> = input.libraries.iter().map(|l| {
        let cmake_var_name = format!("LIB_{}_SOURCES",
            l.name.chars().map(|c| if c.is_alphanumeric() { c } else { '_' }).collect::<String>());
        LibraryCtx {
            name: l.name.clone(),
            cmake_var_name,
            source_dir: l.source_dir.to_string_lossy().to_string(),
            glob_patterns: l.glob_patterns.clone(),
            exclude_dirs: l.exclude_dirs.clone(),
            exclude_files: l.exclude_files.clone(),
            add_include_dirs: l.add_include_dirs.iter()
                .map(|s| s.replace("${BOARDGHOST_RUNTIME_DIR}", &runtime_dir_str))
                .collect(),
            shim_only: l.shim_only,
        }
    }).collect();

    let mut ctx = Context::new();
    ctx.insert("sketch_cpp",     &input.sketch_cpp.to_string_lossy());
    ctx.insert("runtime_dir",    &runtime_dir_str);
    ctx.insert("board_name",     &input.board.name);
    ctx.insert("display_width",  &input.board.display.width);
    ctx.insert("display_height", &input.board.display.height);
    ctx.insert("release",        &input.release);
    ctx.insert("libraries",      &libs_ctx);
    let extra_srcs: Vec<String> = input.extra_sketch_sources.iter()
        .map(|p| p.to_string_lossy().to_string()).collect();
    ctx.insert("extra_sketch_sources", &extra_srcs);

    let rendered = tera.render("cml", &ctx)?;
    let out = input.out_dir.join("CMakeLists.txt");
    std::fs::write(&out, rendered)
        .map_err(|e| anyhow!("write {:?}: {e}", out))?;
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::board::{BoardProfile, DisplayConfig};
    use tempfile::tempdir;

    fn fake_board() -> BoardProfile {
        BoardProfile {
            name: "test-board".into(),
            description: "test".into(),
            arduino_fqbn_hint: "esp32:esp32:esp32".into(),
            display: DisplayConfig {
                controller: "ST7789".into(),
                width: 240,
                height: 320,
                rotation: 0,
                bus: "SPI".into(),
                color_depth: 16,
            },
            touch: None,
        }
    }

    #[test]
    fn renders_template_with_no_libraries() {
        let dir = tempdir().unwrap();
        let board = fake_board();
        let out_dir = dir.path().to_path_buf();
        let result = generate_cmake(&CodegenInput {
            sketch_cpp: PathBuf::from("/tmp/sketch.cpp"),
            board: &board,
            runtime_dir: PathBuf::from("/tmp/runtime"),
            release: false,
            out_dir: out_dir.clone(),
            libraries: vec![],
            extra_sketch_sources: vec![],
        }).unwrap();
        let content = std::fs::read_to_string(&result).unwrap();
        assert!(content.contains("/tmp/sketch.cpp"));
        assert!(content.contains("test-board"));
        // No library blocks when libraries is empty.
        assert!(!content.contains("# Library:"));
    }

    #[test]
    fn renders_library_block() {
        let dir = tempdir().unwrap();
        let board = fake_board();
        let lib = ResolvedLibrary {
            name: "ArduinoJson".into(),
            source_dir: PathBuf::from("/tmp/lib/ArduinoJson/src"),
            glob_patterns: vec!["**/*.cpp".into()],
            exclude_dirs: vec!["internal".into()],
            exclude_files: vec![],
            add_include_dirs: vec![],
            shim_only: false,
        };
        let result = generate_cmake(&CodegenInput {
            sketch_cpp: PathBuf::from("/tmp/sketch.cpp"),
            board: &board,
            runtime_dir: PathBuf::from("/tmp/runtime"),
            release: false,
            out_dir: dir.path().to_path_buf(),
            libraries: vec![lib],
            extra_sketch_sources: vec![],
        }).unwrap();
        let content = std::fs::read_to_string(&result).unwrap();
        assert!(content.contains("# Library: ArduinoJson"));
        assert!(content.contains("/tmp/lib/ArduinoJson/src"));
        assert!(content.contains("**/*.cpp"));
    }

    #[test]
    fn substitutes_runtime_dir_in_add_include_dirs() {
        let dir = tempdir().unwrap();
        let board = fake_board();
        let lib = ResolvedLibrary {
            name: "Foo".into(),
            source_dir: PathBuf::from("/tmp/lib/Foo/src"),
            glob_patterns: vec![],
            exclude_dirs: vec![],
            exclude_files: vec![],
            add_include_dirs: vec!["${BOARDGHOST_RUNTIME_DIR}/shims/sim_ws".into()],
            shim_only: true,
        };
        let result = generate_cmake(&CodegenInput {
            sketch_cpp: PathBuf::from("/tmp/sketch.cpp"),
            board: &board,
            runtime_dir: PathBuf::from("/abs/runtime"),
            release: false,
            out_dir: dir.path().to_path_buf(),
            libraries: vec![lib],
            extra_sketch_sources: vec![],
        }).unwrap();
        let content = std::fs::read_to_string(&result).unwrap();
        assert!(content.contains("/abs/runtime/shims/sim_ws"));
        assert!(!content.contains("${BOARDGHOST_RUNTIME_DIR}"));
    }
}
