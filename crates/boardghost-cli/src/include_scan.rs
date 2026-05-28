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
