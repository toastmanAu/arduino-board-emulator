use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

use crate::{
    arduino_libs, board::BoardProfile, codegen, compile, discover, include_scan,
    lib_resolve, libraries, preprocess,
};

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
    // Stage 1: Discover sketch
    let discovered = discover::discover(project)
        .context("Stage 1: discover")?;
    eprintln!("→ Sketch: {:?}", discovered.entry);

    let board = BoardProfile::load_by_name(boards_dir, board_name)
        .context("loading board profile")?;
    eprintln!("→ Board:  {} ({})", board.name, board.description);

    // Stage 2a: Scan sketch for #include directives BEFORE preprocess so we can
    // tell arduino-cli where the user's libraries live (otherwise preprocess
    // fails on TimeLib.h-style "not found" errors).
    let headers = include_scan::scan_file(&discovered.entry)
        .with_context(|| format!("scan {:?}", discovered.entry))?;
    eprintln!("→ Headers: {} includes scanned", headers.len());

    // Stage 2a.1: drop headers that exist as local files in the sketch dir.
    // The arduino-cli preprocessor finds them automatically — they're not
    // libraries and shouldn't fail discovery.
    let sketch_dir = discovered.entry.parent().unwrap_or(Path::new("."));
    let library_headers: std::collections::BTreeSet<String> = headers.into_iter()
        .filter(|h| !sketch_dir.join(h).exists())
        .collect();

    // Stage 2b: Resolve headers → installed libraries (skipping shimmed ones).
    let installed = arduino_libs::list_installed()
        .context("arduino-cli lib list failed; install arduino-cli or check it's in PATH")?;
    let overrides_dir = runtime_dir.join("library_overrides");
    let resolved = lib_resolve::resolve(
        &library_headers,
        &installed,
        &overrides_dir,
        libraries::ALLOWLIST,
    ).context("Stage 2b: library resolve")?;
    if !resolved.is_empty() {
        eprintln!("→ Auto-discovered libraries:");
        for r in &resolved {
            eprintln!("    • {} ({})", r.name, r.source_dir.display());
        }
    }

    // Stage 2c: Preprocess via arduino-cli, with both runtime shim dirs AND
    // discovered library include dirs.
    eprintln!("→ Preprocessing via arduino-cli...");
    let runtime_abs = runtime_dir.canonicalize()
        .with_context(|| format!("canonicalize runtime dir {:?}", runtime_dir))?;
    let mut extra_includes = vec![
        runtime_abs.join("displays"),
        runtime_abs.join("shims"),
        runtime_abs.join("third_party/LovyanGFX/src"),
        runtime_abs.join("third_party/lvgl"),
        runtime_abs.join("include"),
    ];
    for lib in &resolved {
        extra_includes.push(lib.source_dir.clone());
        // Also add any post-substitution add_include_dirs (these reach absolute paths).
        for inc in &lib.add_include_dirs {
            let path = inc.replace("${BOARDGHOST_RUNTIME_DIR}", &runtime_abs.to_string_lossy());
            extra_includes.push(PathBuf::from(path));
        }
    }
    let preprocessed = preprocess::preprocess(
        &discovered.entry,
        &board.arduino_fqbn_hint,
        &extra_includes,
    ).context("Stage 2c: preprocess")?;

    // Stage 4: Codegen
    let out_dir = project.join(".boardghost").join(&board.name);
    std::fs::create_dir_all(&out_dir).context("create .boardghost dir")?;
    let sketch_cpp_abs = preprocessed.canonicalize()
        .with_context(|| format!("canonicalize preprocessed file {:?}", preprocessed))?;
    let _ = codegen::generate_cmake(&codegen::CodegenInput {
        sketch_cpp:  sketch_cpp_abs,
        board:       &board,
        runtime_dir: runtime_abs.clone(),
        release,
        out_dir:     out_dir.clone(),
        libraries:   resolved,
    })?;

    // Stage 5: Compile
    eprintln!("→ Compiling...");
    let out = compile::cmake_configure_and_build(&out_dir)?;
    eprintln!("→ Binary: {:?}", out.binary);

    Ok(BuildResult { binary: out.binary })
}
