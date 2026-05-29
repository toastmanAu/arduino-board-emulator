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

/// Information extracted from the user's LGFX setup file.
#[derive(Debug, Clone, PartialEq)]
pub struct ParsedSetup {
    /// The class name extending `lgfx::LGFX_Device` (e.g. `LGFX`, `MyLGFX`).
    pub class_name: String,
    /// The LovyanGFX panel class (e.g. `Panel_ILI9488`, `Panel_ST7789`).
    pub panel_class: String,
    /// The touch class, if any (e.g. `Touch_XPT2046`).
    pub touch_class: Option<String>,
    /// Panel width in pixels (from `cfg.panel_width = N;`).
    pub panel_width: u32,
    /// Panel height in pixels.
    pub panel_height: u32,
    /// Rotation offset (0..7). Defaults to 0 when not present in the source.
    pub offset_rotation: u8,
}

/// Parse the contents of an LGFX setup file. Returns `Ok(None)` when any of
/// the required fields can't be extracted — the caller must skip the codemod
/// in that case and let the original file be used.
pub fn parse_setup(contents: &str) -> Option<ParsedSetup> {
    use regex::Regex;

    // 1. Class name: matches `class Foo : public lgfx::LGFX_Device`
    let class_re = Regex::new(r"class\s+(\w+)\s*:\s*public\s+lgfx::LGFX_Device").ok()?;
    let class_name = class_re.captures(contents)?.get(1)?.as_str().to_string();

    // 2. Panel class: matches `lgfx::Panel_Xxx _member`
    let panel_re = Regex::new(r"lgfx::Panel_(\w+)\s+\w+").ok()?;
    let panel_class = format!("Panel_{}", panel_re.captures(contents)?.get(1)?.as_str());

    // 3. Touch class (optional): matches `lgfx::Touch_Xxx _member`
    let touch_re = Regex::new(r"lgfx::Touch_(\w+)\s+\w+").ok()?;
    let touch_class = touch_re
        .captures(contents)
        .and_then(|c| c.get(1))
        .map(|m| format!("Touch_{}", m.as_str()));

    // 4. Dimensions: `cfg.panel_width = N;` and `cfg.panel_height = N;`
    let width_re = Regex::new(r"cfg\.panel_width\s*=\s*(\d+)").ok()?;
    let height_re = Regex::new(r"cfg\.panel_height\s*=\s*(\d+)").ok()?;
    let panel_width: u32 = width_re
        .captures(contents)?
        .get(1)?
        .as_str()
        .parse()
        .ok()?;
    let panel_height: u32 = height_re
        .captures(contents)?
        .get(1)?
        .as_str()
        .parse()
        .ok()?;

    // 5. Rotation (optional, default 0): `cfg.offset_rotation = N;`
    let rot_re = Regex::new(r"cfg\.offset_rotation\s*=\s*(\d+)").ok()?;
    let offset_rotation: u8 = rot_re
        .captures(contents)
        .and_then(|c| c.get(1))
        .and_then(|m| m.as_str().parse().ok())
        .unwrap_or(0);

    Some(ParsedSetup {
        class_name,
        panel_class,
        touch_class,
        panel_width,
        panel_height,
        offset_rotation,
    })
}

const SIM_TEMPLATE: &str = include_str!("../templates/lgfx_sim.hpp.tera");

/// Render the parsed setup into a sim-side wrapper source string.
pub fn generate_sim_wrapper(parsed: &ParsedSetup) -> anyhow::Result<String> {
    use tera::{Context, Tera};
    let mut tera = Tera::default();
    tera.add_raw_template("lgfx_sim", SIM_TEMPLATE)?;
    let mut ctx = Context::new();
    ctx.insert("class_name",      &parsed.class_name);
    ctx.insert("panel_width",     &parsed.panel_width);
    ctx.insert("panel_height",    &parsed.panel_height);
    ctx.insert("offset_rotation", &parsed.offset_rotation);
    ctx.insert("has_touch",       &parsed.touch_class.is_some());
    Ok(tera.render("lgfx_sim", &ctx)?)
}

