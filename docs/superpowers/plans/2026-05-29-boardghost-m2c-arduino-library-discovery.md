# BoardGhost M2.C — Arduino Library Auto-Discovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make boardghost build any Arduino sketch whose libraries are installed via `arduino-cli lib install`, by auto-discovering each library's source dir and injecting its includes + sources into the generated CMakeLists.txt — replacing the static allowlist.

**Architecture:**
1. New `arduino_libs` module queries `arduino-cli lib list --format json` to enumerate installed libraries.
2. New `include_scan` module walks the sketch + co-located files, extracts `#include <name>` lines, and maps each header back to a library (via `provides_includes` lookup with a fallback header-search).
3. New `library_overrides` module loads per-library `.toml` config from `runtime/library_overrides/<name>.toml` to exclude hardware-platform dirs/files or add sim-shim dirs.
4. Codegen extends the CMake template with library include paths and per-library source globs (honouring overrides).
5. The static `ALLOWLIST` becomes a "known-good" fast path — sketches using only allowlisted libs skip discovery; sketches with anything else go through the new path. `UnsupportedLibrary` becomes `LibraryNotInstalled` with an actionable arduino-cli command.
6. We ship an ArduinoWebsockets header-only shim (matches the M2.B WiFi/HTTPClient pattern) so cryptoTickerv3-class projects compile against a fake/real network mode without dragging in platform-specific TCP transports.

**Tech Stack:** Rust (boardghost-cli), serde_json for parsing arduino-cli output, Tera templates, CMake `file(GLOB ...)` with explicit exclusion lists, existing `BOARDGHOST_NET` env var (fake/fail/real) for shimmed libs.

---

## File Structure

**Create:**
- `crates/boardghost-cli/src/arduino_libs.rs` — `arduino-cli lib list --format json` parser; `InstalledLibrary` struct; `list_installed()` function.
- `crates/boardghost-cli/src/include_scan.rs` — walks sketch sources, extracts `#include <name>` and `#include "name"` lines, returns `Vec<String>` of header names.
- `crates/boardghost-cli/src/lib_resolve.rs` — joins scanned includes against `InstalledLibrary` list and per-library overrides; returns `ResolvedLibrary { include_dirs, source_globs, exclude_patterns }` per used library.
- `crates/boardghost-cli/src/library_overrides.rs` — loads `runtime/library_overrides/<name>.toml`; defines `LibraryOverride { exclude_dirs, exclude_files, add_dirs }`.
- `runtime/library_overrides/ArduinoWebsockets.toml` — exclude platform-specific transport dirs.
- `runtime/shims/ArduinoWebsockets.h` — header-only sim shim providing `websockets::WebsocketsClient`, `websockets::WebsocketsMessage`, message-type enums. Routes through `sim_net_mode()`.
- `examples/cryptoticker_smoke/` — minimal smoke-test sketch using `WiFi.h`, `HTTPClient.h`, `ArduinoJson.h`, `ArduinoWebsockets.h` so the M2.C path has CI coverage.
- `tests/cli/library_discovery_test.rs` — integration test that mocks an arduino-cli `lib list` JSON fixture and verifies the resolved include/source lists.

**Modify:**
- `crates/boardghost-cli/src/lib.rs` — export the new modules.
- `crates/boardghost-cli/src/preprocess.rs` — accept resolved library include dirs and pass them through to the preprocess `-I` flags.
- `crates/boardghost-cli/src/codegen.rs` — accept `Vec<ResolvedLibrary>`; pass to template as `library_includes` and `library_sources` arrays.
- `crates/boardghost-cli/templates/CMakeLists.txt.tera` — render `target_include_directories(sketch PRIVATE <library_includes>)` and `file(GLOB ... ) ; target_sources(sketch PRIVATE ${...})` per library, with explicit `list(FILTER ... EXCLUDE REGEX ...)` for override patterns.
- `crates/boardghost-cli/src/build.rs` — call the new discovery pipeline between Stage 2 (preprocess) and Stage 4 (codegen), thread the resolved libraries into both calls.
- `crates/boardghost-cli/src/libraries.rs` — keep `ALLOWLIST` as "shimmed" set; rename `is_allowed` → `is_shimmed`. No longer fails the build for non-shimmed libs.
- `crates/boardghost-cli/src/error.rs` — replace `UnsupportedLibrary` with `LibraryNotInstalled { header, suggested_lib_query }` and add `ArduinoLibListFailed { stderr }`.
- `runtime/CMakeLists.txt` — add the new `ArduinoWebsockets.h` shim path. (Header-only; no source-file changes needed since shim lives in `runtime/shims/`.)
- `docs/superpowers/plans/2026-05-28-boardghost-m2b-iot-stubs.md` — add cross-reference note that the M2.B IoT stubs now coexist with M2.C auto-discovery (shimmed lib short-circuits discovery).
- `README.md` — add "Custom Arduino libraries" section explaining install with `arduino-cli lib install <name>` then `boardghost run`.
- `.github/workflows/ci.yml` — install ArduinoJson via `arduino-cli lib install` and add cryptoticker_smoke to the E2E test matrix.

---

## Decomposition Notes

- Each Rust task is TDD (failing test → minimal impl → green → commit). Most modules are 30–60 LoC plus tests.
- Tasks 1–4 build the discovery library bottom-up; they're independent of pipeline wiring.
- Task 5 wires discovery into preprocess; Task 6 wires it into codegen; Task 7 changes the error model. These three sequence the integration.
- Task 8 ships the ArduinoWebsockets shim + override (the "hard library" example). Task 9 ships the smoke sketch + E2E.
- Task 10 covers docs + CI; Task 11 is the tag.

---

## Task 1: Parse `arduino-cli lib list --format json`

**Files:**
- Create: `crates/boardghost-cli/src/arduino_libs.rs`
- Create: `crates/boardghost-cli/tests/fixtures/arduino_lib_list.json`
- Modify: `crates/boardghost-cli/src/lib.rs` (add `pub mod arduino_libs;`)
- Modify: `crates/boardghost-cli/Cargo.toml` (add `serde_json` dep)

- [ ] **Step 1: Add serde_json to workspace and crate**

In `Cargo.toml` (workspace root), under `[workspace.dependencies]`, after the `toml` line:

```toml
serde_json = "1"
```

In `crates/boardghost-cli/Cargo.toml`, under `[dependencies]`, after `toml.workspace = true`:

```toml
serde_json = { workspace = true }
```

- [ ] **Step 2: Create JSON fixture**

Save this exact content to `crates/boardghost-cli/tests/fixtures/arduino_lib_list.json`:

```json
{
  "installed_libraries": [
    {
      "library": {
        "name": "ArduinoJson",
        "install_dir": "/tmp/Arduino/libraries/ArduinoJson",
        "source_dir": "/tmp/Arduino/libraries/ArduinoJson/src",
        "layout": "recursive",
        "provides_includes": ["ArduinoJson.h", "ArduinoJson.hpp"]
      }
    },
    {
      "library": {
        "name": "Time",
        "install_dir": "/tmp/Arduino/libraries/Time",
        "source_dir": "/tmp/Arduino/libraries/Time",
        "layout": "flat",
        "provides_includes": ["TimeLib.h", "Time.h"]
      }
    }
  ]
}
```

