use serde::Deserialize;
use std::path::Path;

/// Per-library override config loaded from
/// `runtime/library_overrides/<name>.toml`. Used to skip platform-specific
/// source files (e.g. `network/esp32/` in ArduinoWebsockets) and inject
/// boardghost-specific shim dirs.
///
/// All paths are relative to the library's `source_dir`.
#[derive(Debug, Clone, Default, Deserialize, PartialEq)]
pub struct LibraryOverride {
    /// Directories under source_dir to exclude from globbing (e.g.
    /// `["network/esp32", "network/esp8266"]`).
    #[serde(default)]
    pub exclude_dirs: Vec<String>,

    /// Individual files under source_dir to exclude (e.g.
    /// `["tiny_websockets/internals/server.cpp"]`).
    #[serde(default)]
    pub exclude_files: Vec<String>,

    /// Extra include directories OUTSIDE source_dir (e.g.
    /// `["${BOARDGHOST_RUNTIME_DIR}/shims/arduino_websockets_sim"]`).
    /// `${BOARDGHOST_RUNTIME_DIR}` will be substituted by the CMake codegen.
    #[serde(default)]
    pub add_include_dirs: Vec<String>,

    /// If true, the library's own source globs are skipped entirely — only
    /// `add_include_dirs` contribute. Use when shipping a complete shim.
    #[serde(default)]
    pub shim_only: bool,
}

/// Load an override from `<overrides_dir>/<name>.toml`. Returns
/// `Ok(None)` when no override exists (the common case).
pub fn load_override(
    overrides_dir: &Path,
    library_name: &str,
) -> anyhow::Result<Option<LibraryOverride>> {
    let path = overrides_dir.join(format!("{library_name}.toml"));
    if !path.exists() {
        return Ok(None);
    }
    let content = std::fs::read_to_string(&path)?;
    let parsed: LibraryOverride = toml::from_str(&content)?;
    Ok(Some(parsed))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn returns_none_when_missing() {
        let dir = tempdir().unwrap();
        let result = load_override(dir.path(), "Whatever").unwrap();
        assert!(result.is_none());
    }

    #[test]
    fn loads_full_override() {
        let dir = tempdir().unwrap();
        let body = r#"
            exclude_dirs = ["network/esp32", "network/esp8266"]
            exclude_files = ["server.cpp"]
            add_include_dirs = ["${BOARDGHOST_RUNTIME_DIR}/shims/sim_ws"]
            shim_only = false
        "#;
        fs::write(dir.path().join("ArduinoWebsockets.toml"), body).unwrap();
        let ov = load_override(dir.path(), "ArduinoWebsockets").unwrap().unwrap();
        assert_eq!(ov.exclude_dirs, vec!["network/esp32", "network/esp8266"]);
        assert_eq!(ov.exclude_files, vec!["server.cpp"]);
        assert_eq!(ov.add_include_dirs, vec!["${BOARDGHOST_RUNTIME_DIR}/shims/sim_ws"]);
        assert!(!ov.shim_only);
    }

    #[test]
    fn loads_partial_override_with_defaults() {
        let dir = tempdir().unwrap();
        fs::write(dir.path().join("Foo.toml"), "shim_only = true\n").unwrap();
        let ov = load_override(dir.path(), "Foo").unwrap().unwrap();
        assert!(ov.shim_only);
        assert!(ov.exclude_dirs.is_empty());
    }
}
