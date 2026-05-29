# BoardGhost M2.E — LGFX Codemod Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Real Arduino sketches with custom LGFX hardware setup files (e.g. cryptoTickerv3's `lvgfx_setup.h`) build and render at the correct resolution under BoardGhost simulation — with zero user edits.

**Architecture:**
1. The CLI scans the sketch directory for files containing LGFX hardware setup patterns (`lgfx::Bus_SPI`, `lgfx::Panel_***`, `class … : public lgfx::LGFX_Device`).
2. A regex-based parser extracts the user's LGFX class name, panel/touch types, and the panel dimensions/rotation from the config code.
3. A code generator emits a sim-side replacement: a class with the same name, inheriting `lgfx::LGFX_Device`, configuring `lgfx::Panel_sdl` with the user's dimensions, and attaching `Touch_sdl`.
4. Build-pipeline integration: the CLI mirrors the sketch dir into `.boardghost/<board>/sketch_src/`, overwrites the matched setup file with the generated sim version, then points arduino-cli at the mirror. The user's original file stays untouched on disk.
5. Result: `cryptoTickerv3` compiles + runs end-to-end. The LGFX wall is broken.

**Tech Stack:** Rust (`regex` crate for parsing), existing Tera codegen for the sim wrapper template, existing arduino-cli preprocess pipeline (with sketch-dir redirection added in Stage 2c).

---

## File Structure

**Create:**
- `crates/boardghost-cli/src/lgfx_codemod.rs` — keystone module. `scan_sketch`, `parse_setup`, `generate_sim_wrapper`. Public API: `try_apply(sketch_dir, dest_dir) -> Result<Option<CodemodReport>>`.
- `crates/boardghost-cli/src/sketch_mirror.rs` — shallow recursive copy of a sketch dir into a build cache, with selective overwrites. Public API: `mirror(src, dest, overrides: &[(PathBuf, String)]) -> Result<()>`.
- `crates/boardghost-cli/templates/lgfx_sim.hpp.tera` — Tera template for the generated sim wrapper.
- `crates/boardghost-cli/tests/fixtures/lvgfx_setup_ili9488.h` — test fixture matching the cryptoTickerv3 setup style.
- `crates/boardghost-cli/tests/fixtures/lvgfx_setup_st7789.h` — test fixture for a different panel/dim combo.
- `tests/e2e/cryptoticker_real.sh` — E2E that builds + runs cryptoTickerv3 (gated on the sketch being present at `~/Arduino/arduinoProjects/cryptoTickerv3/`).

**Modify:**
- `crates/boardghost-cli/src/lib.rs` — export `lgfx_codemod` and `sketch_mirror`.
- `crates/boardghost-cli/src/build.rs` — call `lgfx_codemod::try_apply` between Stage 2a (scan) and Stage 2c (preprocess); when a codemod fires, mirror the sketch dir + use the mirror as the preprocess source.
- `crates/boardghost-cli/Cargo.toml` — add `regex` dep.
- `README.md` — add "Custom LGFX setups (M2.E)" section explaining the auto-detection.
- `.github/workflows/ci.yml` — install ESP32 Arduino libs needed for cryptoticker_real test (gated on a synthetic fixture sketch in `examples/`).
- `docs/superpowers/plans/2026-05-29-boardghost-m2c-arduino-library-discovery.md` — cross-reference: this plan closes gap 3 (LGFX panel mismatch) noted in M2.C's first-contact findings.

---

## Decomposition Notes

- Each Rust task uses TDD (failing test → minimal impl → green → commit) and produces a working module that the next task can build on.
- Task 1 (`scan`) is purely filesystem; Task 2 (`parse`) is purely string processing — both are unit-testable with fixtures.
- Task 3 (`generate`) is a Tera render; unit-testable.
- Task 4 (`sketch_mirror`) is independent infrastructure — copy logic with override map.
- Task 5 ties it all together in `build.rs`. This is the integration task that exercises the real arduino-cli pipeline.
- Task 6 is the cryptoTickerv3 end-to-end check.
- Task 7 wires CI + docs.

The codemod is **fail-safe**: if `lgfx_codemod::try_apply` returns `Ok(None)` (no setup file found, or parse failed), the build proceeds with the original sketch unchanged. Existing E2Es (hello_serial, ssd1306_text, lvgl_hello, cryptoticker_smoke) must continue to pass.

