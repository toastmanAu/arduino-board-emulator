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
    eprintln!("→ Preprocessing via arduino-cli...");
    let preprocessed = preprocess::preprocess(&discovered.entry, &board.arduino_fqbn_hint)
        .context("Stage 2: preprocess")?;

    // Stage 3: Library allowlist
    // (M1 placeholder — uses #include-line scan rather than --show-properties;
    // upgrade to --show-properties in a follow-up if needed.)
    // For now we skip the allowlist check here and rely on compile-time errors
    // from unshimmed headers. Stage 3's filter_allowed is exercised by unit tests
    // and will be wired into the pipeline once arduino-cli library introspection
    // is integrated.

    // Stage 4: Codegen
    let out_dir = project.join(".boardghost").join(&board.name);
    std::fs::create_dir_all(&out_dir).context("create .boardghost dir")?;
    let _ = codegen::generate_cmake(&codegen::CodegenInput {
        sketch_cpp:  preprocessed,
        board:       &board,
        runtime_dir: runtime_dir.to_path_buf(),
        release,
        out_dir:     out_dir.clone(),
    })?;

    // Stage 5: Compile
    eprintln!("→ Compiling...");
    let out = compile::cmake_configure_and_build(&out_dir)?;
    eprintln!("→ Binary: {:?}", out.binary);

    Ok(BuildResult { binary: out.binary })
}
