use crate::error::BoardGhostError;
use serde::Deserialize;
use std::path::PathBuf;
use std::process::Command;

#[derive(Debug, Clone, Deserialize)]
pub struct InstalledLibrary {
    pub name: String,
    pub install_dir: PathBuf,
    pub source_dir: PathBuf,
    #[serde(default)]
    pub layout: String,
    #[serde(default)]
    pub provides_includes: Vec<String>,
}

#[derive(Debug, Deserialize)]
struct LibListResponse {
    installed_libraries: Vec<LibEntry>,
}

#[derive(Debug, Deserialize)]
struct LibEntry {
    library: InstalledLibrary,
}

pub fn parse_lib_list_json(json: &str) -> Result<Vec<InstalledLibrary>, BoardGhostError> {
    let resp: LibListResponse =
        serde_json::from_str(json).map_err(|e| BoardGhostError::ArduinoLibListFailed {
            stderr: format!("parse: {e}"),
        })?;
    Ok(resp.installed_libraries.into_iter().map(|e| e.library).collect())
}

pub fn list_installed() -> Result<Vec<InstalledLibrary>, BoardGhostError> {
    let out = Command::new("arduino-cli")
        .args(["lib", "list", "--format", "json"])
        .output()
        .map_err(|_| BoardGhostError::ArduinoCliMissing)?;
    if !out.status.success() {
        return Err(BoardGhostError::ArduinoLibListFailed {
            stderr: String::from_utf8_lossy(&out.stderr).to_string(),
        });
    }
    parse_lib_list_json(&String::from_utf8_lossy(&out.stdout))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_fixture() {
        let json = include_str!("../tests/fixtures/arduino_lib_list.json");
        let libs = parse_lib_list_json(json).unwrap();
        assert_eq!(libs.len(), 2);
        assert_eq!(libs[0].name, "ArduinoJson");
        assert_eq!(libs[0].source_dir.to_str().unwrap(),
                   "/tmp/Arduino/libraries/ArduinoJson/src");
        assert_eq!(libs[0].provides_includes, vec!["ArduinoJson.h", "ArduinoJson.hpp"]);
        assert_eq!(libs[1].name, "Time");
        assert_eq!(libs[1].layout, "flat");
    }

    #[test]
    fn rejects_invalid_json() {
        let result = parse_lib_list_json("{not json");
        assert!(result.is_err());
    }
}
