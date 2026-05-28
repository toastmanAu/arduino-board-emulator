use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProjectEntry {
    pub path: PathBuf,
    pub board: String,
    pub last_used: u64,   // unix seconds
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ProjectStore {
    #[serde(default)]
    entries: Vec<ProjectEntry>,
}

impl ProjectStore {
    pub fn load(path: &Path) -> anyhow::Result<Self> {
        if !path.exists() {
            return Ok(Self::default());
        }
        let text = std::fs::read_to_string(path)?;
        // Tolerate a corrupt or empty file by returning an empty store instead
        // of erroring — the launcher should never refuse to start because of a
        // bad projects.json. Better to log and recover.
        Ok(serde_json::from_str(&text).unwrap_or_default())
    }

    pub fn save(&self, path: &Path) -> anyhow::Result<()> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let text = serde_json::to_string_pretty(self)?;
        std::fs::write(path, text)?;
        Ok(())
    }

    pub fn add_or_update(&mut self, entry: ProjectEntry) {
        if let Some(slot) = self.entries.iter_mut().find(|e| e.path == entry.path) {
            *slot = entry;
        } else {
            self.entries.push(entry);
        }
    }

    pub fn recent(&self) -> Vec<ProjectEntry> {
        let mut out = self.entries.clone();
        out.sort_by(|a, b| b.last_used.cmp(&a.last_used));
        out
    }
}