/// Report describing what the codemod did. Returned to the build pipeline so
/// stage logs can show the user what was rewritten.
#[derive(Debug, Clone)]
pub struct CodemodReport {
    /// Relative path (under sketch dir) of the file that got rewritten.
    pub rewritten_rel_path: std::path::PathBuf,
    /// Parsed setup details, surfaced for diagnostics.
    pub parsed: ParsedSetup,
    /// The full sim-side source written into the mirror.
    pub sim_contents: String,
}

/// Try to apply the LGFX codemod to a sketch dir. Returns `Ok(None)` when no
/// hardware setup file is found OR when the parser can't extract enough info
/// to safely generate a sim wrapper. Returns `Err` only on filesystem errors.
pub fn try_apply(sketch_dir: &Path) -> anyhow::Result<Option<CodemodReport>> {
    let Some(setup) = scan_sketch(sketch_dir)? else {
        return Ok(None);
    };
    let Some(parsed) = parse_setup(&setup.contents) else {
        eprintln!(
            "→ LGFX codemod: setup detected at {:?} but couldn't parse — skipping",
            setup.path
        );
        return Ok(None);
    };
    let sim_contents = generate_sim_wrapper(&parsed)?;
    let rel_path = setup.path.strip_prefix(sketch_dir)?.to_path_buf();
    Ok(Some(CodemodReport {
        rewritten_rel_path: rel_path,
        parsed,
        sim_contents,
    }))
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

    #[test]
    fn parses_ili9488_fixture() {
        let contents = include_str!("../tests/fixtures/lvgfx_setup_ili9488.h");
        let parsed = parse_setup(contents).unwrap();
        assert_eq!(parsed.class_name, "LGFX");
        assert_eq!(parsed.panel_class, "Panel_ILI9488");
        assert_eq!(parsed.touch_class.as_deref(), Some("Touch_XPT2046"));
        assert_eq!(parsed.panel_width, 320);
        assert_eq!(parsed.panel_height, 480);
        assert_eq!(parsed.offset_rotation, 2);
    }

    #[test]
    fn parses_st7789_fixture_no_touch_no_rotation() {
        let contents = include_str!("../tests/fixtures/lvgfx_setup_st7789.h");
        let parsed = parse_setup(contents).unwrap();
        assert_eq!(parsed.class_name, "MyLGFX");
        assert_eq!(parsed.panel_class, "Panel_ST7789");
        assert!(parsed.touch_class.is_none());
        assert_eq!(parsed.panel_width, 240);
        assert_eq!(parsed.panel_height, 320);
        assert_eq!(parsed.offset_rotation, 0);
    }

    #[test]
    fn returns_none_when_class_missing() {
        let result = parse_setup("// no class here");
        assert!(result.is_none());
    }

    #[test]
    fn returns_none_when_dimensions_missing() {
        let result = parse_setup(
            "class X : public lgfx::LGFX_Device { lgfx::Panel_Foo p; };",
        );
        assert!(result.is_none());
    }

    #[test]
    fn generates_wrapper_with_dimensions() {
        let parsed = ParsedSetup {
            class_name: "LGFX".into(),
            panel_class: "Panel_ILI9488".into(),
            touch_class: Some("Touch_XPT2046".into()),
            panel_width: 320,
            panel_height: 480,
            offset_rotation: 2,
        };
        let out = generate_sim_wrapper(&parsed).unwrap();
        assert!(out.contains("class LGFX : public lgfx::LGFX_Device"));
        assert!(out.contains("cfg.panel_width     = 320;"));
        assert!(out.contains("cfg.panel_height    = 480;"));
        assert!(out.contains("cfg.offset_rotation = 2;"));
        assert!(out.contains("lgfx::Panel_sdl _panel_instance;"));
        assert!(out.contains("Touch_sdl       _touch_instance;"));
        assert!(out.contains("_panel_instance.setTouch(&_touch_instance);"));
    }

    #[test]
    fn generates_wrapper_without_touch() {
        let parsed = ParsedSetup {
            class_name: "MyLGFX".into(),
            panel_class: "Panel_ST7789".into(),
            touch_class: None,
            panel_width: 240,
            panel_height: 320,
            offset_rotation: 0,
        };
        let out = generate_sim_wrapper(&parsed).unwrap();
        assert!(out.contains("class MyLGFX : public lgfx::LGFX_Device"));
        assert!(!out.contains("Touch_sdl"));
        assert!(!out.contains("setTouch"));
    }
}
