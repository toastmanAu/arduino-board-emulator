use anyhow::{Context, Result};
use clap::Parser;
use std::path::PathBuf;

use boardghost::{
    cli::{BuildProfile, Cli, Command, OtaAction, SeedAction, UartAction},
    eeprom_seed, ota, BoardProfile,
};
use std::io::Write;

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.command {
        Command::ListBoards => list_boards(),
        Command::Doctor => boardghost::doctor::run(),
        Command::Build {
            project,
            board,
            profile,
        } => {
            let boards = boards_dir()?;
            let runtime = runtime_dir()?;
            let release = matches!(profile, BuildProfile::Release);
            let r = boardghost::build::run_build(&project, &board, &boards, &runtime, release)?;
            println!("Built: {}", r.binary.display());
            Ok(())
        }
        Command::Run {
            project,
            board,
            profile,
            screenshot,
        } => {
            let boards = boards_dir()?;
            let runtime = runtime_dir()?;
            let release = matches!(profile, BuildProfile::Release);
            let r = boardghost::build::run_build(&project, &board, &boards, &runtime, release)?;
            match eeprom_seed::auto_restore_if_missing(&project)? {
                Some(seed) => eprintln!(
                    "→ EEPROM seed: restored {} → .boardghost/eeprom.bin (live EEPROM was missing)",
                    seed.display()
                ),
                None => {}
            }
            eprintln!("→ Launching {}...", r.binary.display());
            let code =
                boardghost::run::exec_sketch(&r.binary, screenshot.as_deref(), Some(&project))?;
            std::process::exit(code);
        }
        Command::Seed { action } => seed(action),
        Command::Uart { action } => uart(action),
        Command::Ota { action } => ota_cmd(action),
    }
}

fn ota_cmd(action: OtaAction) -> Result<()> {
    match action {
        OtaAction::Push {
            file,
            port,
            password,
        } => {
            let firmware =
                std::fs::read(&file).with_context(|| format!("reading {}", file.display()))?;
            eprintln!("→ Pushing {} bytes to 127.0.0.1:{port}...", firmware.len());
            ota::push(port, &firmware, password.as_deref())?;
            eprintln!("✓ OTA accepted by device");
            Ok(())
        }
    }
}

fn uart(action: UartAction) -> Result<()> {
    match action {
        UartAction::Inject {
            project,
            port,
            crlf,
            text,
            hex,
            queue,
        } => {
            let bytes: Vec<u8> = if hex {
                // Strip whitespace then parse hex pairs; helpful for modem
                // protocols where you paste a wireshark dump.
                let cleaned: String = text.chars().filter(|c| !c.is_whitespace()).collect();
                if cleaned.len() % 2 != 0 {
                    anyhow::bail!(
                        "--hex needs an even number of digits, got {} chars",
                        cleaned.len()
                    );
                }
                (0..cleaned.len())
                    .step_by(2)
                    .map(|i| u8::from_str_radix(&cleaned[i..i + 2], 16))
                    .collect::<std::result::Result<_, _>>()
                    .map_err(|e| anyhow::anyhow!("invalid hex: {e}"))?
            } else {
                let mut v = text.into_bytes();
                if crlf {
                    v.extend_from_slice(b"\r\n");
                }
                v
            };
            if queue {
                // Stage for next-trigger delivery. The runtime's GM861S
                // emulator drains this file into delayed_rx whenever the
                // sketch fires a start-trigger; the bytes then surface in
                // rx_buf after the trigger ACK has been read. Removes the
                // 10-second timing pressure entirely — queue first, click
                // the trigger UI whenever you're ready.
                let qpath = project
                    .join(".boardghost")
                    .join(format!("uart-{port}-queue.bin"));
                if let Some(parent) = qpath.parent() {
                    std::fs::create_dir_all(parent).ok();
                }
                // Truncate-and-write: each invocation replaces the staged
                // scan, since a scanner only fires one barcode per trigger.
                let mut f = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .truncate(true)
                    .open(&qpath)?;
                f.write_all(&bytes)?;
                f.flush()?;
                eprintln!(
                    "Queued {} byte(s) for UART{port} — will fire on the next \
                    scanner trigger (click the scanner activate UI to release).",
                    bytes.len()
                );
                return Ok(());
            }
            let fifo = project
                .join(".boardghost")
                .join(format!("uart-{port}-in.fifo"));
            if !fifo.exists() {
                anyhow::bail!(
                    "FIFO {} doesn't exist yet — start the sketch first, the runtime creates the FIFO on the first HardwareSerial({port}) call",
                    fifo.display()
                );
            }
            // Open in write-only mode; the runtime opened it O_NONBLOCK read,
            // so this won't block waiting for a reader.
            let mut f = std::fs::OpenOptions::new()
                .write(true)
                .open(&fifo)
                .map_err(|e| anyhow::anyhow!("open {} for write failed: {e}", fifo.display()))?;
            f.write_all(&bytes)?;
            f.flush()?;
            eprintln!("Injected {} byte(s) into UART{port}", bytes.len());
            Ok(())
        }
    }
}

fn seed(action: SeedAction) -> Result<()> {
    match action {
        SeedAction::Save { project, to } => {
            let dest = to.unwrap_or_else(|| eeprom_seed::default_seed_path(&project));
            eeprom_seed::save(&project, &dest)?;
            println!("Saved EEPROM seed → {}", dest.display());
            Ok(())
        }
        SeedAction::Restore { project, from } => {
            let src = from.unwrap_or_else(|| eeprom_seed::default_seed_path(&project));
            eeprom_seed::restore(&project, &src)?;
            println!(
                "Restored EEPROM from {} → .boardghost/eeprom.bin",
                src.display()
            );
            Ok(())
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
        if c.exists() {
            return Ok(c.clone());
        }
    }
    anyhow::bail!("could not locate runtime/boards; set BOARDGHOST_BOARDS");
}

fn runtime_dir() -> Result<PathBuf> {
    if let Ok(p) = std::env::var("BOARDGHOST_RUNTIME") {
        return Ok(PathBuf::from(p));
    }
    for c in ["runtime", "../runtime", "../../runtime"] {
        let p = PathBuf::from(c);
        if p.exists() {
            return Ok(p);
        }
    }
    anyhow::bail!("could not locate runtime/; set BOARDGHOST_RUNTIME");
}