---

## Task 1: `lgfx_codemod::scan_sketch` — locate the setup file

**Files:**
- Create: `crates/boardghost-cli/src/lgfx_codemod.rs`
- Modify: `crates/boardghost-cli/src/lib.rs` (add `pub mod lgfx_codemod;`)
- Modify: `crates/boardghost-cli/Cargo.toml` (add `regex` dep)

- [ ] **Step 1: Add regex dep**

In workspace `Cargo.toml`, under `[workspace.dependencies]`:

```toml
regex = "1"
```

In `crates/boardghost-cli/Cargo.toml`, under `[dependencies]`:

```toml
regex = { workspace = true }
```

- [ ] **Step 2: Create the module with scan() and a failing test**

Create `crates/boardghost-cli/src/lgfx_codemod.rs`:

```rust
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
```

In `crates/boardghost-cli/src/lib.rs`, add:

```rust
pub mod lgfx_codemod;
```

- [ ] **Step 3: Run tests**

```bash
cargo test -p boardghost-cli --lib lgfx_codemod::tests::scan
```

Expected: 5 tests pass (`returns_none_for_empty_dir`, `finds_lvgfx_setup_h_in_root`, `finds_setup_in_immediate_subdir`, `ignores_sdl_setup_files`, `ignores_unrelated_headers`).

- [ ] **Step 4: Commit**

```bash
git add crates/boardghost-cli/src/lgfx_codemod.rs \
        crates/boardghost-cli/src/lib.rs \
        crates/boardghost-cli/Cargo.toml \
        Cargo.toml \
        Cargo.lock
git commit -m "feat(cli): lgfx_codemod::scan_sketch — locate LGFX hardware setup files"
```

---

## Task 2: `lgfx_codemod::parse_setup` — extract panel + dims + rotation

**Files:**
- Modify: `crates/boardghost-cli/src/lgfx_codemod.rs` (add `parse_setup` + tests)
- Create: `crates/boardghost-cli/tests/fixtures/lvgfx_setup_ili9488.h`
- Create: `crates/boardghost-cli/tests/fixtures/lvgfx_setup_st7789.h`

- [ ] **Step 1: Write the fixtures**

Create `crates/boardghost-cli/tests/fixtures/lvgfx_setup_ili9488.h`:

```cpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Touch_XPT2046 _touch_instance;

public:
    LGFX(void)
    {
        auto cfg = _panel_instance.config();
        cfg.panel_width = 320;
        cfg.panel_height = 480;
        cfg.offset_rotation = 2;
        _panel_instance.config(cfg);
        setPanel(&_panel_instance);
    }
};
```

Create `crates/boardghost-cli/tests/fixtures/lvgfx_setup_st7789.h`:

```cpp
#include <LovyanGFX.hpp>

class MyLGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    MyLGFX() {
        auto cfg = _panel_instance.config();
        cfg.panel_width  = 240;
        cfg.panel_height = 320;
        cfg.offset_rotation = 0;
        _panel_instance.config(cfg);
        setPanel(&_panel_instance);
    }
};
```

- [ ] **Step 2: Append to lgfx_codemod.rs — ParsedSetup struct + parse_setup function**

Insert AFTER the `scan_sketch` function and BEFORE the `#[cfg(test)]` mod:

```rust
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
    let touch_class = touch_re.captures(contents)
        .and_then(|c| c.get(1))
        .map(|m| format!("Touch_{}", m.as_str()));

    // 4. Dimensions: `cfg.panel_width = N;` and `cfg.panel_height = N;`
    let width_re  = Regex::new(r"cfg\.panel_width\s*=\s*(\d+)").ok()?;
    let height_re = Regex::new(r"cfg\.panel_height\s*=\s*(\d+)").ok()?;
    let panel_width:  u32 = width_re.captures(contents)?.get(1)?.as_str().parse().ok()?;
    let panel_height: u32 = height_re.captures(contents)?.get(1)?.as_str().parse().ok()?;

    // 5. Rotation (optional, default 0): `cfg.offset_rotation = N;`
    let rot_re = Regex::new(r"cfg\.offset_rotation\s*=\s*(\d+)").ok()?;
    let offset_rotation: u8 = rot_re.captures(contents)
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
```

