use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

use crate::{board::BoardProfile, codegen, compile, discover, preprocess};

pub struct BuildResult {
    pub binary: PathBuf,
}

pub fn run_build(
    project: &Path,
    board_name: &str,
    boards_dir: &Path,
    runtime_dir: &Path,
    release: bool,
) -> Result<BuildResult> {
    // Stage 1: Discover
    let discovered = discover::discover(project)
        .context("Stage 1: discover")?;
    eprintln!("→ Sketch: {:?}", discovered.entry);

    // Resolve board profile
    let board = BoardProfile::load_by_name(boards_dir, board_name)
        .context("loading board profile")?;
    eprintln!("→ Board:  {} ({})", board.name, board.description);

    // Stage 2: Preprocess
    // Pass runtime include dirs so BoardGhost display headers (e.g. LGFX_SSD1306_SDL.hpp)
    // are visible to the arduino-cli preprocessor's compiler.
    eprintln!("→ Preprocessing via arduino-cli...");
    let runtime_abs = runtime_dir.canonicalize()
        .with_context(|| format!("canonicalize runtime dir {:?}", runtime_dir))?;
    let extra_includes = vec![
        runtime_abs.join("displays"),
        runtime_abs.join("shims"),
        runtime_abs.join("third_party/LovyanGFX/src"),
        // LVGL v9: sketch can include <lvgl.h> and <sim_lvgl.h>
        runtime_abs.join("third_party/lvgl"),
        runtime_abs.join("include"),
    ];
    let preprocessed = preprocess::preprocess(
        &discovered.entry,
        &board.arduino_fqbn_hint,
        &extra_includes,
    ).context("Stage 2: preprocess")?;

    // Stage 3: Library resolution
    // (M2.C placeholder — lib_resolve::resolve wires headers → installed libs.
    // Full integration with codegen happens in Tasks 6/7.)

    // Stage 4: Codegen
    let out_dir = project.join(".boardghost").join(&board.name);
    std::fs::create_dir_all(&out_dir).context("create .boardghost dir")?;
    // Canonicalize paths so CMakeLists.txt uses absolute paths regardless of CWD.
    let sketch_cpp_abs = preprocessed.canonicalize()
        .with_context(|| format!("canonicalize preprocessed file {:?}", preprocessed))?;
    let _ = codegen::generate_cmake(&codegen::CodegenInput {
        sketch_cpp:  sketch_cpp_abs,
        board:       &board,
        runtime_dir: runtime_abs.clone(),
        release,
        out_dir:     out_dir.clone(),
        libraries:   vec![],
    })?;

    // Stage 5: Compile
    eprintln!("→ Compiling...");
    let out = compile::cmake_configure_and_build(&out_dir)?;
    eprintln!("→ Binary: {:?}", out.binary);

    Ok(BuildResult { binary: out.binary })
}
