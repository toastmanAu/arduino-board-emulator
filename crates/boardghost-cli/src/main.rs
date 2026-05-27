use anyhow::Result;
use clap::Parser;
use std::path::PathBuf;

use boardghost::{cli::{Cli, Command}, BoardProfile};

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.command {
        Command::ListBoards => list_boards(),
        Command::Doctor     => boardghost::doctor::run(),
        Command::Build { .. } => anyhow::bail!("build: not yet implemented (Task 16-20)"),
        Command::Run   { .. } => anyhow::bail!("run: not yet implemented (Task 21)"),
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
