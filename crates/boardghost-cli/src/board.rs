use serde::Deserialize;
use std::path::{Path, PathBuf};

use crate::error::BoardGhostError;

#[derive(Debug, Clone, Deserialize)]
pub struct BoardProfile {
    pub name: String,
    pub description: String,
    pub arduino_fqbn_hint: String,
    pub display: DisplayConfig,
    pub touch: Option<TouchConfig>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct DisplayConfig {
    pub controller: String,
    pub width: u32,
    pub height: u32,
    pub rotation: u8,
    pub bus: String,
    pub color_depth: u8,
}

#[derive(Debug, Clone, Deserialize)]
pub struct TouchConfig {
    pub controller: String,
}

impl BoardProfile {
    pub fn load(path: &Path) -> Result<Self, BoardGhostError> {
        let text = std::fs::read_to_string(path).map_err(|e| {
            BoardGhostError::BoardProfileMalformed {
                path: path.to_path_buf(),
                reason: format!("read failed: {e}"),
            }
        })?;
        toml::from_str(&text).map_err(|e| BoardGhostError::BoardProfileMalformed {
            path: path.to_path_buf(),
            reason: e.message().to_string(),
        })
    }

    pub fn load_by_name(boards_dir: &Path, name: &str) -> Result<Self, BoardGhostError> {
        let path = boards_dir.join(format!("{name}.toml"));
        if !path.exists() {
            return Err(BoardGhostError::UnknownBoard(name.to_string()));
        }
        Self::load(&path)
    }

    pub fn list_in(boards_dir: &Path) -> Result<Vec<PathBuf>, std::io::Error> {
        let mut out: Vec<PathBuf> = std::fs::read_dir(boards_dir)?
            .filter_map(|e| e.ok())
            .map(|e| e.path())
            .filter(|p| p.extension().and_then(|s| s.to_str()) == Some("toml"))
            .collect();
        out.sort();
        Ok(out)
    }
}
