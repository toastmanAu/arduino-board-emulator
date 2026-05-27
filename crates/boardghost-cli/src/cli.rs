use clap::{Parser, Subcommand};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "boardghost", version, about = "Arduino/ESP32 sketch simulator")]
pub struct Cli {
    #[command(subcommand)]
    pub command: Command,
}

#[derive(Subcommand)]
pub enum Command {
    /// List available board profiles.
    ListBoards,
    /// Check that required host tools are installed.
    Doctor,
    /// Compile a sketch for the given board (no execution).
    Build {
        #[arg(value_name = "PROJECT_DIR")]
        project: PathBuf,
        #[arg(long)]
        board: String,
        #[arg(long, default_value = "debug")]
        profile: BuildProfile,
    },
    /// Compile and run a sketch for the given board.
    Run {
        #[arg(value_name = "PROJECT_DIR")]
        project: PathBuf,
        #[arg(long)]
        board: String,
        #[arg(long, default_value = "debug")]
        profile: BuildProfile,
    },
}

#[derive(Copy, Clone, Debug, clap::ValueEnum)]
pub enum BuildProfile {
    Debug,
    Release,
}