- [ ] **Step 3: Append parser tests to the `#[cfg(test)] mod tests` block**

Append (before the closing `}` of the test mod):

```rust
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
            "class X : public lgfx::LGFX_Device { lgfx::Panel_Foo p; };"
        );
        assert!(result.is_none());
    }
```

- [ ] **Step 4: Run tests**

```bash
cargo test -p boardghost-cli --lib lgfx_codemod::tests::parses
cargo test -p boardghost-cli --lib lgfx_codemod
```

Expected: 9 total tests pass in `lgfx_codemod` (5 scan + 4 parse).

- [ ] **Step 5: Commit**

```bash
git add crates/boardghost-cli/src/lgfx_codemod.rs \
        crates/boardghost-cli/tests/fixtures/lvgfx_setup_ili9488.h \
        crates/boardghost-cli/tests/fixtures/lvgfx_setup_st7789.h
git commit -m "feat(cli): lgfx_codemod::parse_setup — extract panel/dims/rotation via regex"
```

---

## Task 3: `lgfx_codemod::generate_sim_wrapper` — render the sim class

**Files:**
- Modify: `crates/boardghost-cli/src/lgfx_codemod.rs` (add `generate_sim_wrapper` + tests)
- Create: `crates/boardghost-cli/templates/lgfx_sim.hpp.tera`

- [ ] **Step 1: Write the Tera template**

Create `crates/boardghost-cli/templates/lgfx_sim.hpp.tera`:

```cpp
// Generated by boardghost (M2.E LGFX codemod). DO NOT EDIT.
// Replaces the user's hardware LGFX setup with a Panel_sdl-backed wrapper that
// matches the user's panel dimensions ({{ panel_width }}x{{ panel_height }}).
#pragma once
#include <LovyanGFX.hpp>
#include "Touch_sdl.hpp"

class {{ class_name }} : public lgfx::LGFX_Device
{
public:
    {{ class_name }}() {
        auto cfg = _panel_instance.config();
        cfg.memory_width    = {{ panel_width }};
        cfg.memory_height   = {{ panel_height }};
        cfg.panel_width     = {{ panel_width }};
        cfg.panel_height    = {{ panel_height }};
        cfg.offset_x        = 0;
        cfg.offset_y        = 0;
        cfg.offset_rotation = {{ offset_rotation }};
        _panel_instance.config(cfg);
        setPanel(&_panel_instance);
        {% if has_touch %}_panel_instance.setTouch(&_touch_instance);{% endif %}
    }

private:
    lgfx::Panel_sdl _panel_instance;
    {% if has_touch %}Touch_sdl       _touch_instance;{% endif %}
};
```

- [ ] **Step 2: Append generator function to lgfx_codemod.rs**

Insert AFTER `parse_setup` and BEFORE the `#[cfg(test)]` block. Also add `use tera` and a const for the template:

```rust
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
```

- [ ] **Step 3: Append generator tests to the test module**

```rust
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
        assert!(out.contains("cfg.panel_width    = 320;"));
        assert!(out.contains("cfg.panel_height   = 480;"));
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
```

- [ ] **Step 4: Run tests**

```bash
cargo test -p boardghost-cli --lib lgfx_codemod
```

Expected: 11 total tests pass (5 scan + 4 parse + 2 generate).

- [ ] **Step 5: Commit**

```bash
git add crates/boardghost-cli/src/lgfx_codemod.rs \
        crates/boardghost-cli/templates/lgfx_sim.hpp.tera
git commit -m "feat(cli): lgfx_codemod::generate_sim_wrapper — render sim LGFX class"
```

---

## Task 4: `sketch_mirror::mirror` — copy sketch dir with selective overrides

**Files:**
- Create: `crates/boardghost-cli/src/sketch_mirror.rs`
- Modify: `crates/boardghost-cli/src/lib.rs` (add `pub mod sketch_mirror;`)

- [ ] **Step 1: Write the module + failing tests**

Create `crates/boardghost-cli/src/sketch_mirror.rs`:

