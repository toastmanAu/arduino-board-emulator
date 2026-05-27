use crate::error::BoardGhostError;
use std::path::{Path, PathBuf};
use std::process::Command;

pub struct CompileOutput {
    pub binary: PathBuf,
}

pub fn cmake_configure_and_build(build_dir: &Path) -> Result<CompileOutput, BoardGhostError> {
    let bin_dir = build_dir.join("build");
    std::fs::create_dir_all(&bin_dir).map_err(|e| BoardGhostError::CmakeConfigureFailed {
        exit: -1, stderr: format!("mkdir {:?}: {e}", bin_dir),
    })?;

    let cfg = Command::new("cmake")
        .args(["-S", build_dir.to_str().unwrap(), "-B", bin_dir.to_str().unwrap()])
        .output()
        .map_err(|e| BoardGhostError::CmakeConfigureFailed { exit: -1, stderr: e.to_string() })?;
    if !cfg.status.success() {
        return Err(BoardGhostError::CmakeConfigureFailed {
            exit: cfg.status.code().unwrap_or(-1),
            stderr: format!("{}{}",
                String::from_utf8_lossy(&cfg.stdout),
                String::from_utf8_lossy(&cfg.stderr)),
        });
    }

    let build = Command::new("cmake")
        .args(["--build", bin_dir.to_str().unwrap(), "-j"])
        .output()
        .map_err(|e| BoardGhostError::CmakeBuildFailed { exit: -1, stderr: e.to_string() })?;
    if !build.status.success() {
        return Err(BoardGhostError::CmakeBuildFailed {
            exit: build.status.code().unwrap_or(-1),
            stderr: format!("{}{}",
                String::from_utf8_lossy(&build.stdout),
                String::from_utf8_lossy(&build.stderr)),
        });
    }

    Ok(CompileOutput { binary: bin_dir.join("sketch") })
}
