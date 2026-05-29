use std::path::PathBuf;
use tauri::State;

use crate::projects::ProjectEntry;
use crate::state::AppState;

#[derive(serde::Serialize)]
pub struct ProjectSummary {
    pub path:      PathBuf,
    pub board:     String,
    pub last_used: u64,
}

impl From<ProjectEntry> for ProjectSummary {
    fn from(e: ProjectEntry) -> Self {
        Self { path: e.path, board: e.board, last_used: e.last_used }
    }
}

#[tauri::command]
pub fn list_recent_projects(state: State<'_, AppState>) -> Vec<ProjectSummary> {
    let store = state.store.lock().unwrap();
    store.recent().into_iter().map(Into::into).collect()
}

#[tauri::command]
pub fn add_project(
    state: State<'_, AppState>,
    path:  PathBuf,
    board: String,
) -> Result<(), String> {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);

    {
        let mut store = state.store.lock().unwrap();
        store.add_or_update(ProjectEntry { path, board, last_used: now });
        store.save(&state.projects_path).map_err(|e| e.to_string())?;
    }
    Ok(())
}

#[derive(serde::Serialize, Debug)]
pub struct BoardSummary {
    pub name:        String,
    pub description: String,
}

pub fn parse_list_boards_output(stdout: &str) -> Vec<BoardSummary> {
    stdout
        .lines()
        .map(|l| l.trim())
        .filter(|l| !l.is_empty())
        .filter_map(|l| {
            let mut iter = l.splitn(2, char::is_whitespace);
            let name = iter.next()?.to_string();
            let desc = iter.next()?.trim().to_string();
            Some(BoardSummary { name, description: desc })
        })
        .collect()
}

#[tauri::command]
pub async fn list_boards() -> Result<Vec<BoardSummary>, String> {
    let out = tokio::process::Command::new("boardghost")
        .arg("list-boards")
        .output()
        .await
        .map_err(|e| format!("could not invoke boardghost: {e}"))?;
    if !out.status.success() {
        return Err(format!(
            "boardghost list-boards exited {}: {}",
            out.status,
            String::from_utf8_lossy(&out.stderr)
        ));
    }
    Ok(parse_list_boards_output(&String::from_utf8_lossy(&out.stdout)))
}

use crate::runner::{self, RunOptions};
use tauri::AppHandle;

#[tauri::command]
pub async fn build_and_run(
    app:     AppHandle,
    state:   State<'_, AppState>,
    project: PathBuf,
    board:   String,
    options: Option<RunOptions>,
) -> Result<(), String> {
    // Kill any previous sketch first.
    {
        let mut slot = state.running.lock().unwrap();
        if let Some(mut child) = slot.take() {
            let _ = child.start_kill();
        }
    }

    let child = runner::spawn(
        app.clone(),
        project.clone(),
        board.clone(),
        options.unwrap_or_default(),
    )
        .await
        .map_err(|e| e.to_string())?;

    let pid = child.id();
    {
        let mut slot = state.running.lock().unwrap();
        *slot = Some(child);
    }
    *state.running_pid.lock().unwrap() = pid;

    // Record the project as recently used.
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    {
        let mut store = state.store.lock().unwrap();
        store.add_or_update(crate::projects::ProjectEntry { path: project, board, last_used: now });
        store.save(&state.projects_path).map_err(|e| e.to_string())?;
    }

    Ok(())
}

#[tauri::command]
pub async fn stop(state: State<'_, AppState>) -> Result<(), String> {
    let child_opt = {
        let mut slot = state.running.lock().unwrap();
        slot.take()
    };

    let Some(mut child) = child_opt else { return Ok(()); };

    // Try graceful kill first. tokio's Child::start_kill maps to SIGKILL on
    // Unix; for SIGTERM we drop to the raw libc / unix-specific path.
    #[cfg(unix)]
    {
        if let Some(pid) = child.id() {
            unsafe { libc::kill(pid as i32, libc::SIGTERM); }
        }
    }

    // Wait up to 2 seconds.
    let wait = tokio::time::timeout(std::time::Duration::from_secs(2), child.wait()).await;
    if wait.is_err() {
        // Still alive — escalate.
        let _ = child.start_kill();
        let _ = child.wait().await;
    }

    *state.running_pid.lock().unwrap() = None;
    Ok(())
}

#[tauri::command]
pub async fn screenshot(state: State<'_, AppState>) -> Result<String, String> {
    let pid = state.running_pid.lock().unwrap().clone();
    let Some(pid) = pid else {
        return Err("no sketch running".to_string());
    };

    let path = std::env::temp_dir().join("boardghost-screenshot.png");

    // Send SIGUSR1; the sketch's signal handler will write the PNG.
    #[cfg(unix)]
    unsafe { libc::kill(pid as i32, libc::SIGUSR1); }

    // Wait briefly for the file to appear (next sim_pump_events iteration).
    for _ in 0..20 {
        if std::path::Path::new(&path).exists() {
            return Ok(path.to_string_lossy().to_string());
        }
        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
    }
    Err(format!("screenshot timed out after 2s (path was {})", path.display()))
}
