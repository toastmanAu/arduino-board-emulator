use crate::error::BoardGhostError;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Runs `arduino-cli compile --preprocess --fqbn <fqbn> <sketch>`.
/// Returns the path to the generated preprocessed .cpp.
pub fn preprocess(sketch: &Path, fqbn: &str) -> Result<PathBuf, BoardGhostError> {
    // Verify arduino-cli is available.
    if Command::new("arduino-cli").arg("version").output().is_err() {
        return Err(BoardGhostError::ArduinoCliMissing);
    }

    let out = Command::new("arduino-cli")
        .args(["compile", "--preprocess", "--fqbn", fqbn])
        .arg(sketch)
        .output()
        .map_err(|_| BoardGhostError::ArduinoCliMissing)?;

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