- [ ] **Step 3: Write the failing test**

Create `crates/boardghost-cli/src/arduino_libs.rs` with this test block:

```rust
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
```

In `crates/boardghost-cli/src/lib.rs`, add a line near the other `pub mod` declarations:

```rust
pub mod arduino_libs;
```

In `crates/boardghost-cli/src/error.rs`, add a new variant before the closing `}`:

```rust
    #[error("arduino-cli lib list failed: {stderr}")]
    ArduinoLibListFailed { stderr: String },
```

- [ ] **Step 4: Run test, verify it fails (before module exists)**

Run from project root:

```bash
cargo test -p boardghost-cli arduino_libs
```

Expected: First run will compile-fail because `BoardGhostError::ArduinoLibListFailed` is a new variant. Fix any compile errors by ensuring the variant is added exactly as shown.

- [ ] **Step 5: Run test, verify it passes**

```bash
cargo test -p boardghost-cli arduino_libs
```

Expected: 2 tests pass.

- [ ] **Step 6: Commit**

```bash
git add crates/boardghost-cli/src/arduino_libs.rs \
        crates/boardghost-cli/src/lib.rs \
        crates/boardghost-cli/src/error.rs \
        crates/boardghost-cli/tests/fixtures/arduino_lib_list.json \
        crates/boardghost-cli/Cargo.toml \
        Cargo.toml \
        Cargo.lock
git commit -m "feat(cli): parse arduino-cli lib list --format json"
```

---

## Task 2: Scan sketch sources for `#include` directives

**Files:**
- Create: `crates/boardghost-cli/src/include_scan.rs`
- Modify: `crates/boardghost-cli/src/lib.rs` (add `pub mod include_scan;`)

- [ ] **Step 1: Write the failing test**

Create `crates/boardghost-cli/src/include_scan.rs`:

```rust
use std::collections::BTreeSet;
use std::path::Path;

/// Scans a single source file for `#include <name.h>` and `#include "name.h"`
/// directives. Returns the bare header names (no angle brackets, no quotes).
///
/// Lines that are commented out (`//` or inside `/* */`) are NOT filtered —
/// preprocessor-level commenting is out of scope for this scanner. Real builds
/// will tolerate over-inclusion; only failure mode is "library declared but
/// not used", which is harmless.
pub fn scan_file(path: &Path) -> std::io::Result<BTreeSet<String>> {
    let contents = std::fs::read_to_string(path)?;
    Ok(scan_text(&contents))
}

