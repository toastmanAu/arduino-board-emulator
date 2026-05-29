use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

use crate::{
    arduino_libs, board::BoardProfile, codegen, compile, discover, include_scan,
    lgfx_codemod, lib_resolve, libraries, preprocess, sketch_mirror,
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

    // Stage 1.5: LGFX codemod. If the sketch has a hardware LGFX setup file
    // (e.g. cryptoTickerv3's lvgfx_setup.h), mirror the sketch dir into our
    // build cache with the setup file replaced by a Panel_sdl wrapper. The
    // preprocess + compile stages then operate on the mirror so the user's
    // sketch dir is never touched on disk.
    let original_sketch_dir = discovered.entry.parent()
        .unwrap_or(Path::new(".")).to_path_buf();
    let out_root = project.join(".boardghost").join(&board.name);
    std::fs::create_dir_all(&out_root).context("create .boardghost dir")?;

    let codemod = lgfx_codemod::try_apply(&original_sketch_dir)?;
    let (effective_sketch_entry, effective_sketch_dir) = if let Some(ref report) = codemod {
        eprintln!(
            "→ LGFX codemod: rewriting {} → Panel_sdl {}x{} (rotation {})",
            report.rewritten_rel_path.display(),
            report.parsed.panel_width,
            report.parsed.panel_height,
            report.parsed.offset_rotation,
        );
        // arduino-cli requires the sketch's main .ino file to share its name
        // with the parent dir, so name the mirror after the original sketch
        // dir (e.g. `cryptoTickerv3/`), nested under a `sketch_src/` namespace
        // to keep the build cache tidy.
        let original_dir_name = original_sketch_dir.file_name()
            .ok_or_else(|| anyhow::anyhow!(
                "sketch dir has no name: {:?}", original_sketch_dir
            ))?;
        let mirror_dir = out_root.join("sketch_src").join(original_dir_name);
        sketch_mirror::mirror(
            &original_sketch_dir,
            &mirror_dir,
            &[(report.rewritten_rel_path.clone(), report.sim_contents.clone())],
        )?;
        let new_entry = mirror_dir.join(
            discovered.entry.file_name().expect("sketch entry has a name")
        );
        (new_entry, mirror_dir)
    } else {
        (discovered.entry.clone(), original_sketch_dir.clone())
    };

    // Stage 2a: Scan sketch for #include directives BEFORE preprocess so we can
    // tell arduino-cli where the user's libraries live.
    let headers = include_scan::scan_file(&effective_sketch_entry)
        .with_context(|| format!("scan {:?}", effective_sketch_entry))?;
    eprintln!("→ Headers: {} includes scanned", headers.len());

    // Stage 2a.1: drop headers findable on disk in the (effective) sketch dir
    // or a BoardGhost runtime helper dir. runtime/shims/ is excluded — see
    // include_scan::filter_local_headers for the rationale.
    let sketch_dir_for_filter: &Path = &effective_sketch_dir;
    let displays_dir = runtime_dir.join("displays");
    let include_dir = runtime_dir.join("include");
    let local_search_dirs: [&Path; 3] = [
        sketch_dir_for_filter,
        &displays_dir,
        &include_dir,
    ];
    let library_headers = include_scan::filter_local_headers(headers, &local_search_dirs);

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
        for inc in &lib.add_include_dirs {
            let path = inc.replace("${BOARDGHOST_RUNTIME_DIR}", &runtime_abs.to_string_lossy());
            extra_includes.push(PathBuf::from(path));
        }
    }
    let preprocessed = preprocess::preprocess(
        &effective_sketch_entry,
        &board.arduino_fqbn_hint,
        &extra_includes,
    ).context("Stage 2c: preprocess")?;

    // Stage 4: Codegen (uses out_root established in Stage 1.5).
    let sketch_cpp_abs = preprocessed.canonicalize()
        .with_context(|| format!("canonicalize preprocessed file {:?}", preprocessed))?;
    let _ = codegen::generate_cmake(&codegen::CodegenInput {
        sketch_cpp:  sketch_cpp_abs,
        board:       &board,
        runtime_dir: runtime_abs.clone(),
        release,
        out_dir:     out_root.clone(),
        libraries:   resolved,
    })?;

    // Stage 5: Compile
    eprintln!("→ Compiling...");
    let out = compile::cmake_configure_and_build(&out_root)?;
    eprintln!("→ Binary: {:?}", out.binary);

    Ok(BuildResult { binary: out.binary })
}
