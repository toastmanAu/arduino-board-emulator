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

use crate::runner;
use tauri::AppHandle;

#[tauri::command]
pub async fn build_and_run(
    app:     AppHandle,
    state:   State<'_, AppState>,
    project: PathBuf,
    board:   String,
) -> Result<(), String> {
    // Kill any previous sketch first.
    {
        let mut slot = state.running.lock().unwrap();
        if let Some(mut child) = slot.take() {
            let _ = child.start_kill();
        }
    }

    let child = runner::spawn(app.clone(), project.clone(), board.clone())
        .await
        .map_err(|e| e.to_string())?;

    {
        let mut slot = state.running.lock().unwrap();
        *slot = Some(child);
    }

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
