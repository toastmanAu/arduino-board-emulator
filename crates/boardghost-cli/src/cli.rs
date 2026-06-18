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
        /// Expose the mirror (observe+act surface) on the LAN, not just
        /// loopback. Generates a random token unless --mirror-token is given,
        /// and prints a pairing URL + token for an agent / the companion app.
        #[arg(long, default_value_t = false)]
        mirror: bool,
        /// Token gating all /mirror/* routes when --mirror is set. Random if
        /// omitted.
        #[arg(long, value_name = "TOKEN")]
        mirror_token: Option<String>,
    },
    /// Snapshot or restore a sketch's EEPROM state. Snapshot once after
    /// completing the in-sketch setup flow; future launches auto-restore from
    /// `<project>/eeprom.seed.bin` so the sketch boots into the configured
    /// state instead of the cold-start onboarding path.
    Seed {
        #[command(subcommand)]
        action: SeedAction,
    },
    /// Inject bytes into a sketch's HardwareSerial(N) input — drives a QR
    /// scanner, GSM modem, or any other UART peripheral the sketch reads
    /// from. Writes to the FIFO the runtime listens on, then exits.
    Uart {
        #[command(subcommand)]
        action: UartAction,
    },
    /// Push a firmware file to a running sim over the espota protocol — the
    /// same path arduino-cli / the IDE network port uses, but built-in so no
    /// Python is required. The sketch must be running with BOARDGHOST_NET=real
    /// and have called ArduinoOTA.begin().
    Ota {
        #[command(subcommand)]
        action: OtaAction,
    },
}

#[derive(Subcommand)]
pub enum UartAction {
    /// Send a string (plus optional \r\n) to the sketch's HardwareSerial(N).
    /// Default is port 2 (matches the QR-scanner convention on ESP32).
    Inject {
        #[arg(value_name = "PROJECT_DIR")]
        project: PathBuf,
        /// Which UART the sketch is listening on (HardwareSerial(N)).
        #[arg(long, default_value_t = 2u8)]
        port: u8,
        /// Append CR+LF after the text (most barcode scanners do this).
        #[arg(long, default_value_t = true)]
        crlf: bool,
        /// The text to inject. Use --hex for binary protocols (modems etc.).
        text: String,
        /// Interpret `text` as hex pairs (e.g. "7e00080200" instead of ASCII).
        #[arg(long, default_value_t = false)]
        hex: bool,
        /// Stage the bytes for delivery on the *next* scanner trigger
        /// instead of writing to the FIFO immediately. Solves the
        /// "trigger window is only 10 seconds" timing pressure — pre-queue
        /// at your own pace, click the trigger UI, the scan fires.
        /// Only meaningful when the port has a peripheral emulator
        /// (BOARDGHOST_UART_<N>_PERIPHERAL=gm861s).
        #[arg(long, default_value_t = false)]
        queue: bool,
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

#[derive(Subcommand)]
pub enum OtaAction {
    /// Stream a firmware .bin to the sim's OTA receiver on 127.0.0.1:<port>.
    Push {
        /// Firmware file to upload.
        #[arg(value_name = "FIRMWARE_BIN")]
        file: PathBuf,
        /// OTA UDP port the sim is listening on (matches BOARDGHOST_OTA_PORT).
        #[arg(long, default_value_t = 3232u16)]
        port: u16,
        /// Password, if the sketch called ArduinoOTA.setPassword().
        #[arg(long)]
        password: Option<String>,
    },
}

#[derive(Copy, Clone, Debug, clap::ValueEnum)]
pub enum BuildProfile {
    Debug,
    Release,
}