```rust
use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

/// Recursively copies `src` to `dest`, applying selective content overrides.
///
/// `overrides` is a slice of (path-relative-to-src, new-contents) pairs. Any
/// file in `src` whose canonical relative path matches an override entry is
/// written with the override contents instead of being copied verbatim.
///
/// Pre-existing contents at `dest` are cleared first to keep the mirror
/// deterministic.
pub fn mirror(
    src: &Path,
    dest: &Path,
    overrides: &[(PathBuf, String)],
) -> Result<()> {
    if dest.exists() {
        std::fs::remove_dir_all(dest)
            .with_context(|| format!("clear mirror dest {:?}", dest))?;
    }
    std::fs::create_dir_all(dest)
        .with_context(|| format!("create mirror dest {:?}", dest))?;
    walk(src, src, dest, overrides)
}

fn walk(
    root: &Path,
    cur_src: &Path,
    cur_dest: &Path,
    overrides: &[(PathBuf, String)],
) -> Result<()> {
    for entry in std::fs::read_dir(cur_src)
        .with_context(|| format!("read_dir {:?}", cur_src))?
    {
        let entry = entry?;
        let path = entry.path();
        let name = entry.file_name();
        let dest_path = cur_dest.join(&name);

        if path.is_dir() {
            // Skip .boardghost so we don't recurse into our own build cache.
            if name == ".boardghost" { continue; }
            std::fs::create_dir_all(&dest_path)?;
            walk(root, &path, &dest_path, overrides)?;
        } else if path.is_file() {
            let rel = path.strip_prefix(root).unwrap_or(&path).to_path_buf();
            if let Some((_, contents)) = overrides.iter().find(|(p, _)| p == &rel) {
                std::fs::write(&dest_path, contents)
                    .with_context(|| format!("write override {:?}", dest_path))?;
            } else {
                std::fs::copy(&path, &dest_path)
                    .with_context(|| format!("copy {:?} -> {:?}", path, dest_path))?;
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn mirrors_a_flat_dir() {
        let src = tempdir().unwrap();
        let dest = tempdir().unwrap();
        let dest_dir = dest.path().join("mirror");
        fs::write(src.path().join("a.txt"), "alpha").unwrap();
        fs::write(src.path().join("b.txt"), "beta").unwrap();
        mirror(src.path(), &dest_dir, &[]).unwrap();
        assert_eq!(fs::read_to_string(dest_dir.join("a.txt")).unwrap(), "alpha");
        assert_eq!(fs::read_to_string(dest_dir.join("b.txt")).unwrap(), "beta");
    }

    #[test]
    fn applies_override_for_matched_file() {
        let src = tempdir().unwrap();
        let dest = tempdir().unwrap();
        let dest_dir = dest.path().join("mirror");
        fs::write(src.path().join("setup.h"), "ORIGINAL").unwrap();
        let overrides = vec![(PathBuf::from("setup.h"), "REPLACED".into())];
        mirror(src.path(), &dest_dir, &overrides).unwrap();
        assert_eq!(fs::read_to_string(dest_dir.join("setup.h")).unwrap(), "REPLACED");
    }

    #[test]
    fn recurses_into_subdirs() {
        let src = tempdir().unwrap();
        let dest = tempdir().unwrap();
        let dest_dir = dest.path().join("mirror");
        let sub = src.path().join("data");
        fs::create_dir(&sub).unwrap();
        fs::write(sub.join("payload.bin"), b"abc").unwrap();
        mirror(src.path(), &dest_dir, &[]).unwrap();
        assert_eq!(fs::read(dest_dir.join("data/payload.bin")).unwrap(), b"abc");
    }

    #[test]
    fn skips_dot_boardghost() {
        let src = tempdir().unwrap();
        let dest = tempdir().unwrap();
        let dest_dir = dest.path().join("mirror");
        let bg = src.path().join(".boardghost");
        fs::create_dir(&bg).unwrap();
        fs::write(bg.join("stale.txt"), "leftover").unwrap();
        fs::write(src.path().join("real.ino"), "void setup(){}").unwrap();
        mirror(src.path(), &dest_dir, &[]).unwrap();
        assert!(!dest_dir.join(".boardghost").exists());
        assert!(dest_dir.join("real.ino").exists());
    }

    #[test]
    fn clears_existing_dest() {
        let src = tempdir().unwrap();
        let dest = tempdir().unwrap();
        let dest_dir = dest.path().join("mirror");
        fs::create_dir_all(&dest_dir).unwrap();
        fs::write(dest_dir.join("stale.txt"), "old").unwrap();
        fs::write(src.path().join("new.txt"), "new").unwrap();
        mirror(src.path(), &dest_dir, &[]).unwrap();
        assert!(!dest_dir.join("stale.txt").exists());
        assert!(dest_dir.join("new.txt").exists());
    }
}
```