/// Same as scan_file, but for an already-loaded string.
pub fn scan_text(contents: &str) -> BTreeSet<String> {
    let mut out = BTreeSet::new();
    for line in contents.lines() {
        let trimmed = line.trim_start();
        if !trimmed.starts_with("#include") {
            continue;
        }
        let rest = trimmed.trim_start_matches("#include").trim_start();
        let header = if let Some(stripped) = rest.strip_prefix('<') {
            stripped.split('>').next()
        } else if let Some(stripped) = rest.strip_prefix('"') {
            stripped.split('"').next()
        } else {
            None
        };
        if let Some(h) = header {
            out.insert(h.to_string());
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_angle_and_quoted_includes() {
        let text = "
            #include <WiFi.h>
            #include <ArduinoJson.h>
              #include \"my_local.h\"
            int main() {}
        ";
        let result = scan_text(text);
        assert!(result.contains("WiFi.h"));
        assert!(result.contains("ArduinoJson.h"));
        assert!(result.contains("my_local.h"));
        assert_eq!(result.len(), 3);
    }

    #[test]
    fn ignores_lines_without_include() {
        let text = "int x = 0;\nclass Foo {};\n";
        assert!(scan_text(text).is_empty());
    }

    #[test]
    fn dedup_by_set() {
        let text = "#include <A.h>\n#include <A.h>\n";
        assert_eq!(scan_text(text).len(), 1);
    }
}
```

In `crates/boardghost-cli/src/lib.rs`, after `pub mod arduino_libs;`:

```rust
pub mod include_scan;
```

- [ ] **Step 2: Run test, verify it passes**

```bash
cargo test -p boardghost-cli include_scan
```

Expected: 3 tests pass.

- [ ] **Step 3: Commit**

```bash
git add crates/boardghost-cli/src/include_scan.rs \
        crates/boardghost-cli/src/lib.rs
git commit -m "feat(cli): scan sketch sources for #include directives"
```

---

## Task 3: Library-override config loader

**Files:**
- Create: `crates/boardghost-cli/src/library_overrides.rs`
- Create: `runtime/library_overrides/.gitkeep`
- Modify: `crates/boardghost-cli/src/lib.rs` (add `pub mod library_overrides;`)

- [ ] **Step 1: Write the failing test**

Create `crates/boardghost-cli/src/library_overrides.rs`:

```rust
use serde::Deserialize;
use std::path::{Path, PathBuf};

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
```

In `crates/boardghost-cli/src/lib.rs`:

```rust
pub mod library_overrides;
```

Create `runtime/library_overrides/.gitkeep` (empty file) so the dir is tracked by git:

```bash
mkdir -p runtime/library_overrides
touch runtime/library_overrides/.gitkeep
```

- [ ] **Step 2: Run test, verify it passes**

```bash
cargo test -p boardghost-cli library_overrides
```

Expected: 3 tests pass.

- [ ] **Step 3: Commit**

```bash
git add crates/boardghost-cli/src/library_overrides.rs \
        crates/boardghost-cli/src/lib.rs \
        runtime/library_overrides/.gitkeep
git commit -m "feat(cli): library override config loader (per-lib exclude/shim)"
```

---

## Task 4: Resolve scanned headers against installed libraries

**Files:**
- Create: `crates/boardghost-cli/src/lib_resolve.rs`
- Modify: `crates/boardghost-cli/src/lib.rs` (add `pub mod lib_resolve;`)
- Modify: `crates/boardghost-cli/src/error.rs` (add `LibraryNotInstalled`)

- [ ] **Step 1: Update error variants AND remove `filter_allowed` (which references the dropped variant)**

In `crates/boardghost-cli/src/error.rs`, replace the existing `UnsupportedLibrary` variant with two new variants (drop the old one):

```rust
    #[error("Header <{header}> is not provided by any installed Arduino library.\n  \
             Try: arduino-cli lib search <name> && arduino-cli lib install <name>\n  \
             Or shim it under runtime/shims/.")]
    LibraryNotInstalled { header: String },

    #[error("Library {name} resolved but no source files were globbed (check library_overrides/{name}.toml).")]
    LibraryEmpty { name: String },
```

Since `crates/boardghost-cli/src/libraries.rs`'s `filter_allowed` references `UnsupportedLibrary`, replace the full file content with this trimmed version (the full migration happens in Task 7; for now we just need it to compile):

```rust
/// Libraries that BoardGhost ships header-only shims for (under runtime/shims/).
pub const ALLOWLIST: &[&str] = &[
    "LovyanGFX",
    "lvgl",
    "Adafruit_GFX",
    "WiFi",
    "WiFiClient",
    "WiFiClientSecure",
    "HTTPClient",
    "EEPROM",
    "FS",
    "SPIFFS",
    "LittleFS",
    "SD",
    "TinyGSM",
    "StreamDebugger",
];

pub fn is_shimmed(lib: &str) -> bool {
    ALLOWLIST.iter().any(|a| a.eq_ignore_ascii_case(lib))
}

// Note: `filter_allowed` was removed in M2.C — discovery happens in
// `lib_resolve::resolve` instead. `is_allowed` was renamed to `is_shimmed`
// since shimmed libs short-circuit discovery (our shims win in shims/).
```

Verify no other callsite references the removed function or variant:

```bash
grep -rn -e "filter_allowed" -e "UnsupportedLibrary" -e "is_allowed" crates/
```

Expected: zero matches.

- [ ] **Step 2: Write the failing test**

Create `crates/boardghost-cli/src/lib_resolve.rs`:

```rust
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
    // Anything starting with these prefixes is provided by arduino-cli's core
    // or the C++ stdlib — no library to resolve.
    const CORE_PREFIXES: &[&str] = &[
        "Arduino.h", "Wire.h", "SPI.h", "Serial.h",
        "stdint.h", "stddef.h", "stdio.h", "string.h", "math.h",
    ];
    CORE_PREFIXES.iter().any(|p| header.eq_ignore_ascii_case(p))
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
```

In `crates/boardghost-cli/src/lib.rs`:

```rust
pub mod lib_resolve;
```

- [ ] **Step 3: Run test, verify it passes**

```bash
cargo test -p boardghost-cli lib_resolve
```

Expected: 5 tests pass.

- [ ] **Step 4: Commit**

```bash
git add crates/boardghost-cli/src/lib_resolve.rs \
        crates/boardghost-cli/src/lib.rs \
        crates/boardghost-cli/src/error.rs \
        crates/boardghost-cli/src/libraries.rs
git commit -m "feat(cli): resolve sketch headers to installed libraries"
```

---

## Task 5: Pipe discovery into preprocess.rs

**Files:**
- Modify: `crates/boardghost-cli/src/preprocess.rs`

- [ ] **Step 1: Read the current preprocess signature**

Run:

```bash
cat crates/boardghost-cli/src/preprocess.rs
```

The current signature is:

```rust
pub fn preprocess(
    sketch: &Path,
    fqbn: &str,
    extra_include_dirs: &[PathBuf],
) -> Result<PathBuf, BoardGhostError>
```

This already accepts `extra_include_dirs`. The caller in `build.rs` populates it with runtime shim dirs. We just need to make sure the resolved library include dirs are appended by the caller. **No signature change needed here.**

- [ ] **Step 2: Write a regression test confirming extra includes are propagated**

In `crates/boardghost-cli/src/preprocess.rs`, append at the end of the file:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    // We can't easily test the full arduino-cli invocation in unit tests, but
    // we can verify the extra-flags string-building is correct.
    #[test]
    fn builds_extra_flags_from_include_dirs() {
        let dirs = vec![
            PathBuf::from("/a/b"),
            PathBuf::from("/c/d"),
        ];
        let flags: String = dirs.iter()
            .filter_map(|d| d.to_str())
            .map(|d| format!("-I{d}"))
            .collect::<Vec<_>>()
            .join(" ");
        assert_eq!(flags, "-I/a/b -I/c/d");
    }

    #[test]
    fn builds_empty_flags_for_empty_dirs() {
        let dirs: Vec<PathBuf> = vec![];
        let flags: String = dirs.iter()
            .filter_map(|d| d.to_str())
            .map(|d| format!("-I{d}"))
            .collect::<Vec<_>>()
            .join(" ");
        assert!(flags.is_empty());
    }
}
```

- [ ] **Step 3: Run, verify pass**

```bash
cargo test -p boardghost-cli preprocess
```

Expected: 2 tests pass.

- [ ] **Step 4: Commit**

```bash
git add crates/boardghost-cli/src/preprocess.rs
git commit -m "test(cli): preprocess extra-flags builder"
```

---

## Task 6: Extend codegen to render per-library includes + sources

**Files:**
- Modify: `crates/boardghost-cli/src/codegen.rs`
- Modify: `crates/boardghost-cli/templates/CMakeLists.txt.tera`

- [ ] **Step 1: Extend CodegenInput and pass resolved libs through**

Replace the entire content of `crates/boardghost-cli/src/codegen.rs` with:

```rust
use anyhow::{anyhow, Result};
use std::path::PathBuf;
use tera::{Context, Tera};

use crate::BoardProfile;
use crate::lib_resolve::ResolvedLibrary;

const TEMPLATE: &str = include_str!("../templates/CMakeLists.txt.tera");

pub struct CodegenInput<'a> {
    pub sketch_cpp:  PathBuf,
    pub board:       &'a BoardProfile,
    pub runtime_dir: PathBuf,
    pub release:     bool,
    pub out_dir:     PathBuf,
    pub libraries:   Vec<ResolvedLibrary>,
}

#[derive(serde::Serialize)]
struct LibraryCtx {
    name: String,
    source_dir: String,
    glob_patterns: Vec<String>,
    exclude_dirs: Vec<String>,
    exclude_files: Vec<String>,
    add_include_dirs: Vec<String>,
    shim_only: bool,
}

pub fn generate_cmake(input: &CodegenInput) -> Result<PathBuf> {
    let mut tera = Tera::default();
    tera.add_raw_template("cml", TEMPLATE)?;

    let runtime_dir_str = input.runtime_dir.to_string_lossy().to_string();
    let libs_ctx: Vec<LibraryCtx> = input.libraries.iter().map(|l| LibraryCtx {
        name: l.name.clone(),
        source_dir: l.source_dir.to_string_lossy().to_string(),
        glob_patterns: l.glob_patterns.clone(),
        exclude_dirs: l.exclude_dirs.clone(),
        exclude_files: l.exclude_files.clone(),
        add_include_dirs: l.add_include_dirs.iter()
            .map(|s| s.replace("${BOARDGHOST_RUNTIME_DIR}", &runtime_dir_str))
            .collect(),
        shim_only: l.shim_only,
    }).collect();

    let mut ctx = Context::new();
    ctx.insert("sketch_cpp",     &input.sketch_cpp.to_string_lossy());
    ctx.insert("runtime_dir",    &runtime_dir_str);
    ctx.insert("board_name",     &input.board.name);
    ctx.insert("display_width",  &input.board.display.width);
    ctx.insert("display_height", &input.board.display.height);
    ctx.insert("release",        &input.release);
    ctx.insert("libraries",      &libs_ctx);

    let rendered = tera.render("cml", &ctx)?;
    let out = input.out_dir.join("CMakeLists.txt");
    std::fs::write(&out, rendered)
        .map_err(|e| anyhow!("write {:?}: {e}", out))?;
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::board::{BoardProfile, DisplayConfig};
    use tempfile::tempdir;

    fn fake_board() -> BoardProfile {
        BoardProfile {
            name: "test-board".into(),
            description: "test".into(),
            arduino_fqbn_hint: "esp32:esp32:esp32".into(),
            display: DisplayConfig {
                controller: "ST7789".into(),
                width: 240,
                height: 320,
                rotation: 0,
                bus: "SPI".into(),
                color_depth: 16,
            },
            touch: None,
        }
    }

    #[test]
    fn renders_template_with_no_libraries() {
        let dir = tempdir().unwrap();
        let board = fake_board();
        let out_dir = dir.path().to_path_buf();
        let result = generate_cmake(&CodegenInput {
            sketch_cpp: PathBuf::from("/tmp/sketch.cpp"),
            board: &board,
            runtime_dir: PathBuf::from("/tmp/runtime"),
            release: false,
            out_dir: out_dir.clone(),
            libraries: vec![],
        }).unwrap();
        let content = std::fs::read_to_string(&result).unwrap();
        assert!(content.contains("/tmp/sketch.cpp"));
        assert!(content.contains("test-board"));
        // No library blocks when libraries is empty.
        assert!(!content.contains("# Library:"));
    }

    #[test]
    fn renders_library_block() {
        let dir = tempdir().unwrap();
        let board = fake_board();
        let lib = ResolvedLibrary {
            name: "ArduinoJson".into(),
            source_dir: PathBuf::from("/tmp/lib/ArduinoJson/src"),
            glob_patterns: vec!["**/*.cpp".into()],
            exclude_dirs: vec!["internal".into()],
            exclude_files: vec![],
            add_include_dirs: vec![],
            shim_only: false,
        };
        let result = generate_cmake(&CodegenInput {
            sketch_cpp: PathBuf::from("/tmp/sketch.cpp"),
            board: &board,
            runtime_dir: PathBuf::from("/tmp/runtime"),
            release: false,
            out_dir: dir.path().to_path_buf(),
            libraries: vec![lib],
        }).unwrap();
        let content = std::fs::read_to_string(&result).unwrap();
        assert!(content.contains("# Library: ArduinoJson"));
        assert!(content.contains("/tmp/lib/ArduinoJson/src"));
        assert!(content.contains("**/*.cpp"));
    }

    #[test]
    fn substitutes_runtime_dir_in_add_include_dirs() {
        let dir = tempdir().unwrap();
        let board = fake_board();
        let lib = ResolvedLibrary {
            name: "Foo".into(),
            source_dir: PathBuf::from("/tmp/lib/Foo/src"),
            glob_patterns: vec![],
            exclude_dirs: vec![],
            exclude_files: vec![],
            add_include_dirs: vec!["${BOARDGHOST_RUNTIME_DIR}/shims/sim_ws".into()],
            shim_only: true,
        };
        let result = generate_cmake(&CodegenInput {
            sketch_cpp: PathBuf::from("/tmp/sketch.cpp"),
            board: &board,
            runtime_dir: PathBuf::from("/abs/runtime"),
            release: false,
            out_dir: dir.path().to_path_buf(),
            libraries: vec![lib],
        }).unwrap();
        let content = std::fs::read_to_string(&result).unwrap();
        assert!(content.contains("/abs/runtime/shims/sim_ws"));
        assert!(!content.contains("${BOARDGHOST_RUNTIME_DIR}"));
    }
}
```

- [ ] **Step 2: Update the Tera template**

Replace `crates/boardghost-cli/templates/CMakeLists.txt.tera` with:

```cmake
# Generated by boardghost. Do not edit.
cmake_minimum_required(VERSION 3.20)
project(boardghost_sketch CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BOARDGHOST_RUNTIME_DIR "{{ runtime_dir }}" CACHE PATH "")
add_subdirectory(${BOARDGHOST_RUNTIME_DIR} ${CMAKE_BINARY_DIR}/runtime_build)

add_executable(sketch
    {{ sketch_cpp }}
    $<TARGET_OBJECTS:sim_main>
)

target_link_libraries(sketch PRIVATE sim_runtime)

target_compile_definitions(sketch PRIVATE
    BOARD_PROFILE_NAME="{{ board_name }}"
    BOARD_DISPLAY_WIDTH={{ display_width }}
    BOARD_DISPLAY_HEIGHT={{ display_height }}
)

{% if release %}target_compile_options(sketch PRIVATE -O2){% else %}target_compile_options(sketch PRIVATE -O0 -g){% endif %}

# Auto-discovered Arduino libraries (M2.C).
# Each library contributes include dirs and (unless shim_only) a glob of sources.
# Warnings from third-party Arduino libs are suppressed so they don't drown out the user's.
{% for lib in libraries %}
# Library: {{ lib.name }}
target_include_directories(sketch PRIVATE
    "{{ lib.source_dir }}"
{% for inc in lib.add_include_dirs %}    "{{ inc }}"
{% endfor %})

{% if not lib.shim_only %}{% if lib.glob_patterns %}file(GLOB_RECURSE LIB_{{ loop.index }}_SOURCES
{% for pat in lib.glob_patterns %}    "{{ lib.source_dir }}/{{ pat }}"
{% endfor %})
{% for excl_dir in lib.exclude_dirs %}list(FILTER LIB_{{ loop.index0 }}_SOURCES EXCLUDE REGEX "{{ lib.source_dir }}/{{ excl_dir }}/.*")
{% endfor %}{% for excl_file in lib.exclude_files %}list(FILTER LIB_{{ loop.index0 }}_SOURCES EXCLUDE REGEX "{{ lib.source_dir }}/{{ excl_file }}$")
{% endfor %}
if(LIB_{{ loop.index }}_SOURCES)
    target_sources(sketch PRIVATE ${LIB_{{ loop.index }}_SOURCES})
endif()
{% endif %}{% endif %}
{% endfor %}

# Suppress warnings on third-party Arduino library sources but keep them on for the sketch.
if(libraries)
    set_source_files_properties(${ALL_ARDUINO_LIB_SOURCES} PROPERTIES COMPILE_FLAGS "-w")
endif()
```

Note: the Tera loop indices look duplicated (`loop.index` and `loop.index0`); we use `loop.index` (1-based) for the variable name and `loop.index0` (0-based) for the FILTER lines — but Tera variable names need to be consistent. Simpler: use `loop.index` throughout. **Revise the template** so FILTER lines reference `LIB_{{ loop.index }}_SOURCES`:

Replace the filter lines:

```cmake
{% for excl_dir in lib.exclude_dirs %}list(FILTER LIB_{{ loop.index }}_SOURCES EXCLUDE REGEX "{{ lib.source_dir }}/{{ excl_dir }}/.*")
{% endfor %}{% for excl_file in lib.exclude_files %}list(FILTER LIB_{{ loop.index }}_SOURCES EXCLUDE REGEX "{{ lib.source_dir }}/{{ excl_file }}$")
{% endfor %}
```

Note: `loop.index` inside a nested `{% for %}` refers to the INNER loop. Use Tera's `{% set_global %}` or store the outer index as a variable. Simplest fix: use the library's NAME slug in the variable name. Replace `LIB_{{ loop.index }}_SOURCES` with `LIB_{{ lib.name }}_SOURCES` everywhere in the for-block (Tera renders this verbatim; CMake accepts hyphens/underscores in variable names but not dots, so library names must be sanitised).

**Simpler approach — sanitise in codegen.rs:** add a `cmake_var_name` field to `LibraryCtx`:

```rust
#[derive(serde::Serialize)]
struct LibraryCtx {
    name: String,
    cmake_var_name: String,   // <-- new: name sanitised for CMake variables
    source_dir: String,
    glob_patterns: Vec<String>,
    exclude_dirs: Vec<String>,
    exclude_files: Vec<String>,
    add_include_dirs: Vec<String>,
    shim_only: bool,
}
```

And populate it in the `iter().map()`:

```rust
let cmake_var_name = format!("LIB_{}_SOURCES",
    l.name.chars().map(|c| if c.is_alphanumeric() { c } else { '_' }).collect::<String>());
```

Then in the template use `{{ lib.cmake_var_name }}` instead of `LIB_{{ loop.index }}_SOURCES`. Update the `Step 1` codegen.rs code to include `cmake_var_name`.

Update the LibraryCtx-build map to:

```rust
let libs_ctx: Vec<LibraryCtx> = input.libraries.iter().map(|l| {
    let cmake_var_name = format!("LIB_{}_SOURCES",
        l.name.chars().map(|c| if c.is_alphanumeric() { c } else { '_' }).collect::<String>());
    LibraryCtx {
        name: l.name.clone(),
        cmake_var_name,
        source_dir: l.source_dir.to_string_lossy().to_string(),
        glob_patterns: l.glob_patterns.clone(),
        exclude_dirs: l.exclude_dirs.clone(),
        exclude_files: l.exclude_files.clone(),
        add_include_dirs: l.add_include_dirs.iter()
            .map(|s| s.replace("${BOARDGHOST_RUNTIME_DIR}", &runtime_dir_str))
            .collect(),
        shim_only: l.shim_only,
    }
}).collect();
```

Final template version (replace ENTIRE file content):

```cmake
# Generated by boardghost. Do not edit.
cmake_minimum_required(VERSION 3.20)
project(boardghost_sketch CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BOARDGHOST_RUNTIME_DIR "{{ runtime_dir }}" CACHE PATH "")
add_subdirectory(${BOARDGHOST_RUNTIME_DIR} ${CMAKE_BINARY_DIR}/runtime_build)

add_executable(sketch
    {{ sketch_cpp }}
    $<TARGET_OBJECTS:sim_main>
)

target_link_libraries(sketch PRIVATE sim_runtime)

target_compile_definitions(sketch PRIVATE
    BOARD_PROFILE_NAME="{{ board_name }}"
    BOARD_DISPLAY_WIDTH={{ display_width }}
    BOARD_DISPLAY_HEIGHT={{ display_height }}
)

{% if release %}target_compile_options(sketch PRIVATE -O2){% else %}target_compile_options(sketch PRIVATE -O0 -g){% endif %}

# === M2.C: Auto-discovered Arduino libraries ===
{% for lib in libraries %}
# Library: {{ lib.name }}
target_include_directories(sketch PRIVATE
    "{{ lib.source_dir }}"
{%- for inc in lib.add_include_dirs %}
    "{{ inc }}"
{%- endfor %}
)

{% if not lib.shim_only and lib.glob_patterns %}
file(GLOB_RECURSE {{ lib.cmake_var_name }}
{%- for pat in lib.glob_patterns %}
    "{{ lib.source_dir }}/{{ pat }}"
{%- endfor %}
)
{%- for excl_dir in lib.exclude_dirs %}
list(FILTER {{ lib.cmake_var_name }} EXCLUDE REGEX "{{ lib.source_dir }}/{{ excl_dir }}/.*")
{%- endfor %}
{%- for excl_file in lib.exclude_files %}
list(FILTER {{ lib.cmake_var_name }} EXCLUDE REGEX "{{ lib.source_dir }}/{{ excl_file }}$")
{%- endfor %}

if({{ lib.cmake_var_name }})
    target_sources(sketch PRIVATE ${% raw %}{{% endraw %}{{ lib.cmake_var_name }}{% raw %}}{% endraw %})
    set_source_files_properties(${% raw %}{{% endraw %}{{ lib.cmake_var_name }}{% raw %}}{% endraw %} PROPERTIES COMPILE_FLAGS "-w")
endif()
{% endif %}
{% endfor %}
```

(Tera's `{% raw %}` escapes `{` and `}` so CMake gets `${VAR}`.)

- [ ] **Step 3: Update call site in build.rs**

We'll wire the call site fully in Task 7, but to keep the crate compiling we need to update the `generate_cmake` call. Open `crates/boardghost-cli/src/build.rs`. Find:

```rust
let _ = codegen::generate_cmake(&codegen::CodegenInput {
    sketch_cpp:  sketch_cpp_abs,
    board:       &board,
    runtime_dir: runtime_abs.clone(),
    release,
    out_dir:     out_dir.clone(),
})?;
```

Replace with:

```rust
let _ = codegen::generate_cmake(&codegen::CodegenInput {
    sketch_cpp:  sketch_cpp_abs,
    board:       &board,
    runtime_dir: runtime_abs.clone(),
    release,
    out_dir:     out_dir.clone(),
    libraries:   vec![],  // wired in Task 7
})?;
```

- [ ] **Step 4: Run codegen tests**

```bash
cargo test -p boardghost-cli codegen
```

Expected: 3 tests pass.

- [ ] **Step 5: Verify the rest of the crate still compiles**

```bash
cargo build -p boardghost-cli
```

Expected: clean build (warnings about the deprecated `filter_allowed` are fine — Task 7 removes it).

- [ ] **Step 6: Commit**

```bash
git add crates/boardghost-cli/src/codegen.rs \
        crates/boardghost-cli/templates/CMakeLists.txt.tera \
        crates/boardghost-cli/src/build.rs
git commit -m "feat(cli): codegen renders per-library includes + sources from ResolvedLibrary"
```

---

## Task 7: Wire discovery into build.rs (the integration task)

**Files:**
- Modify: `crates/boardghost-cli/src/build.rs`
- Modify: `crates/boardghost-cli/src/libraries.rs` (remove deprecated `filter_allowed`)

- [ ] **Step 1: Replace build.rs body with the discovery-aware pipeline**

Replace the entire contents of `crates/boardghost-cli/src/build.rs` with:

```rust
use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

use crate::{
    arduino_libs, board::BoardProfile, codegen, compile, discover, include_scan,
    lib_resolve, libraries, preprocess,
};

pub struct BuildResult {
    pub binary: PathBuf,
}

pub fn run_build(
    project: &Path,
    board_name: &str,
    boards_dir: &Path,
    runtime_dir: &Path,
    release: bool,
) -> Result<BuildResult> {
    // Stage 1: Discover sketch
    let discovered = discover::discover(project)
        .context("Stage 1: discover")?;
    eprintln!("→ Sketch: {:?}", discovered.entry);

    let board = BoardProfile::load_by_name(boards_dir, board_name)
        .context("loading board profile")?;
    eprintln!("→ Board:  {} ({})", board.name, board.description);

    // Stage 2a: Scan sketch for #include directives BEFORE preprocess so we can
    // tell arduino-cli where the user's libraries live (otherwise preprocess
    // fails on TimeLib.h-style "not found" errors).
    let headers = include_scan::scan_file(&discovered.entry)
        .with_context(|| format!("scan {:?}", discovered.entry))?;
    eprintln!("→ Headers: {} includes scanned", headers.len());

    // Stage 2b: Resolve headers → installed libraries (skipping shimmed ones).
    let installed = arduino_libs::list_installed()
        .context("arduino-cli lib list failed; install arduino-cli or check it's in PATH")?;
    let overrides_dir = runtime_dir.join("library_overrides");
    let resolved = lib_resolve::resolve(
        &headers,
        &installed,
        &overrides_dir,
        libraries::ALLOWLIST,
    ).context("Stage 2b: library resolve")?;
    if !resolved.is_empty() {
        eprintln!("→ Auto-discovered libraries:");
        for r in &resolved {
            eprintln!("    • {} ({})", r.name, r.source_dir.display());
        }
    }

    // Stage 2c: Preprocess via arduino-cli, with both runtime shim dirs AND
    // discovered library include dirs.
    eprintln!("→ Preprocessing via arduino-cli...");
    let runtime_abs = runtime_dir.canonicalize()
        .with_context(|| format!("canonicalize runtime dir {:?}", runtime_dir))?;
    let mut extra_includes = vec![
        runtime_abs.join("displays"),
        runtime_abs.join("shims"),
        runtime_abs.join("third_party/LovyanGFX/src"),
        runtime_abs.join("third_party/lvgl"),
        runtime_abs.join("include"),
    ];
    for lib in &resolved {
        extra_includes.push(lib.source_dir.clone());
        // Also add any post-substitution add_include_dirs (these reach absolute paths).
        for inc in &lib.add_include_dirs {
            let path = inc.replace("${BOARDGHOST_RUNTIME_DIR}", &runtime_abs.to_string_lossy());
            extra_includes.push(PathBuf::from(path));
        }
    }
    let preprocessed = preprocess::preprocess(
        &discovered.entry,
        &board.arduino_fqbn_hint,
        &extra_includes,
    ).context("Stage 2c: preprocess")?;

    // Stage 4: Codegen
    let out_dir = project.join(".boardghost").join(&board.name);
    std::fs::create_dir_all(&out_dir).context("create .boardghost dir")?;
    let sketch_cpp_abs = preprocessed.canonicalize()
        .with_context(|| format!("canonicalize preprocessed file {:?}", preprocessed))?;
    let _ = codegen::generate_cmake(&codegen::CodegenInput {
        sketch_cpp:  sketch_cpp_abs,
        board:       &board,
        runtime_dir: runtime_abs.clone(),
        release,
        out_dir:     out_dir.clone(),
        libraries:   resolved,
    })?;

    // Stage 5: Compile
    eprintln!("→ Compiling...");
    let out = compile::cmake_configure_and_build(&out_dir)?;
    eprintln!("→ Binary: {:?}", out.binary);

    Ok(BuildResult { binary: out.binary })
}
```

- [ ] **Step 2: Add tests to libraries.rs**

libraries.rs was already trimmed in Task 4 (filter_allowed removed, is_shimmed added). Append unit tests:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_libs_are_shimmed() {
        assert!(is_shimmed("WiFi"));
        assert!(is_shimmed("wifi"));        // case-insensitive
        assert!(is_shimmed("HTTPClient"));
        assert!(is_shimmed("LovyanGFX"));
    }

    #[test]
    fn unknown_libs_are_not_shimmed() {
        assert!(!is_shimmed("ArduinoJson"));
        assert!(!is_shimmed("TimeLib"));
    }
}
```

- [ ] **Step 3: Build & run all tests**

```bash
cargo build -p boardghost-cli && cargo test -p boardghost-cli
```

Expected: clean build, all unit tests pass (arduino_libs ×2, include_scan ×3, library_overrides ×3, lib_resolve ×5, codegen ×3, preprocess ×2, libraries ×2 + pre-existing tests).

- [ ] **Step 4: Smoke-test against an existing example**

```bash
cd examples/hello_serial && \
    cargo run -p boardghost-cli -- build --board generic-esp32-st7789 2>&1 | tail -20 && \
    cd ../..
```

Expected: build succeeds. The new `→ Headers: N includes scanned` line should appear; for hello_serial, no libraries need auto-discovery.

- [ ] **Step 5: Commit**

```bash
git add crates/boardghost-cli/src/build.rs \
        crates/boardghost-cli/src/libraries.rs
git commit -m "feat(cli): wire library auto-discovery into build pipeline (M2.C)"
```

---

## Task 8: Ship ArduinoWebsockets shim + override

**Files:**
- Create: `runtime/shims/ArduinoWebsockets.h`
- Create: `runtime/library_overrides/ArduinoWebsockets.toml`

- [ ] **Step 1: Write the override config**

Create `runtime/library_overrides/ArduinoWebsockets.toml`:

```toml
# M2.C: ArduinoWebsockets has hardware-specific TCP transports in
# `tiny_websockets/network/`. We ship a header-only shim
# (`runtime/shims/ArduinoWebsockets.h`) that wins over the real library at
# preprocess time, so the actual library sources are NOT compiled.
shim_only = true
```

- [ ] **Step 2: Write the shim header**

Create `runtime/shims/ArduinoWebsockets.h`. This needs to provide just enough surface area for typical sketch code (`WebsocketsClient`, `onMessage`, `connect`, `poll`, `send`). All ops are no-ops in `FAKE` mode; `REAL` mode logs that real-mode WebSockets are unimplemented (future work).

```cpp
// BoardGhost shim for ArduinoWebsockets (https://github.com/gilmaimon/ArduinoWebsockets).
// In sim builds, this header wins over the real library at preprocess time.
// All operations are routed through sim_net.h so behavior matches the
// BOARDGHOST_NET=fake|fail|real mode.
//
// Surface coverage:
//   - websockets::WebsocketsClient
//   - websockets::WebsocketsMessage
//   - WSInterfaceEvents / WSInterfaceMessages
//   - onMessage(handler), onEvent(handler)
//   - connect(url), close(), poll(), send(), sendBinary(), available()
//
// Real-mode WebSocket support via libcurl is future work; for now REAL falls
// back to FAKE behavior with a one-line stderr notice.
#pragma once

#include <string>
#include <functional>
#include "sim_net.h"   // boardghost_net_mode

namespace websockets {

class WebsocketsMessage {
public:
    WebsocketsMessage() = default;
    explicit WebsocketsMessage(const std::string& data) : data_(data) {}
    const std::string& data() const { return data_; }
    std::string c_str() const { return data_; }
    bool isText() const { return true; }
    bool isBinary() const { return false; }
    bool isPing() const { return false; }
    bool isPong() const { return false; }
    bool isComplete() const { return true; }
    int length() const { return (int)data_.size(); }
private:
    std::string data_;
};

enum class WebsocketsEvent {
    ConnectionOpened,
    ConnectionClosed,
    GotPing,
    GotPong,
};

class WebsocketsClient {
public:
    using MessageCallback = std::function<void(WebsocketsMessage)>;
    using EventCallback   = std::function<void(WebsocketsEvent, std::string)>;

    bool connect(const std::string& url) {
        url_ = url;
        if (sim_net_mode() == BOARDGHOST_NET_FAIL) return false;
        connected_ = true;
        if (event_cb_) event_cb_(WebsocketsEvent::ConnectionOpened, "");
        return true;
    }
    bool connect(const char* url) { return connect(std::string(url)); }
    bool connect(const char* host, int port, const char* path) {
        char url[512];
        snprintf(url, sizeof(url), "ws://%s:%d%s", host, port, path);
        return connect(std::string(url));
    }

    void close() {
        if (connected_ && event_cb_) {
            event_cb_(WebsocketsEvent::ConnectionClosed, "");
        }
        connected_ = false;
    }

    bool available() const { return connected_; }
    bool ping() { return connected_; }

    bool send(const std::string&) { return connected_; }
    bool send(const char*)        { return connected_; }
    bool sendBinary(const char*, size_t) { return connected_; }

    void onMessage(MessageCallback cb) { message_cb_ = std::move(cb); }
    void onEvent(EventCallback cb)     { event_cb_   = std::move(cb); }

    // poll() drives the message pump. In FAKE mode it never delivers messages.
    // REAL mode is currently a no-op (logs once).
    void poll() {
        if (!connected_) return;
        if (sim_net_mode() == BOARDGHOST_NET_REAL) {
            static bool warned = false;
            if (!warned) {
                fprintf(stderr,
                    "[boardghost] WebsocketsClient: REAL mode not yet wired "
                    "(libcurl WS pending) — behaving as FAKE.\n");
                warned = true;
            }
        }
    }

private:
    bool connected_ = false;
    std::string url_;
    MessageCallback message_cb_;
    EventCallback   event_cb_;
};

} // namespace websockets

// Real ArduinoWebsockets exposes this `using` shortcut at top-level too.
using websockets::WebsocketsClient;
using websockets::WebsocketsMessage;
using websockets::WebsocketsEvent;
```

- [ ] **Step 3: Add the shim to runtime/shims path (already covered)**

The shim dir is already in `runtime/CMakeLists.txt`'s `sim_runtime` include path (line 29: `${CMAKE_CURRENT_SOURCE_DIR}/shims`). Header-only — no new source file to add.

But we DO need to add "ArduinoWebsockets" to the ALLOWLIST so library_overrides can be a fall-through and the discovery path skips it. Open `crates/boardghost-cli/src/libraries.rs` and add `"ArduinoWebsockets"` to the ALLOWLIST const:

```rust
    "TinyGSM",
    "StreamDebugger",
    // M2.C — shim wins via header-only override
    "ArduinoWebsockets",
```

- [ ] **Step 4: Smoke test the shim header in isolation**

```bash
echo '#include "runtime/shims/ArduinoWebsockets.h"
int main() {
    using namespace websockets;
    WebsocketsClient c;
    c.onMessage([](WebsocketsMessage m){ (void)m; });
    c.connect("ws://example.com");
    c.poll();
    c.close();
    return 0;
}' > /tmp/ws_shim_test.cpp
g++ -std=c++17 -I runtime/include -I runtime/shims /tmp/ws_shim_test.cpp runtime/src/sim_net.cpp -o /tmp/ws_shim_test 2>&1 | head
/tmp/ws_shim_test
```

Expected: compiles clean, runs and exits 0.

- [ ] **Step 5: Re-run library tests**

```bash
cargo test -p boardghost-cli libraries
```

Expected: 2 tests still pass (ArduinoWebsockets is in the allowlist now).

- [ ] **Step 6: Commit**

```bash
git add runtime/shims/ArduinoWebsockets.h \
        runtime/library_overrides/ArduinoWebsockets.toml \
        crates/boardghost-cli/src/libraries.rs
git commit -m "feat(runtime): ArduinoWebsockets header-only shim + shim_only override"
```

---

## Task 9: cryptoticker_smoke example + E2E

**Files:**
- Create: `examples/cryptoticker_smoke/sketch/sketch.ino`
- Create: `examples/cryptoticker_smoke/README.md`
- Create: `tests/e2e/cryptoticker_smoke.sh`

- [ ] **Step 1: Write a small sketch that exercises the M2.C path**

Create `examples/cryptoticker_smoke/sketch/sketch.ino`:

```cpp
// M2.C smoke test: exercises auto-discovery of ArduinoJson + the
// ArduinoWebsockets shim in fake-net mode. The sketch is intentionally
// minimal — its job is to compile and run cleanly, not to be useful.
//
// Used as a CI gate for the library auto-discovery pipeline.
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

static WebsocketsClient wsClient;
static int loop_count = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("cryptoticker_smoke: setup");

    WiFi.begin("simulated", "simulated");
    while (WiFi.status() != WL_CONNECTED) {
        delay(50);
    }
    Serial.println("WiFi connected (simulated)");

    JsonDocument doc;
    doc["symbol"] = "BTC";
    doc["price"] = 100000.0;
    String body;
    serializeJson(doc, body);
    Serial.print("JSON: ");
    Serial.println(body);

    wsClient.onMessage([](WebsocketsMessage msg) {
        Serial.print("WS msg: ");
        Serial.println(msg.data().c_str());
    });
    wsClient.connect("ws://example.com/socket");
}

void loop() {
    if (wsClient.available()) {
        wsClient.poll();
    }
    if (++loop_count > 200) {
        Serial.println("cryptoticker_smoke: done");
        exit(0);
    }
    delay(10);
}
```

Create `examples/cryptoticker_smoke/README.md`:

```markdown
# cryptoticker_smoke

Smoke test sketch for the M2.C library auto-discovery path. Exercises:
- WiFi shim (M2.B)
- HTTPClient shim (M2.B)
- ArduinoJson via auto-discovery (M2.C)
- ArduinoWebsockets via header-only shim (M2.C)

## Run

```bash
arduino-cli lib install ArduinoJson
boardghost run --board generic-esp32-st7789 examples/cryptoticker_smoke
```

Auto-exits after ~200 loop iterations so it works in headless CI.
```

- [ ] **Step 2: Write the E2E script**

Create `tests/e2e/cryptoticker_smoke.sh`:

```bash
#!/usr/bin/env bash
# M2.C E2E: cryptoticker_smoke must build and run to completion.
set -euo pipefail

cd "$(dirname "$0")/../.."

# Ensure ArduinoJson is installed (CI installs this in the workflow).
if ! arduino-cli lib list ArduinoJson 2>&1 | grep -q ArduinoJson; then
    echo "❌ ArduinoJson not installed. Run: arduino-cli lib install ArduinoJson"
    exit 1
fi

# Build via boardghost.
cargo run -p boardghost-cli --quiet -- build \
    --board generic-esp32-st7789 \
    examples/cryptoticker_smoke

# Run with screenshot to force a clean exit even if SDL doesn't pump.
TMPDIR=$(mktemp -d)
cargo run -p boardghost-cli --quiet -- run \
    --board generic-esp32-st7789 \
    --screenshot "$TMPDIR/shot.png" \
    examples/cryptoticker_smoke \
    > "$TMPDIR/out.log" 2>&1 || true

if grep -q "cryptoticker_smoke: done" "$TMPDIR/out.log"; then
    echo "✅ cryptoticker_smoke E2E passed"
    rm -rf "$TMPDIR"
    exit 0
fi
echo "❌ cryptoticker_smoke did not reach end:"
cat "$TMPDIR/out.log" | tail -40
exit 1
```

Make it executable:

```bash
chmod +x tests/e2e/cryptoticker_smoke.sh
```

- [ ] **Step 3: Install ArduinoJson locally and run**

```bash
arduino-cli lib install ArduinoJson || true
./tests/e2e/cryptoticker_smoke.sh
```

Expected: `✅ cryptoticker_smoke E2E passed` printed.

If `JsonDocument` is unknown (older ArduinoJson API), the smoke sketch may need adjusting. ArduinoJson ≥7.0 has `JsonDocument`; ≥6.x has `DynamicJsonDocument(1024)`. Adjust the sketch to use whichever API matches the installed version:

```bash
arduino-cli lib list ArduinoJson
```

If version is 6.x, change `JsonDocument doc;` to `DynamicJsonDocument doc(1024);`. Otherwise leave as-is.

- [ ] **Step 4: Commit**

```bash
git add examples/cryptoticker_smoke/ \
        tests/e2e/cryptoticker_smoke.sh
git commit -m "test: cryptoticker_smoke E2E for M2.C auto-discovery"
```

---

## Task 10: Wire E2E into CI + update docs

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`

- [ ] **Step 1: Add ArduinoJson install step to CI**

Open `.github/workflows/ci.yml`. Find the `build-and-test` job's "Install arduino-cli" step. After it, add:

```yaml
      - name: Install Arduino libraries for E2E
        run: |
          arduino-cli core install esp32:esp32 || true
          arduino-cli lib install ArduinoJson
```

In the same job, after the existing E2E run step (look for the existing E2E test invocation), add:

```yaml
      - name: E2E — cryptoticker_smoke (M2.C)
        run: ./tests/e2e/cryptoticker_smoke.sh
        env:
          BOARDGHOST_NET: fake
```

- [ ] **Step 2: Document the feature in README.md**

Open `README.md`. Find an appropriate section (likely under "Usage" or before "Architecture"). Add a new section:

````markdown
## Using custom Arduino libraries (M2.C)

BoardGhost can build sketches against any Arduino library installed via
`arduino-cli`. Header-only libraries (ArduinoJson, TimeLib, ...) work
transparently; libraries with platform-specific transports can be shimmed.

```bash
# Install whatever your sketch needs
arduino-cli lib install ArduinoJson Time

# Then build normally
boardghost run --board generic-esp32-st7789 path/to/your/sketch
```

The CLI auto-discovers installed libraries via `arduino-cli lib list`,
maps `#include <...>` directives to libraries via their `provides_includes`
metadata, and injects each library's source dir into the generated
CMakeLists.txt.

### Library overrides

For libraries with hardware-specific code that won't compile against SDL
(e.g. ESP32-only TCP transports), drop a `<libname>.toml` file in
`runtime/library_overrides/`:

```toml
exclude_dirs = ["network/esp32", "network/esp8266"]
exclude_files = ["server.cpp"]
add_include_dirs = ["${BOARDGHOST_RUNTIME_DIR}/shims/sim_ws"]
shim_only = false   # set true if you ship a full header-only replacement
```

Existing overrides:
- `ArduinoWebsockets` — uses BoardGhost's header-only shim
  (`runtime/shims/ArduinoWebsockets.h`); real WebSocket support is future
  work but the shim compiles cleanly.
````

- [ ] **Step 3: Run the full local test suite**

```bash
cargo test -p boardghost-cli
./tests/e2e/cryptoticker_smoke.sh
```

Expected: all green.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml README.md
git commit -m "ci(docs): wire cryptoticker_smoke E2E + custom-libraries usage notes"
```

---

## Task 11: Validate against cryptoTickerv3 (first-contact) + tag

**Files:**
- This task is exploratory — no new files, just verification.

- [ ] **Step 1: Install cryptoTickerv3's dependencies**

```bash
arduino-cli lib install ArduinoJson "ESP32Time" "ArduinoWebsockets" "Time"
arduino-cli lib list
```

Confirm all four are listed.

- [ ] **Step 2: Attempt build of cryptoTickerv3**

```bash
cargo run -p boardghost-cli -- build \
    --board generic-esp32-st7789 \
    /home/phill/Arduino/arduinoProjects/cryptoTickerv3 2>&1 | tee /tmp/cryptov3.log | tail -40
```

Expected outcomes:
- ✅ best case: build succeeds → run it and screenshot.
- 🟡 likely case: build fails on a header from another library not yet installed (e.g. an LGFX user-setup), or on an API mismatch in `JsonDocument` vs `DynamicJsonDocument`, or on a function the ArduinoWebsockets shim doesn't expose yet.

For each failure mode, document the gap. If it's a missing shim API, add it to `runtime/shims/ArduinoWebsockets.h` and re-run. If it's a missing library, install via `arduino-cli lib install` and re-run. If it's something architectural (e.g. the sketch needs `<esp_task_wdt.h>`), document as a "follow-up shim" in the M2.D backlog.

- [ ] **Step 3: Capture findings**

Append a section to `docs/superpowers/plans/2026-05-29-boardghost-m2c-arduino-library-discovery.md` (this file) titled `## cryptoTickerv3 First-Contact Results` with a bullet list of every gap encountered and its resolution (or deferment to M2.D). Be honest — gaps are inputs to M2.D, not failures.

- [ ] **Step 4: Tag the release**

Only if the entire `cargo test` + E2E + CI matrix is green:

```bash
git tag m2c-arduino-libs
git push origin m2c-arduino-libs
```

- [ ] **Step 5: Final commit (if cryptoTickerv3 findings were appended)**

```bash
git add docs/superpowers/plans/2026-05-29-boardghost-m2c-arduino-library-discovery.md
git commit -m "docs(m2c): cryptoTickerv3 first-contact findings"
```

---

## Done criteria

- [ ] All 11 tasks merged on `main`.
- [ ] `cargo test -p boardghost-cli` shows ~22+ unit tests passing (5 new modules × 2-5 tests each, plus pre-existing).
- [ ] `./tests/e2e/cryptoticker_smoke.sh` exits 0 locally and in CI.
- [ ] cryptoTickerv3 either builds cleanly OR has its remaining gaps written up in the plan's `First-Contact Results` section.
- [ ] `m2c-arduino-libs` tag pushed.
- [ ] README.md has the "Using custom Arduino libraries" section.
