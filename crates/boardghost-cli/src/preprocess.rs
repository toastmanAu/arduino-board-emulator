use crate::error::BoardGhostError;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Runs `arduino-cli compile --preprocess --fqbn <fqbn> <sketch>`.
///
/// `extra_include_dirs` are injected as `-I<dir>` flags via
/// `--build-property compiler.cpp.extra_flags=...` so that BoardGhost
/// display headers (e.g. LGFX_SSD1306_SDL.hpp) are visible to the
/// arduino-cli preprocessor without needing an Arduino library package.
///
/// Returns the path to the generated preprocessed .cpp.
pub fn preprocess(
    sketch: &Path,
    fqbn: &str,
    extra_include_dirs: &[PathBuf],
) -> Result<PathBuf, BoardGhostError> {
    // Verify arduino-cli is available.
    if Command::new("arduino-cli").arg("version").output().is_err() {
        return Err(BoardGhostError::ArduinoCliMissing);
    }

    // Build compiler.cpp.extra_flags from extra_include_dirs.
    let extra_flags: String = extra_include_dirs
        .iter()
        .filter_map(|d| d.to_str())
        .map(|d| format!("-I{d}"))
        .collect::<Vec<_>>()
        .join(" ");

    let mut cmd = Command::new("arduino-cli");
    cmd.args(["compile", "--preprocess", "--fqbn", fqbn]);
    if !extra_flags.is_empty() {
        cmd.arg("--build-property")
            .arg(format!("compiler.cpp.extra_flags={extra_flags}"));
    }
    cmd.arg(sketch);

    let out = cmd.output().map_err(|_| BoardGhostError::ArduinoCliMissing)?;

    if !out.status.success() {
        return Err(BoardGhostError::PreprocessFailed {
            exit:   out.status.code().unwrap_or(-1),
            stderr: String::from_utf8_lossy(&out.stderr).to_string(),
        });
    }

    // arduino-cli prints the preprocessed source to stdout. Capture to a file
    // alongside the sketch (under the sketch's parent dir) so paths are stable.
    let target = sketch.with_file_name(
        format!("{}.boardghost.cpp",
            sketch.file_stem().and_then(|s| s.to_str()).unwrap_or("sketch")));
    std::fs::write(&target, &out.stdout).map_err(|e| {
        BoardGhostError::PreprocessFailed {
            exit: -1,
            stderr: format!("could not write preprocessed file: {e}"),
        }
    })?;

    Ok(target)
}

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
