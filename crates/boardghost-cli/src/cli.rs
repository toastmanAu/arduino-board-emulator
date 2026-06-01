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
        /// If set, the sketch will write a PNG to this path on SIGUSR1
        /// (and the CLI will fire SIGUSR1 once 2 seconds after launch).
        #[arg(long, value_name = "PNG_PATH")]
        screenshot: Option<PathBuf>,
    },
    /// Snapshot or restore a sketch's EEPROM state. Snapshot once after
    /// completing the in-sketch setup flow; future launches auto-restore from
    /// `<project>/eeprom.seed.bin` so the sketch boots into the configured
    /// state instead of the cold-start onboarding path.
    Seed {
        #[command(subcommand)]
        action: SeedAction,
    },
}

#[derive(Subcommand)]
pub enum SeedAction {
    /// Copy the live `.boardghost/eeprom.bin` to a seed file (default
    /// `<project>/eeprom.seed.bin`). Fails if the sketch hasn't yet run far
    /// enough to commit EEPROM.
    Save {
        #[arg(value_name = "PROJECT_DIR")]
        project: PathBuf,
        /// Output path. Defaults to `<project>/eeprom.seed.bin`.
        #[arg(long, value_name = "PATH")]
        to: Option<PathBuf>,
    },
    /// Copy a seed file over the live `.boardghost/eeprom.bin`, overwriting
    /// any existing live state. Reads from `<project>/eeprom.seed.bin` by
    /// default.
    Restore {
        #[arg(value_name = "PROJECT_DIR")]
        project: PathBuf,
        /// Input path. Defaults to `<project>/eeprom.seed.bin`.
        #[arg(long, value_name = "PATH")]
        from: Option<PathBuf>,
    },
}

#[derive(Copy, Clone, Debug, clap::ValueEnum)]
pub enum BuildProfile {
    Debug,
    Release,
}
