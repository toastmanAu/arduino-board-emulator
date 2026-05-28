use anyhow::Result;
use clap::Parser;
use std::path::PathBuf;

use boardghost::{cli::{Cli, Command, BuildProfile}, BoardProfile};

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.command {
        Command::ListBoards => list_boards(),
        Command::Doctor     => boardghost::doctor::run(),
        Command::Build { project, board, profile } => {
            let boards = boards_dir()?;
            let runtime = runtime_dir()?;
            let release = matches!(profile, BuildProfile::Release);
            let r = boardghost::build::run_build(&project, &board, &boards, &runtime, release)?;
            println!("Built: {}", r.binary.display());
            Ok(())
        }
        Command::Run { project, board, profile, screenshot } => {
            let boards  = boards_dir()?;
            let runtime = runtime_dir()?;
            let release = matches!(profile, BuildProfile::Release);
            let r = boardghost::build::run_build(&project, &board, &boards, &runtime, release)?;
            eprintln!("→ Launching {}...", r.binary.display());
            let code = boardghost::run::exec_sketch(&r.binary, screenshot.as_deref(), Some(&project))?;
            std::process::exit(code);
        }
    }
}

fn list_boards() -> Result<()> {
    let dir = boards_dir()?;
    for path in BoardProfile::list_in(&dir)? {
        let p = BoardProfile::load(&path)?;
        println!("  {:<28}  {}", p.name, p.description);
    }
    Ok(())
}

// Locate the boards directory relative to the binary or the repo.
fn boards_dir() -> Result<PathBuf> {
    // Search order: $BOARDGHOST_BOARDS, then ../../runtime/boards, then ./runtime/boards.
    if let Ok(p) = std::env::var("BOARDGHOST_BOARDS") {
        return Ok(PathBuf::from(p));
    }
    let candidates = [
        PathBuf::from("runtime/boards"),
        PathBuf::from("../runtime/boards"),
        PathBuf::from("../../runtime/boards"),
    ];
    for c in &candidates {
        if c.exists() { return Ok(c.clone()); }
    }
    anyhow::bail!("could not locate runtime/boards; set BOARDGHOST_BOARDS");
}

fn runtime_dir() -> Result<PathBuf> {
    if let Ok(p) = std::env::var("BOARDGHOST_RUNTIME") {
        return Ok(PathBuf::from(p));
    }
    for c in ["runtime", "../runtime", "../../runtime"] {
        let p = PathBuf::from(c);
        if p.exists() { return Ok(p); }
    }
    anyhow::bail!("could not locate runtime/; set BOARDGHOST_RUNTIME");
}