In `crates/boardghost-cli/src/lib.rs`:

```rust
pub mod sketch_mirror;
```

- [ ] **Step 2: Run tests**

```bash
cargo test -p boardghost-cli --lib sketch_mirror
```

Expected: 5 tests pass.

- [ ] **Step 3: Commit**

```bash
git add crates/boardghost-cli/src/sketch_mirror.rs \
        crates/boardghost-cli/src/lib.rs
git commit -m "feat(cli): sketch_mirror — recursive sketch dir copy with selective overrides"
```

---

## Task 5: Wire codemod into build.rs

**Files:**
- Modify: `crates/boardghost-cli/src/build.rs`
- Modify: `crates/boardghost-cli/src/lgfx_codemod.rs` (add the `try_apply` orchestrator)

- [ ] **Step 1: Add `try_apply` to lgfx_codemod.rs**

Insert AFTER `generate_sim_wrapper` and BEFORE the `#[cfg(test)]` block:

```rust
/// Report describing what the codemod did. Returned to the build pipeline so
/// stage logs can show the user what was rewritten.
#[derive(Debug, Clone)]
pub struct CodemodReport {
    /// Relative path (under sketch dir) of the file that got rewritten.
    pub rewritten_rel_path: PathBuf,
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
```

(The function returns `anyhow::Result` so `build.rs` can use `?` and propagate context.)

- [ ] **Step 2: Integrate into build.rs**

Open `crates/boardghost-cli/src/build.rs`. Add `sketch_mirror` and `lgfx_codemod` to the import block at the top:

```rust
use crate::{
    arduino_libs, board::BoardProfile, codegen, compile, discover, include_scan,
    lgfx_codemod, lib_resolve, libraries, preprocess, sketch_mirror,
};
```

Find the line that establishes `discovered` from `discover::discover(project)?`. Just after the `eprintln!("→ Board:  ...")` line and BEFORE Stage 2a, add:

```rust
    // Stage 1.5: LGFX codemod. If the sketch has a hardware LGFX setup file
    // (e.g. cryptoTickerv3's lvgfx_setup.h), mirror the sketch dir into our
    // build cache with the setup file replaced by a Panel_sdl wrapper. The
    // preprocess + compile stages then operate on the mirror.
    let sketch_dir = discovered.entry.parent().unwrap_or(Path::new(".")).to_path_buf();
    let out_root  = project.join(".boardghost").join(&board.name);
    std::fs::create_dir_all(&out_root).context("create .boardghost dir")?;

    let codemod = lgfx_codemod::try_apply(&sketch_dir)?;
    let (effective_sketch_entry, effective_sketch_dir) = if let Some(ref report) = codemod {
        eprintln!(
            "→ LGFX codemod: rewriting {} → Panel_sdl {}x{} (rotation {})",
            report.rewritten_rel_path.display(),
            report.parsed.panel_width,
            report.parsed.panel_height,
            report.parsed.offset_rotation,
        );
        let mirror_dir = out_root.join("sketch_src");
        sketch_mirror::mirror(
            &sketch_dir,
            &mirror_dir,
            &[(report.rewritten_rel_path.clone(), report.sim_contents.clone())],
        )?;
        let new_entry = mirror_dir.join(
            discovered.entry.file_name().expect("sketch entry has a name")
        );
        (new_entry, mirror_dir)
    } else {
        (discovered.entry.clone(), sketch_dir.clone())
    };
```

THEN replace every later reference to `discovered.entry` with `effective_sketch_entry`, and replace `discovered.entry.parent().unwrap_or(Path::new("."))` (used in the Stage 2a.1 local-headers filter) with `&effective_sketch_dir`.

Specifically:

