use std::path::{Path, PathBuf};

/// Information about an LGFX hardware-setup file found in a sketch dir.
#[derive(Debug, Clone, PartialEq)]
pub struct SetupFile {
    /// Absolute path to the file on disk.
    pub path: PathBuf,
    /// Raw contents at scan time.
    pub contents: String,
}

/// Search the sketch dir (and any immediate subdir) for a file that contains
/// an LGFX hardware-setup pattern. Returns the first match (sorted by path so
/// results are deterministic).
///
/// A file qualifies when it contains BOTH:
///   - `lgfx::LGFX_Device` (parent class reference), AND
///   - At least one of `lgfx::Bus_SPI`, `lgfx::Panel_`, or `lgfx::Touch_`.
///
/// Files that LovyanGFX-SDL users would already use (those that include
/// `LGFX_*_SDL.hpp`) are skipped: the build pipeline already handles them.
pub fn scan_sketch(sketch_dir: &Path) -> std::io::Result<Option<SetupFile>> {
    let mut candidates: Vec<PathBuf> = Vec::new();
    for entry in std::fs::read_dir(sketch_dir)? {
        let entry = entry?;
        let path = entry.path();
        if path.is_file() && is_header_file(&path) {
            candidates.push(path);
        } else if path.is_dir() {
            for sub_entry in std::fs::read_dir(&path)? {
                let sub_entry = sub_entry?;
                let sub_path = sub_entry.path();
                if sub_path.is_file() && is_header_file(&sub_path) {
                    candidates.push(sub_path);
                }
            }
        }
    }
    candidates.sort();

    for path in candidates {
        let contents = std::fs::read_to_string(&path)?;
        if is_lgfx_setup(&contents) {
            return Ok(Some(SetupFile { path, contents }));
        }
    }
    Ok(None)
}

fn is_header_file(path: &Path) -> bool {
    matches!(
        path.extension().and_then(|s| s.to_str()),
        Some("h") | Some("hpp")
    )
}

fn is_lgfx_setup(contents: &str) -> bool {
    if contents.contains("LGFX_") && contents.contains("_SDL.hpp") {
        // Already an SDL setup — leave alone.
        return false;
    }
    let has_device = contents.contains("lgfx::LGFX_Device");
    let has_hw_class = contents.contains("lgfx::Bus_SPI")
        || contents.contains("lgfx::Panel_")
        || contents.contains("lgfx::Touch_");
    has_device && has_hw_class
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn returns_none_for_empty_dir() {
        let dir = tempdir().unwrap();
        let result = scan_sketch(dir.path()).unwrap();
        assert!(result.is_none());
    }

    #[test]
    fn finds_lvgfx_setup_h_in_root() {
        let dir = tempdir().unwrap();
        fs::write(dir.path().join("sketch.ino"), "// sketch").unwrap();
        fs::write(
            dir.path().join("lvgfx_setup.h"),
            "class LGFX : public lgfx::LGFX_Device {\n  lgfx::Panel_ILI9488 _panel_instance;\n  lgfx::Bus_SPI _bus_instance;\n};",
        ).unwrap();
        let result = scan_sketch(dir.path()).unwrap().unwrap();
        assert_eq!(result.path.file_name().unwrap(), "lvgfx_setup.h");
        assert!(result.contents.contains("Panel_ILI9488"));
    }

    #[test]
    fn finds_setup_in_immediate_subdir() {
        let dir = tempdir().unwrap();
        let sub = dir.path().join("setup");
        fs::create_dir(&sub).unwrap();
        fs::write(
            sub.join("display.h"),
            "class MyLGFX : public lgfx::LGFX_Device {\n  lgfx::Panel_ST7789 _p;\n};",
        ).unwrap();
        let result = scan_sketch(dir.path()).unwrap().unwrap();
        assert_eq!(result.path.file_name().unwrap(), "display.h");
    }

    #[test]
    fn ignores_sdl_setup_files() {
        let dir = tempdir().unwrap();
        fs::write(
            dir.path().join("setup.h"),
            "#include <LGFX_ILI9488_SDL.hpp>\nclass LGFX : public lgfx::LGFX_Device {};",
        ).unwrap();
        let result = scan_sketch(dir.path()).unwrap();
        assert!(result.is_none(), "SDL setups should be skipped");
    }

    #[test]
    fn ignores_unrelated_headers() {
        let dir = tempdir().unwrap();
        fs::write(dir.path().join("utils.h"), "int add(int a, int b);").unwrap();
        fs::write(dir.path().join("config.h"), "#define WIFI_SSID \"x\"").unwrap();
        let result = scan_sketch(dir.path()).unwrap();
        assert!(result.is_none());
    }
}
