use std::path::PathBuf;
use std::process::Stdio;
use tauri::{AppHandle, Emitter};
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::{Child, Command};

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
) -> anyhow::Result<Child> {
    let mut child = Command::new("boardghost")
        .arg("run")
        .arg(&project)
        .arg("--board")
        .arg(&board)
        .env("SDL_VIDEODRIVER", "x11")
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()?;

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
