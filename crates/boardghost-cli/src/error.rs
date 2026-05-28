use std::path::PathBuf;
use thiserror::Error;

// Minimal stub - extended in later tasks.
// We use anyhow at boundaries and named variants for user-facing errors.
#[derive(Debug, Error)]
pub enum BoardGhostError {
    #[error("Board profile not found: {0}")]
    UnknownBoard(String),

    #[error("Board profile at {path:?} is malformed: {reason}")]
    BoardProfileMalformed { path: PathBuf, reason: String },

    #[error("Could not list board profiles in {path:?}: {reason}")]
    BoardListFailed { path: PathBuf, reason: String },

    #[error("No sketch found in {dir:?}. Expected sketch.ino or src/main.cpp")]
    SketchNotFound { dir: PathBuf },

    #[error("Project directory does not exist: {0:?}")]
    ProjectNotFound(PathBuf),

    #[error("arduino-cli preprocess failed (exit {exit}): {stderr}")]
    PreprocessFailed { exit: i32, stderr: String },

    #[error("arduino-cli not found in PATH; run `boardghost doctor` to diagnose")]
    ArduinoCliMissing,

    #[error("Header <{header}> is not provided by any installed Arduino library.\n  \
             Try: arduino-cli lib search <name> && arduino-cli lib install <name>\n  \
             Or shim it under runtime/shims/.")]
    LibraryNotInstalled { header: String },

    #[error("Library {name} resolved but no source files were globbed (check library_overrides/{name}.toml).")]
    LibraryEmpty { name: String },

    #[error("CMake configure failed (exit {exit}): {stderr}")]
    CmakeConfigureFailed { exit: i32, stderr: String },

    #[error("CMake build failed (exit {exit}): {stderr}")]
    CmakeBuildFailed { exit: i32, stderr: String },

    #[error("arduino-cli lib list failed: {stderr}")]
    ArduinoLibListFailed { stderr: String },
}