(a) The Stage 2a `include_scan::scan_file(&discovered.entry)` call becomes:
```rust
    let headers = include_scan::scan_file(&effective_sketch_entry)
        .with_context(|| format!("scan {:?}", effective_sketch_entry))?;
```

(b) The Stage 2a.1 local-headers filter (`sketch_dir` variable) becomes:
```rust
    let sketch_dir_for_filter: &Path = &effective_sketch_dir;
    let displays_dir = runtime_dir.join("displays");
    let include_dir  = runtime_dir.join("include");
    let local_search_dirs: [&Path; 3] = [
        sketch_dir_for_filter,
        &displays_dir,
        &include_dir,
    ];
    let library_headers = include_scan::filter_local_headers(headers, &local_search_dirs);
```

(c) The Stage 2c `preprocess::preprocess(&discovered.entry, ...)` call becomes:
```rust
    let preprocessed = preprocess::preprocess(
        &effective_sketch_entry,
        &board.arduino_fqbn_hint,
        &extra_includes,
    ).context("Stage 2c: preprocess")?;
```

(d) Remove the old `let sketch_dir = discovered.entry.parent()...;` declaration that now sits inside the original 2a.1 block — it's superseded by the variable introduced in Stage 1.5.

Also REMOVE the old `let out_dir = project.join(".boardghost").join(&board.name);` line and the `std::fs::create_dir_all(&out_dir)` that follow it (they're now established as `out_root` in Stage 1.5). Reference `out_root` from Stage 4 onwards:

```rust
    let _ = codegen::generate_cmake(&codegen::CodegenInput {
        sketch_cpp:  sketch_cpp_abs,
        board:       &board,
        runtime_dir: runtime_abs.clone(),
        release,
        out_dir:     out_root.clone(),
        libraries:   resolved,
    })?;

    eprintln!("→ Compiling...");
    let out = compile::cmake_configure_and_build(&out_root)?;
```

- [ ] **Step 3: Build the whole crate to verify no broken refs**

```bash
cargo build -p boardghost-cli
cargo test -p boardghost-cli
```

Expected: clean build, all tests still pass (codemod ones + the previous 22).

- [ ] **Step 4: Re-run all 4 E2Es locally to confirm no regression**

```bash
./tests/e2e/run_hello_serial.sh
./tests/e2e/run_ssd1306_text.sh
./tests/e2e/run_lvgl_hello.sh
./tests/e2e/cryptoticker_smoke.sh
```

Expected: all four print `PASS:` or `✅ ... passed`.

If ssd1306_text or cryptoticker_smoke fails, the local-headers filter or out_dir refactor probably went wrong. Check the diff against build.rs's original structure carefully.

- [ ] **Step 5: Commit**

```bash
git add crates/boardghost-cli/src/build.rs \
        crates/boardghost-cli/src/lgfx_codemod.rs
git commit -m "feat(cli): wire lgfx_codemod into build pipeline (Stage 1.5)"
```

---

## Task 6: cryptoTickerv3 end-to-end validation

**Files:**
- This task is exploratory: no new files, just verification.

- [ ] **Step 1: Build cryptoTickerv3 through the codemod**

```bash
cargo run -p boardghost-cli --quiet -- build \
    --board st7789_esp32s3_sim \
    /home/phill/Arduino/arduinoProjects/cryptoTickerv3 2>&1 | tee /tmp/cv3-build.log | tail -30
```

Expected outcomes:

**Path A (full success):** build completes, log ends with `Built: .../sketch`. The early lines should include:
```
→ LGFX codemod: rewriting lvgfx_setup.h → Panel_sdl 320x480 (rotation 2)
```

**Path B (compile error in sim wrapper):** the generated sim wrapper has a bug (probably a template syntax glitch). Read the wrapper at `/home/phill/Arduino/arduinoProjects/cryptoTickerv3/.boardghost/st7789_esp32s3_sim/sketch_src/lvgfx_setup.h` and fix the Tera template or the parser. Re-run.

**Path C (next gap surfaced):** compile fails on something unrelated (probably a method call into LGFX_Device that we don't shim, or a font issue). Document the gap in the plan as "M2.F follow-up" and stop — the codemod itself worked.

- [ ] **Step 2: If build succeeds, run the sketch**

```bash
cargo run -p boardghost-cli -- run \
    --board st7789_esp32s3_sim \
    --screenshot /tmp/cv3.png \
    /home/phill/Arduino/arduinoProjects/cryptoTickerv3 2>&1 | tail -30
```

Expected: an SDL window appears showing the sketch's output (some crypto ticker UI). Screenshot saved to `/tmp/cv3.png`.

Even if the WiFi/HTTP calls return empty data in fake mode, the UI should render its empty-state.

- [ ] **Step 3: Document the outcome**

Append a new section to `docs/superpowers/plans/2026-05-29-boardghost-m2c-arduino-library-discovery.md` (the M2.C plan) titled `## cryptoTickerv3 second-contact (M2.E codemod)` with:
- Which path was taken (A/B/C)
- Build log excerpt
- Screenshot path
- Any gaps surfaced for follow-up

- [ ] **Step 4: Commit findings (only if doc was updated)**

```bash
git add docs/superpowers/plans/2026-05-29-boardghost-m2c-arduino-library-discovery.md
git commit -m "docs(m2e): cryptoTickerv3 second-contact findings"
```

---

## Task 7: CI gating + README docs

**Files:**
- Create: `examples/lgfx_codemod_smoke/sketch/sketch.ino`
- Create: `examples/lgfx_codemod_smoke/sketch/lvgfx_setup.h`
- Create: `examples/lgfx_codemod_smoke/README.md`
- Create: `tests/e2e/lgfx_codemod_smoke.sh`
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`

- [ ] **Step 1: Build a sham hardware-setup example for CI**

We can't depend on `~/Arduino/arduinoProjects/cryptoTickerv3/` in CI (it's a user-private sketch). Instead, ship a small example that exercises the same codemod path.

Create `examples/lgfx_codemod_smoke/sketch/lvgfx_setup.h`:

```cpp
// Simulated hardware setup — exercises the M2.E LGFX codemod.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    LGFX(void)
    {
        auto cfg = _panel_instance.config();
        cfg.panel_width  = 320;
        cfg.panel_height = 480;
        cfg.offset_rotation = 2;
        _panel_instance.config(cfg);
        setPanel(&_panel_instance);
    }
};
```

Create `examples/lgfx_codemod_smoke/sketch/sketch.ino`:

```cpp
// M2.E codemod smoke test: a sketch with a hardware LGFX setup file that
// boardghost transparently rewrites for sim execution.
#include "lvgfx_setup.h"

static LGFX tft;
static int frame = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("lgfx_codemod_smoke: setup");
    tft.init();
    tft.fillScreen(0xF800);  // red
    sim_set_active_display(&tft);
}

