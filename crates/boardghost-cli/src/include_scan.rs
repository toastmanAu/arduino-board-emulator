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

/// Filters `headers` down to those that DON'T exist as files in any of
/// `local_search_dirs`. Used by the build pipeline to skip headers that
/// the arduino-cli preprocessor will find via `-I` flags (project-local
/// files + BoardGhost runtime helper dirs like `runtime/displays/` and
/// `runtime/include/`).
///
/// NOTE: `runtime/shims/` must NOT be a search dir here, because it
/// contains both full shims (handled via `libraries::ALLOWLIST`
/// short-circuit) AND empty stubs where we actively want library
/// auto-discovery to find the real library at compile time.
pub fn filter_local_headers(
    headers: BTreeSet<String>,
    local_search_dirs: &[&Path],
) -> BTreeSet<String> {
    headers.into_iter()
        .filter(|h| !local_search_dirs.iter().any(|d| d.join(h).exists()))
        .collect()
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

    #[test]
    fn filter_drops_locals_keeps_unresolved() {
        // Set up two dirs: sketch_dir has "local.h", runtime/displays has "Disp.h".
        let sketch_dir = tempfile::tempdir().unwrap();
        let displays_dir = tempfile::tempdir().unwrap();
        std::fs::write(sketch_dir.path().join("local.h"), "").unwrap();
        std::fs::write(displays_dir.path().join("Disp.h"), "").unwrap();

        let headers: BTreeSet<String> = [
            "local.h",            // exists in sketch dir → drop
            "Disp.h",             // exists in displays dir → drop
            "ArduinoJson.h",      // doesn't exist anywhere local → keep
            "WiFi.h",             // also kept (shimmed short-circuit happens later)
        ].iter().map(|s| s.to_string()).collect();

        let dirs: &[&Path] = &[sketch_dir.path(), displays_dir.path()];
        let kept = filter_local_headers(headers, dirs);

        assert!(!kept.contains("local.h"), "sketch-dir header should be filtered");
        assert!(!kept.contains("Disp.h"), "displays-dir header should be filtered");
        assert!(kept.contains("ArduinoJson.h"), "unresolved header must pass through");
        assert!(kept.contains("WiFi.h"), "shimmed headers pass through (handled downstream)");
    }
}
