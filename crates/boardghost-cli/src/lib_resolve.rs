use crate::arduino_libs::InstalledLibrary;
use crate::error::BoardGhostError;
use crate::library_overrides::{load_override, LibraryOverride};
use std::collections::BTreeSet;
use std::path::Path;

/// A library that's been matched to the sketch's includes and is ready to
/// be passed to codegen.
#[derive(Debug, Clone, PartialEq)]
pub struct ResolvedLibrary {
    pub name: String,
    pub source_dir: std::path::PathBuf,
    /// Source globs to add to the build, after applying override exclusions.
    pub glob_patterns: Vec<String>,
    /// Subdirectories to ignore (each is RELATIVE to source_dir).
    pub exclude_dirs: Vec<String>,
    /// Individual files to drop from the glob result (RELATIVE to source_dir).
    pub exclude_files: Vec<String>,
    /// Extra include dirs (already string-substituted by codegen).
    pub add_include_dirs: Vec<String>,
    /// If true, the library's own source files are skipped — only shim dirs apply.
    pub shim_only: bool,
}

/// Decides which installed libraries the sketch needs based on scanned
/// headers, applies per-library overrides, and returns the resolved set.
///
/// `headers` are bare header names from `include_scan::scan_text`
/// (e.g. `"WiFi.h"`, `"ArduinoJson.h"`).
///
/// `shimmed` is the set of header names BoardGhost already shims natively
/// (from `crate::libraries::ALLOWLIST`); those are NOT passed to discovery
/// because our `runtime/shims/` headers take precedence at preprocess time.
pub fn resolve(
    headers: &BTreeSet<String>,
    installed: &[InstalledLibrary],
    overrides_dir: &Path,
    shimmed: &[&str],
) -> Result<Vec<ResolvedLibrary>, BoardGhostError> {
    let mut resolved = Vec::new();
    let mut used_libs = BTreeSet::new();

    for header in headers {
        if header_is_shimmed(header, shimmed) {
            continue;
        }
        if is_stdlib_or_arduino_core(header) {
            continue;
        }
        let Some(lib) = installed.iter().find(|l| {
            l.provides_includes.iter().any(|h| h.eq_ignore_ascii_case(header))
        }) else {
            return Err(BoardGhostError::LibraryNotInstalled {
                header: header.clone(),
            });
        };
        if used_libs.insert(lib.name.clone()) {
            let ov = load_override(overrides_dir, &lib.name)
                .map_err(|e| BoardGhostError::LibraryNotInstalled {
                    header: format!("{}: override parse failed: {e}", lib.name),
                })?
                .unwrap_or_default();
            resolved.push(build_resolved(lib, ov));
        }
    }
    Ok(resolved)
}

fn header_is_shimmed(header: &str, shimmed: &[&str]) -> bool {
    // Shimmed entries match by library name; a header like "WiFi.h" is shimmed
    // when the library name "WiFi" is in the shimmed list.
    let stem = header.trim_end_matches(".h").trim_end_matches(".hpp");
    shimmed.iter().any(|s| s.eq_ignore_ascii_case(stem))
}

fn is_stdlib_or_arduino_core(header: &str) -> bool {
    // Anything matching these names is provided by arduino-cli's core or the
    // C/C++ stdlib — no library to resolve.
    const CORE_HEADERS: &[&str] = &[
        // Arduino core
        "Arduino.h", "Wire.h", "SPI.h", "Serial.h", "EEPROM.h",
        // C stdlib
        "stdint.h", "stddef.h", "stdio.h", "stdlib.h", "stdarg.h",
        "string.h", "strings.h", "math.h", "time.h", "ctype.h",
        "errno.h", "assert.h", "limits.h", "float.h", "inttypes.h",
        // POSIX-y headers Arduino sketches sometimes reach for
        "unistd.h", "sys/time.h",
    ];
    CORE_HEADERS.iter().any(|p| header.eq_ignore_ascii_case(p))
        || header.ends_with(".hpp") && header.starts_with("std")
        || !header.contains('.') // bare std headers like "vector", "string"
}