void loop() {
    if (++frame > 200) {
        Serial.println("lgfx_codemod_smoke: done");
        exit(0);
    }
    delay(10);
}
```

Create `examples/lgfx_codemod_smoke/README.md`:

```markdown
# lgfx_codemod_smoke

Smoke test for the M2.E LGFX codemod. The sketch ships a hardware-style
`lvgfx_setup.h` (Panel_ILI9488 + Bus_SPI) and expects boardghost to
rewrite it into a Panel_sdl-backed simulator wrapper at build time.

## Run

```bash
boardghost run --board st7789_esp32s3_sim examples/lgfx_codemod_smoke
```

Auto-exits after ~200 frames.
```

- [ ] **Step 2: Write the E2E script**

Create `tests/e2e/lgfx_codemod_smoke.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

# Build via boardghost (should fire the LGFX codemod).
cargo run -p boardghost-cli --quiet -- build \
    --board st7789_esp32s3_sim \
    examples/lgfx_codemod_smoke 2>&1 | tee /tmp/lgfx_codemod_smoke.build.log

if ! grep -q "LGFX codemod: rewriting" /tmp/lgfx_codemod_smoke.build.log; then
    echo "❌ codemod did not fire on lgfx_codemod_smoke"
    tail -20 /tmp/lgfx_codemod_smoke.build.log
    exit 1
fi

# Run and check output.
TMPDIR=$(mktemp -d)
cargo run -p boardghost-cli --quiet -- run \
    --board st7789_esp32s3_sim \
    --screenshot "$TMPDIR/shot.png" \
    examples/lgfx_codemod_smoke \
    > "$TMPDIR/out.log" 2>&1 || true

