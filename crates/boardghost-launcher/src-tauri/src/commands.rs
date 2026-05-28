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

// list_boards, build_and_run, stop arrive in later tasks (5, 7, 8).