fn build_resolved(lib: &InstalledLibrary, ov: LibraryOverride) -> ResolvedLibrary {
    let glob_patterns = if ov.shim_only {
        vec![]
    } else {
        // Recursive layout has src/, flat layout uses install_dir.
        let layout_recursive = lib.layout == "recursive";
        if layout_recursive {
            vec!["**/*.cpp".to_string(), "**/*.c".to_string()]
        } else {
            vec!["*.cpp".to_string(), "*.c".to_string()]
        }
    };
    ResolvedLibrary {
        name: lib.name.clone(),
        source_dir: lib.source_dir.clone(),
        glob_patterns,
        exclude_dirs: ov.exclude_dirs,
        exclude_files: ov.exclude_files,
        add_include_dirs: ov.add_include_dirs,
        shim_only: ov.shim_only,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;
    use tempfile::tempdir;

    fn fixture_libs() -> Vec<InstalledLibrary> {
        vec![
            InstalledLibrary {
                name: "ArduinoJson".into(),
                install_dir: PathBuf::from("/tmp/lib/ArduinoJson"),
                source_dir: PathBuf::from("/tmp/lib/ArduinoJson/src"),
                layout: "recursive".into(),
                provides_includes: vec!["ArduinoJson.h".into()],
            },
            InstalledLibrary {
                name: "Time".into(),
                install_dir: PathBuf::from("/tmp/lib/Time"),
                source_dir: PathBuf::from("/tmp/lib/Time"),
                layout: "flat".into(),
                provides_includes: vec!["TimeLib.h".into(), "Time.h".into()],
            },
        ]
    }

    #[test]
    fn resolves_two_libraries() {
        let dir = tempdir().unwrap();
        let headers: BTreeSet<String> =
            ["ArduinoJson.h", "TimeLib.h"].iter().map(|s| s.to_string()).collect();
        let resolved = resolve(&headers, &fixture_libs(), dir.path(), &[]).unwrap();
        assert_eq!(resolved.len(), 2);
        assert_eq!(resolved[0].name, "ArduinoJson");
        assert_eq!(resolved[0].glob_patterns, vec!["**/*.cpp", "**/*.c"]);
        assert_eq!(resolved[1].name, "Time");
        assert_eq!(resolved[1].glob_patterns, vec!["*.cpp", "*.c"]);
    }

    #[test]
    fn skips_shimmed_libraries() {
        let dir = tempdir().unwrap();
        let headers: BTreeSet<String> = ["WiFi.h", "ArduinoJson.h"]
            .iter().map(|s| s.to_string()).collect();
        let resolved = resolve(&headers, &fixture_libs(), dir.path(), &["WiFi"]).unwrap();
        assert_eq!(resolved.len(), 1);
        assert_eq!(resolved[0].name, "ArduinoJson");
    }

    #[test]
    fn skips_core_and_stdlib() {
        let dir = tempdir().unwrap();
        let headers: BTreeSet<String> = ["Arduino.h", "Wire.h", "vector", "string"]
            .iter().map(|s| s.to_string()).collect();
        let resolved = resolve(&headers, &fixture_libs(), dir.path(), &[]).unwrap();
        assert!(resolved.is_empty());
    }

    #[test]
    fn skips_extended_c_stdlib_headers() {
        // M2.C gap-fix: time.h, stdlib.h, etc. are C stdlib, not Arduino libs.
        let dir = tempdir().unwrap();
        let headers: BTreeSet<String> = [
            "time.h", "stdlib.h", "stdarg.h", "ctype.h",
            "errno.h", "assert.h", "limits.h", "unistd.h",
        ].iter().map(|s| s.to_string()).collect();
        let resolved = resolve(&headers, &fixture_libs(), dir.path(), &[]).unwrap();
        assert!(resolved.is_empty(), "C stdlib headers must not trigger library resolution");
    }

    #[test]
    fn errors_when_header_not_installed() {
        let dir = tempdir().unwrap();
        let headers: BTreeSet<String> =
            ["NotInstalled.h"].iter().map(|s| s.to_string()).collect();
        let result = resolve(&headers, &fixture_libs(), dir.path(), &[]);
        assert!(matches!(result, Err(BoardGhostError::LibraryNotInstalled { header }) if header == "NotInstalled.h"));
    }

    #[test]
    fn applies_override() {
        let dir = tempdir().unwrap();
        std::fs::write(dir.path().join("ArduinoJson.toml"),
            "exclude_dirs = [\"internal\"]\nshim_only = false\n").unwrap();
        let headers: BTreeSet<String> =
            ["ArduinoJson.h"].iter().map(|s| s.to_string()).collect();
        let resolved = resolve(&headers, &fixture_libs(), dir.path(), &[]).unwrap();
        assert_eq!(resolved[0].exclude_dirs, vec!["internal"]);
    }
}