if grep -q "lgfx_codemod_smoke: done" "$TMPDIR/out.log"; then
    echo "✅ lgfx_codemod_smoke E2E passed"
    rm -rf "$TMPDIR"
    exit 0
fi

echo "❌ lgfx_codemod_smoke did not reach end:"
tail -40 "$TMPDIR/out.log"
exit 1
```

Make it executable:

```bash
chmod +x tests/e2e/lgfx_codemod_smoke.sh
```

- [ ] **Step 3: Run locally**

```bash
./tests/e2e/lgfx_codemod_smoke.sh
```

Expected: `✅ lgfx_codemod_smoke E2E passed`.

- [ ] **Step 4: Wire into CI**

Open `.github/workflows/ci.yml`. In the `build-and-test` job, after the existing E2E for `cryptoticker_smoke`, add:

```yaml
      - name: E2E — lgfx_codemod_smoke (M2.E)
        run: ./tests/e2e/lgfx_codemod_smoke.sh
        env:
          BOARDGHOST_NET: fake
```

- [ ] **Step 5: Document in README**

Open `README.md`. Find the "Using custom Arduino libraries (M2.C)" section. Insert a new section directly after it:

````markdown
## Using sketches with hardware LGFX setups (M2.E)

If your sketch ships its own `lvgfx_setup.h` (or similar header) that declares a
`class LGFX : public lgfx::LGFX_Device` using hardware classes like
`lgfx::Panel_ILI9488` + `lgfx::Bus_SPI` + `lgfx::Touch_XPT2046`, boardghost
auto-detects it and rewrites the setup file at build time into a
`lgfx::Panel_sdl`-backed version that matches your declared panel dimensions
and rotation.

Your original sketch on disk is **never modified**. The rewritten copy lives
at `.boardghost/<board>/sketch_src/`.

```bash
# Just build — the codemod fires transparently.
boardghost run --board st7789_esp32s3_sim my-cool-sketch/
```

You'll see a line like:

```
→ LGFX codemod: rewriting lvgfx_setup.h → Panel_sdl 320x480 (rotation 2)
```

### When the codemod skips

It skips silently when:
- The setup file already references an `LGFX_*_SDL.hpp` header (you've already
  done the swap).
- The regex parser can't extract panel dimensions (e.g. they come from a
  `#define` instead of literal numbers in the config). Add the dims as literals
  in your hardware setup, or open an issue with the source so the parser can
  be extended.
````

- [ ] **Step 6: Commit**

```bash
git add examples/lgfx_codemod_smoke/ \
        tests/e2e/lgfx_codemod_smoke.sh \
        .github/workflows/ci.yml \
        README.md
git commit -m "test+docs: M2.E lgfx_codemod_smoke E2E + README section"
```

---

## Task 8: Tag the release

**Files:**
- None.

- [ ] **Step 1: Verify the full test matrix is green**

```bash
cargo test -p boardghost-cli
./tests/e2e/run_hello_serial.sh
./tests/e2e/run_ssd1306_text.sh
./tests/e2e/run_lvgl_hello.sh
./tests/e2e/cryptoticker_smoke.sh
./tests/e2e/lgfx_codemod_smoke.sh
```

Expected: all green.

- [ ] **Step 2: Push and wait for CI**

```bash
git push origin main
```

Use `gh run watch` or `gh run list --limit 1` to confirm CI passes. Do NOT tag until CI is green.

- [ ] **Step 3: Tag**

Once CI reports `conclusion: success`:

```bash
git tag m2e-lgfx-codemod
git push origin m2e-lgfx-codemod
```

---

## Done criteria

- [ ] All 7 implementation tasks merged on `main`.
- [ ] 11+ new unit tests in `lgfx_codemod` + 5 in `sketch_mirror` — all passing.
- [ ] All 5 E2Es green locally and in CI (4 existing + lgfx_codemod_smoke).
- [ ] cryptoTickerv3 either builds + runs cleanly OR has its next-gap findings written up in M2.C plan's `cryptoTickerv3 second-contact` section.
- [ ] `m2e-lgfx-codemod` tag pushed.
- [ ] README has the "Using sketches with hardware LGFX setups" section.
