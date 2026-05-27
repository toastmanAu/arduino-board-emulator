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
}
