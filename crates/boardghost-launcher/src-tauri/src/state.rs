use std::path::PathBuf;
use std::sync::Mutex;
use tokio::process::Child;

use crate::projects::ProjectStore;

/// Shared application state held in Tauri's State<'_>.
pub struct AppState {
    /// Path to projects.json — fixed at startup so tests can override.
    pub projects_path: PathBuf,
    /// In-memory copy of the project store.
    pub store:         Mutex<ProjectStore>,
    /// Currently running sketch child (if any). Killed on `stop`.
    pub running:       Mutex<Option<Child>>,
    /// PID of the currently running sketch process (if any).
    pub running_pid:   Mutex<Option<u32>>,
}

impl AppState {
    pub fn new(projects_path: PathBuf) -> anyhow::Result<Self> {
        let store = ProjectStore::load(&projects_path).unwrap_or_default();
        Ok(Self {
            projects_path,
            store:       Mutex::new(store),
            running:     Mutex::new(None),
            running_pid: Mutex::new(None),
        })
    }
}
