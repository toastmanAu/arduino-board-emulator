use serde::Deserialize;
use std::path::PathBuf;
use std::process::Stdio;
use tauri::{AppHandle, Emitter};
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::{Child, Command};

/// Run-time options passed from the launcher UI to control the sketch's
/// environment. Each field maps to one BOARDGHOST_* env var the CLI/runtime
/// reads. None / empty values mean "leave the env var unset" so defaults
/// (typically the safer fake-mode behaviour) apply.
#[derive(Debug, Default, Clone, Deserialize)]
#[serde(default)]
pub struct RunOptions {
    /// "fake" | "fail" | "real" — empty string means defer to default (fake).
    pub net_mode: String,
    /// Auto-complete lcd.calibrateTouch() so sketches that wait for taps
    /// don't hang in headless mode.
    pub auto_touch_cal: bool,
    /// Scripted touches in screen coords, format "t_ms:x,y;..." Empty = none.
    pub sim_touches_screen: String,
    /// Milliseconds before screenshot fires (0 = default 2000ms in CLI).
    pub screenshot_delay_ms: u32,
    /// Expose the mirror on the LAN (passes --mirror to the CLI, which
    /// generates a token and prints the pairing URL). Off = loopback only.
    pub mirror: bool,
}

/// Spawn `boardghost run <project> --board <board>` as a child process.
/// stdout lines are emitted on the `serial-log` event; stderr lines on
/// the `build-log` event (which is where the CLI writes its own progress).
///
/// The returned Child is owned by the caller (typically stashed in AppState
/// so `stop` can kill it). The two reader tasks run until EOF or are
/// implicitly cancelled when the child is killed.
pub async fn spawn(
    app: AppHandle,
    project: PathBuf,
    board: String,
    opts: RunOptions,
) -> anyhow::Result<Child> {
    let fixed = std::env::temp_dir().join("boardghost-screenshot.png");

    let mut cmd = Command::new("boardghost");
    cmd.arg("run")
        .arg(&project)
        .arg("--board")
        .arg(&board)
        .env("SDL_VIDEODRIVER", "x11")
        .env("BOARDGHOST_SCREENSHOT_PATH", &fixed)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());

    if opts.mirror {
        // Let the CLI generate the token and print the pairing banner (it
        // surfaces on the build-log via stderr).
        cmd.arg("--mirror");
    }

    if !opts.net_mode.is_empty() {
        cmd.env("BOARDGHOST_NET", &opts.net_mode);
    }
    if opts.auto_touch_cal {
        cmd.env("BOARDGHOST_AUTO_TOUCH_CAL", "1");
    }
    if !opts.sim_touches_screen.is_empty() {
        cmd.env("BOARDGHOST_SIM_TOUCHES_SCREEN", &opts.sim_touches_screen);
    }
    if opts.screenshot_delay_ms > 0 {
        cmd.env(
            "BOARDGHOST_SCREENSHOT_DELAY_MS",
            opts.screenshot_delay_ms.to_string(),
        );
    }

    let mut child = cmd.spawn()?;

    if let Some(stdout) = child.stdout.take() {
        let app = app.clone();
        tokio::spawn(async move {
            let reader = BufReader::new(stdout);
            let mut lines = reader.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                let _ = app.emit("serial-log", line);
            }
        });
    }

    if let Some(stderr) = child.stderr.take() {
        let app = app.clone();
        tokio::spawn(async move {
            let reader = BufReader::new(stderr);
            let mut lines = reader.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                if let Some(rest) = line.strip_prefix("[gpio] ") {
                    let _ = app.emit("gpio-log", rest.to_string());
                } else {
                    let _ = app.emit("build-log", line);
                }
            }
        });
    }

    Ok(child)
}
