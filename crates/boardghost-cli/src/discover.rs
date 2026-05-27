use crate::error::BoardGhostError;
use std::path::{Path, PathBuf};

#[derive(Debug)]
pub struct DiscoveredProject {
    pub root:  PathBuf,
    pub entry: PathBuf,
}

pub fn discover(project: &Path) -> Result<DiscoveredProject, BoardGhostError> {
    if !project.exists() {
        return Err(BoardGhostError::ProjectNotFound(project.to_path_buf()));
    }

    // 1. PlatformIO-style: src/main.cpp
    let pio = project.join("src/main.cpp");
    if pio.exists() {
        return Ok(DiscoveredProject { root: project.to_path_buf(), entry: pio });
    }

    // 2. Arduino IDE-style: sketch/sketch.ino or sketch/<dirname>.ino
    let sketch_dir = project.join("sketch");
    if sketch_dir.is_dir() {
        if let Some(ino) = first_ino_in(&sketch_dir) {
            return Ok(DiscoveredProject { root: project.to_path_buf(), entry: ino });
        }
    }

    // 3. Top-level .ino in the project dir.
    if let Some(ino) = first_ino_in(project) {
        return Ok(DiscoveredProject { root: project.to_path_buf(), entry: ino });
    }

    Err(BoardGhostError::SketchNotFound { dir: project.to_path_buf() })
}

fn first_ino_in(dir: &Path) -> Option<PathBuf> {
    let mut inos: Vec<PathBuf> = std::fs::read_dir(dir).ok()?
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().and_then(|s| s.to_str()) == Some("ino"))
        .collect();
    inos.sort();
    inos.into_iter().next()
}
